#include "xparameters.h"
#include "xtmrctr.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include "xiic.h"
#include <stdio.h>
#include <unistd.h>
#include "platform.h"
#include "AXI_NeoPixel.h"
#include "xil_io.h"

// Servo Control Definitions
#define SCUGIC_DEVICE_ID        XPAR_SCUGIC_SINGLE_DEVICE_ID
#define AXI_TIMER_DEVICE_ID_0   XPAR_AXI_TIMER_0_DEVICE_ID
#define AXI_TIMER_DEVICE_ID_1   XPAR_AXI_TIMER_1_DEVICE_ID

#define AXI_TIMER_CHANNEL       0
#define AXI_TIMER_PWM_TEST_SW   1
#define SERVO_PWM_PERIOD_US     20000  // 20 ms period for servo control

XTmrCtr xTmrCtr_Inst0;
XTmrCtr xTmrCtr_Inst1;
XScuGic xScuGic_Inst;

XIic Iic;  // I2C instance

// Infrared Sensor Definitions
#define AMG8833_ADDRESS 0x69
#define AMG8833_PIXEL_START_ADDRESS 0x80
#define AMG8833_PIXEL_ARRAY_SIZE 64
#define NUM_HOT_PIXELS 5

u8 PixelData[AMG8833_PIXEL_ARRAY_SIZE * 2];
int TemperatureArray[8][8]; // This holds raw data in quarter-degrees C

#define NEOPIXEL_BASEADDR XPAR_AXI_NEOPIXEL_0_S00_AXI_BASEADDR

// -- Absolute Temperature Thresholds (in °C) --
#define MIN_COLD_C   10.0f  // Below this => show green
#define MID_WARM_C   20.0f  // 20-30 => transitions green->orange
#define HOT_C        35.0f  // 30-50 => transitions orange->red
#define MAX_CLAMP_C  50.0f  // Above 50 => clamp at bright red

// Function Prototypes
void xTmrCtr_Int_Handler(void *CallBackRef, u8 TmrCtrNumber);
u32 xTmr_US_To_RegValue(u32 US);
u32 xTmr_US_To_NS(u32 US);
int xTmrCtr_Init(XTmrCtr *xTmrCtr_Ptr, u32 DeviceId);
void SetServoAngle(XTmrCtr *xTmrCtr_Ptr, u8 angle);
int InitIIC(void);
int ReadAMG8833(u8 *RecvBuffer, u16 ByteCount);

// Helper function to convert raw AMG8833 data to °C
static inline float RawToCelsius(int16_t rawValue)
{
    // AMG8833 data is in quarter-degree increments
    // rawValue of 4 => 1°C
    // so we do: (rawValue / 4.0)
    return (float)rawValue / 4.0f;
}

// Helper function: map an absolute temperature (°C) to a color
static u32 TempToColor(float tempC)
{
    // Scale factor for brightness
    float brightness = 0.1f;

    u8 red   = 0;
    u8 green = 0;
    u8 blue  = 0;

    // In your TempToColor(float tempC) function, replace the existing blocks with the following:

    if (tempC <= MIN_COLD_C) {
        // Below 20°C => red
        red   = (u8)(255 * brightness);
        green = 0;
        blue  = 0;
    }
    else if (tempC <= MID_WARM_C) {
        // 20°C..30°C => red -> orange
        float ratio = (tempC - MIN_COLD_C) / (MID_WARM_C - MIN_COLD_C);
        // red stays at 255, green goes 0->128, for a "reddish-orange" effect
        red   = (u8)(255 * brightness);
        green = (u8)((ratio * 128) * brightness);
        blue  = 0;
    }
    else if (tempC <= HOT_C) {
        // 30°C..50°C => orange -> green
        float ratio = (tempC - MID_WARM_C) / (HOT_C - MID_WARM_C);
        // orange (255,128,0) -> green (0,255,0)
        red   = (u8)((255 - ratio * 255) * brightness);
        green = (u8)((128 + ratio * 127) * brightness);
        blue  = 0;
    }
    else {
        // Above 50°C => clamp to bright green
        red   = 0;
        green = (u8)(255 * brightness);
        blue  = 0;
    }


    return ((u32)red << 16) | ((u32)green << 8) | (u32)blue;
}

