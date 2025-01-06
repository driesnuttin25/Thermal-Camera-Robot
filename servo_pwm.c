#include "xparameters.h"
#include "xtmrctr.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include <stdio.h>
#include <unistd.h>
#include "platform.h"

#define SCUGIC_DEVICE_ID        XPAR_SCUGIC_SINGLE_DEVICE_ID
#define AXI_TIMER_DEVICE_ID_0   XPAR_AXI_TIMER_0_DEVICE_ID
#define AXI_TIMER_DEVICE_ID_1   XPAR_AXI_TIMER_1_DEVICE_ID

#define AXI_TIMER_CHANNEL       0

#define AXI_TIMER_PWM_TEST_SW   1

#define SERVO_PWM_PERIOD_US     20000

XTmrCtr xTmrCtr_Inst0;
XTmrCtr xTmrCtr_Inst1;
XScuGic xScuGic_Inst;

void xTmrCtr_Int_Handler(void *CallBackRef, u8 TmrCtrNumber)
{
    // Interrupt handler code (if needed)
}

u32 xTmr_US_To_RegValue(u32 US)
{
    // Convert microseconds to timer register value
    // Assuming timer clock is 100 MHz (10 ns period)
    u32 Value = (US * 100);  // 100 ticks per microsecond
    return 0xFFFFFFFF - Value + 1;
}

u32 xTmr_US_To_NS(u32 US)
{
    return US * 1000;
}

int xTmrCtr_Init(XTmrCtr *xTmrCtr_Ptr, u32 DeviceId)
{
    int Status;

    Status = XTmrCtr_Initialize(xTmrCtr_Ptr, DeviceId);
    if (Status != XST_SUCCESS)
    {
        return XST_FAILURE;
    }

    Status = XTmrCtr_SelfTest(xTmrCtr_Ptr, AXI_TIMER_CHANNEL);
    if (Status != XST_SUCCESS)
    {
        return XST_FAILURE;
    }

    XTmrCtr_SetHandler(xTmrCtr_Ptr, xTmrCtr_Int_Handler, xTmrCtr_Ptr);

    // Set the period for the timer (we'll set this in the SetServoAngle function)
    XTmrCtr_SetResetValue(xTmrCtr_Ptr, AXI_TIMER_CHANNEL, xTmr_US_To_RegValue(SERVO_PWM_PERIOD_US));

    // Set the options for the timer
    XTmrCtr_SetOptions(xTmrCtr_Ptr, AXI_TIMER_CHANNEL, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);

    return XST_SUCCESS;
}

// Function to set the servo angle
void SetServoAngle(XTmrCtr *xTmrCtr_Ptr, u8 angle)
{
    u32 highTimeUs;

    // Clamp angle to 0-180 degrees
    if (angle > 180)
        angle = 180;

    // Map angle to pulse width (1000 µs to 2000 µs)
    highTimeUs = 1000 + ((angle * 1000) / 180);

    // Disable PWM before reconfiguring
    XTmrCtr_PwmDisable(xTmrCtr_Ptr);

    // Stop the timer
    XTmrCtr_Stop(xTmrCtr_Ptr, AXI_TIMER_CHANNEL);

    // Configure the PWM with the new high time
    XTmrCtr_PwmConfigure(xTmrCtr_Ptr, xTmr_US_To_NS(SERVO_PWM_PERIOD_US), xTmr_US_To_NS(highTimeUs));

    // Start the timer
    XTmrCtr_Start(xTmrCtr_Ptr, AXI_TIMER_CHANNEL);

    // Enable PWM after reconfiguring
    XTmrCtr_PwmEnable(xTmrCtr_Ptr);
}

int main()
{
    int Status;
    u8 angleServo0 = 0;    // Starting angle for servo 0
    u8 angleServo1 = 180;  // Starting angle for servo 1
    int angleStep = 10;    // Angle increment/decrement step
    int increasing0 = 1;   // Flag for servo 0 angle direction
    int increasing1 = 0;   // Flag for servo 1 angle direction

    xil_printf("Servo Control Test!\n");

    // Initialize and configure AXI_TIMER_0 for PWM
    Status = xTmrCtr_Init(&xTmrCtr_Inst0, AXI_TIMER_DEVICE_ID_0);
    if (Status != XST_SUCCESS)
    {
        xil_printf("AXI Timer 0 Init Error!\n");
        return XST_FAILURE;
    }

    // Initialize and configure AXI_TIMER_1 for PWM
    Status = xTmrCtr_Init(&xTmrCtr_Inst1, AXI_TIMER_DEVICE_ID_1);
    if (Status != XST_SUCCESS)
    {
        xil_printf("AXI Timer 1 Init Error!\n");
        return XST_FAILURE;
    }

    // Start the PWM timers
    XTmrCtr_Start(&xTmrCtr_Inst0, AXI_TIMER_CHANNEL);
    XTmrCtr_Start(&xTmrCtr_Inst1, AXI_TIMER_CHANNEL);

#if AXI_TIMER_PWM_TEST_SW
    // Enable PWM output
    XTmrCtr_PwmEnable(&xTmrCtr_Inst0);
    XTmrCtr_PwmEnable(&xTmrCtr_Inst1);
#endif

    // Main loop to vary the servo angles
    while (1)
    {
        // Set the angles for both servos
        SetServoAngle(&xTmrCtr_Inst0, angleServo0);
        SetServoAngle(&xTmrCtr_Inst1, angleServo1);

        xil_printf("Servo 0 Angle: %d°, Servo 1 Angle: %d°\n", angleServo0, angleServo1);

        usleep(50000);  // Wait for 50 milliseconds

        // Update servo 0 angle
        if (increasing0)
        {
            angleServo0 += angleStep;
            if (angleServo0 >= 180)
            {
                angleServo0 = 180;
                increasing0 = 0;  // Start decreasing
            }
        }
        else
        {
            angleServo0 -= angleStep;
            if (angleServo0 <= 0)
            {
                angleServo0 = 0;
                increasing0 = 1;  // Start increasing
            }
        }

        // Update servo 1 angle
        if (increasing1)
        {
            angleServo1 += angleStep;
            if (angleServo1 >= 180)
            {
                angleServo1 = 180;
                increasing1 = 0;  // Start decreasing
            }
        }
        else
        {
            angleServo1 -= angleStep;
            if (angleServo1 <= 0)
            {
                angleServo1 = 0;
                increasing1 = 1;  // Start increasing
            }
        }
    }

    return 0;
}
