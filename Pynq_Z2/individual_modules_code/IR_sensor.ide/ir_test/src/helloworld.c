#include <stdio.h>
#include <unistd.h>
#include "platform.h"
#include "xiic.h"
#include "xparameters.h"
#include "xil_printf.h"

#define AMG8833_ADDRESS 0x69
#define AMG8833_PIXEL_START_ADDRESS 0x80
#define AMG8833_PIXEL_ARRAY_SIZE 64

u8 PixelData[AMG8833_PIXEL_ARRAY_SIZE * 2];

int InitIIC() {
    xil_printf("Initializing I2C...\n");
    xil_printf("I2C Initialized successfully.\n");
    return XST_SUCCESS;
}

int ReadAMG8833(u8 *RecvBuffer, u16 ByteCount) {
    u8 WriteBuffer[1] = { AMG8833_PIXEL_START_ADDRESS };  // Tell the sensor where to start reading from (pixel data start)
    int Status;

    xil_printf("Attempting to read %d bytes from AMG8833 sensor...\n", ByteCount);

    // Send the address where we want to start reading from
    Status = XIic_Send(XPAR_IIC_0_BASEADDR, AMG8833_ADDRESS, WriteBuffer, 1, XIIC_REPEATED_START);
    if (Status != 1) {
        xil_printf("Failed to set starting address, Status: %d\n", Status);
        return XST_FAILURE;
    }

    // Now we receive the pixel data from the sensor
    Status = XIic_Recv(XPAR_IIC_0_BASEADDR, AMG8833_ADDRESS, RecvBuffer, ByteCount, XIIC_STOP);
    if (Status != ByteCount) {
        xil_printf("I2C Read Failed with Status: %d\n", Status);
        return XST_FAILURE;
    }

    xil_printf("I2C Read Successful!\n");
    return XST_SUCCESS;
}

int main() {
    int Status;
    int16_t PixelValue;
    int TemperatureArray[8][8];

    init_platform();

    Status = InitIIC();
    if (Status != XST_SUCCESS) {
        xil_printf("I2C Initialization Failed, exiting...\n");
        cleanup_platform();
        return XST_FAILURE;
    }

    while (1) {
        xil_printf("Reading AMG8833 temperature data...\n");

        // Read the 64 pixels from the sensor, which are 2 bytes each (so 128 bytes total)
        Status = ReadAMG8833(PixelData, AMG8833_PIXEL_ARRAY_SIZE * 2);
        if (Status != XST_SUCCESS) {
            xil_printf("Failed to read AMG8833 data, retrying...\n");
            usleep(500000);  // I learned my lesson the first time without delay
            continue;
        }

        // Now we'll convert all 64 pixel readings and store them in the TemperatureArray
        for (int i = 0; i < AMG8833_PIXEL_ARRAY_SIZE; i++) {
            // Combine the two bytes for each pixel into a single 16-bit value
            PixelValue = (int16_t)((PixelData[2 * i + 1] << 8) | PixelData[2 * i]);

            // Store the temperature value in the array (this is in quarter-degree units)
            TemperatureArray[i / 8][i % 8] = PixelValue;
        }

        // Let's print out the full 8x8 grid of temperature data
        xil_printf("8x8 Temperature Grid (in quarter degrees, 1 unit = 0.25°C):\n");
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                xil_printf("%5d ", TemperatureArray[row][col]);  // Print each value nicely spaced
            }
            xil_printf("\n");  // Start a new line after each row
        }

        // Pause for half a second before reading the data again
        usleep(500000);
    }

    // Cleanup and exit
    cleanup_platform();
    return 0;
}