int main() {
    int Status;

    // Filtered angles for smooth servo movement
    float filteredAngleX = 90.0f;
    float filteredAngleY = 90.0f;

    init_platform();

    xil_printf("Initializing Servo Control...\n");

    // Initialize timers for servos
    Status = xTmrCtr_Init(&xTmrCtr_Inst0, AXI_TIMER_DEVICE_ID_0);
    if (Status != XST_SUCCESS) {
        xil_printf("AXI Timer 0 Init Error!\n");
        cleanup_platform();
        return XST_FAILURE;
    }

    Status = xTmrCtr_Init(&xTmrCtr_Inst1, AXI_TIMER_DEVICE_ID_1);
    if (Status != XST_SUCCESS) {
        xil_printf("AXI Timer 1 Init Error!\n");
        cleanup_platform();
        return XST_FAILURE;
    }

    XTmrCtr_Start(&xTmrCtr_Inst0, AXI_TIMER_CHANNEL);
    XTmrCtr_Start(&xTmrCtr_Inst1, AXI_TIMER_CHANNEL);

#if AXI_TIMER_PWM_TEST_SW
    XTmrCtr_PwmEnable(&xTmrCtr_Inst0);
    XTmrCtr_PwmEnable(&xTmrCtr_Inst1);
#endif

    xil_printf("Initializing I2C for AMG8833 sensor...\n");

    Status = InitIIC();
    if (Status != XST_SUCCESS) {
        xil_printf("I2C Initialization Failed, exiting...\n");
        cleanup_platform();
        return XST_FAILURE;
    }

    xil_printf("Initialization Complete. Starting Main Loop...\n");

    while (1) {
        Status = ReadAMG8833(PixelData, AMG8833_PIXEL_ARRAY_SIZE * 2);
        if (Status != XST_SUCCESS) {
            xil_printf("Failed to read AMG8833 data, retrying...\n");
            usleep(500000);
            continue;
        }

        // Fill TemperatureArray (raw in quarter-deg)
        for (int i = 0; i < AMG8833_PIXEL_ARRAY_SIZE; i++) {
            int16_t rawVal = (int16_t)((PixelData[(2*i) + 1] << 8) | PixelData[2*i]);
            TemperatureArray[i / 8][i % 8] = rawVal;
        }

        // Convert each cell to degC, find hottest N pixels + servo tracking
        struct {
            float tempC;
            int x;
            int y;
        } hotPixels[AMG8833_PIXEL_ARRAY_SIZE];

        int idx = 0;
        float maxTempC = -999.0f;
        float minTempC = 999.0f;

        // Fill array & find min, max in degC
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                float tC = RawToCelsius(TemperatureArray[y][x]);
                hotPixels[idx].tempC = tC;
                hotPixels[idx].x     = x;
                hotPixels[idx].y     = y;
                idx++;

                if (tC < minTempC) minTempC = tC;
                if (tC > maxTempC) maxTempC = tC;
            }
        }

        // Sort by temperature desc
        for (int a = 0; a < AMG8833_PIXEL_ARRAY_SIZE - 1; a++) {
            for (int b = 0; b < AMG8833_PIXEL_ARRAY_SIZE - a - 1; b++) {
                if (hotPixels[b].tempC < hotPixels[b+1].tempC) {
                    float tt = hotPixels[b].tempC;
                    int tx   = hotPixels[b].x;
                    int ty   = hotPixels[b].y;

                    hotPixels[b].tempC = hotPixels[b+1].tempC;
                    hotPixels[b].x     = hotPixels[b+1].x;
                    hotPixels[b].y     = hotPixels[b+1].y;

                    hotPixels[b+1].tempC = tt;
                    hotPixels[b+1].x     = tx;
                    hotPixels[b+1].y     = ty;
                }
            }
        }

        // Compute average coords of top N hottest
        int count = (NUM_HOT_PIXELS > AMG8833_PIXEL_ARRAY_SIZE) ? AMG8833_PIXEL_ARRAY_SIZE : NUM_HOT_PIXELS;
        int sumX = 0, sumY = 0;
        for (int i = 0; i < count; i++) {
            sumX += hotPixels[i].x;
            sumY += hotPixels[i].y;
        }

        float centerX = (float)sumX / (float)count;
        float centerY = (float)sumY / (float)count;

        // Invert orientation if the sensor is upside down
        float targetAngleX = 180.0f - ((centerX * 180.0f) / 7.0f);
        float targetAngleY = 180.0f - ((centerY * 180.0f) / 7.0f);

        // Smooth servo movement
        filteredAngleX = filteredAngleX * 0.8f + targetAngleX * 0.2f;
        filteredAngleY = filteredAngleY * 0.8f + targetAngleY * 0.2f;

        // Clamp angles
        if (filteredAngleX < 0.0f)   filteredAngleX = 0.0f;
        if (filteredAngleX > 180.0f) filteredAngleX = 180.0f;
        if (filteredAngleY < 0.0f)   filteredAngleY = 0.0f;
        if (filteredAngleY > 180.0f) filteredAngleY = 180.0f;

        // Update servo angles
        SetServoAngle(&xTmrCtr_Inst0, (u8)filteredAngleX);
        SetServoAngle(&xTmrCtr_Inst1, (u8)filteredAngleY);

        // Now set the colors of each pixel based on absolute temperature
        for (int yy = 0; yy < 8; yy++) {
            for (int xx = 0; xx < 8; xx++) {
                float cellTempC = RawToCelsius(TemperatureArray[yy][xx]);
                u32 color       = TempToColor(cellTempC);
                int pixelIndex  = yy * 8 + xx;
                AXI_NEOPIXEL_mWriteReg(NEOPIXEL_BASEADDR, pixelIndex * 4, color);
            }
        }

        xil_printf("Top %d hot pixels center: (%.2f, %.2f), MaxTemp: %.2f°C, MinTemp: %.2f°C\n",
                   count, centerX, centerY, hotPixels[0].tempC, minTempC);
        xil_printf("Moving Servos to Angles -> Horizontal: %d°, Vertical: %d°\n",
                   (u8)filteredAngleX, (u8)filteredAngleY);

        // If your NeoPixel IP requires an "UPDATE" or "GO" command, do it here:
        // AXI_NEOPIXEL_mWriteReg(NEOPIXEL_BASEADDR, SOME_UPDATE_OFFSET, 0x1);

        usleep(50000); // 50 ms
    }

    cleanup_platform();
    return 0;
}

// Servo Control Functions
void xTmrCtr_Int_Handler(void *CallBackRef, u8 TmrCtrNumber) {
    // Not used
}

u32 xTmr_US_To_RegValue(u32 US) {
    // Assuming 100 MHz timer
    u32 Value = (US * 100);
    return 0xFFFFFFFF - Value + 1;
}

u32 xTmr_US_To_NS(u32 US) {
    return US * 1000;
}

int xTmrCtr_Init(XTmrCtr *xTmrCtr_Ptr, u32 DeviceId) {
    int Status = XTmrCtr_Initialize(xTmrCtr_Ptr, DeviceId);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Status = XTmrCtr_SelfTest(xTmrCtr_Ptr, AXI_TIMER_CHANNEL);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    XTmrCtr_SetHandler(xTmrCtr_Ptr, xTmrCtr_Int_Handler, xTmrCtr_Ptr);
    XTmrCtr_SetResetValue(xTmrCtr_Ptr, AXI_TIMER_CHANNEL, xTmr_US_To_RegValue(SERVO_PWM_PERIOD_US));
    XTmrCtr_SetOptions(xTmrCtr_Ptr, AXI_TIMER_CHANNEL, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);

    return XST_SUCCESS;
}

void SetServoAngle(XTmrCtr *xTmrCtr_Ptr, u8 angle) {
    if (angle > 180)
        angle = 180;

    u32 highTimeUs = 1000 + ((angle * 1000) / 180);

    XTmrCtr_PwmDisable(xTmrCtr_Ptr);
    XTmrCtr_Stop(xTmrCtr_Ptr, AXI_TIMER_CHANNEL);
    XTmrCtr_PwmConfigure(xTmrCtr_Ptr, xTmr_US_To_NS(SERVO_PWM_PERIOD_US), xTmr_US_To_NS(highTimeUs));
    XTmrCtr_Start(xTmrCtr_Ptr, AXI_TIMER_CHANNEL);
    XTmrCtr_PwmEnable(xTmrCtr_Ptr);
}

// Infrared Sensor Functions
int InitIIC(void) {
    XIic_Config *ConfigPtr = XIic_LookupConfig(XPAR_IIC_0_DEVICE_ID);
    if (ConfigPtr == NULL) {
        xil_printf("No IIC device found with ID %d\n", XPAR_IIC_0_DEVICE_ID);
        return XST_FAILURE;
    }

    int Status = XIic_CfgInitialize(&Iic, ConfigPtr, ConfigPtr->BaseAddress);
    if (Status != XST_SUCCESS) {
        xil_printf("IIC Initialization failed\n");
        return XST_FAILURE;
    }

    Status = XIic_SelfTest(&Iic);
    if (Status != XST_SUCCESS) {
        xil_printf("IIC Self Test failed\n");
        return XST_FAILURE;
    }

    xil_printf("IIC Initialized successfully.\n");
    return XST_SUCCESS;
}

int ReadAMG8833(u8 *RecvBuffer, u16 ByteCount) {
    u8 WriteBuffer[1] = { AMG8833_PIXEL_START_ADDRESS };
    int Status = XIic_Send(Iic.BaseAddress, AMG8833_ADDRESS, WriteBuffer, 1, XIIC_REPEATED_START);
    if (Status != 1) {
        xil_printf("Failed to set starting address, Status: %d\n", Status);
        return XST_FAILURE;
    }

    Status = XIic_Recv(Iic.BaseAddress, AMG8833_ADDRESS, RecvBuffer, ByteCount, XIIC_STOP);
    if (Status != ByteCount) {
        xil_printf("I2C Read Failed with Status: %d\n", Status);
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}
