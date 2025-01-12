#include "xparameters.h"
#include "xtmrctr.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include <stdio.h>
#include "platform.h"
#include "sleep.h"

#define SCUGIC_DEVICE_ID        XPAR_SCUGIC_SINGLE_DEVICE_ID
#define AXI_TIMER_DEVICE_ID_0   XPAR_AXI_TIMER_0_DEVICE_ID
#define AXI_TIMER_DEVICE_ID_1   XPAR_AXI_TIMER_1_DEVICE_ID
#define AXI_TIMER_IRPT_INTR     XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR

#define AXI_TIMER_CHANNEL       0

#define AXI_TIMER_PWM_TEST_SW   1

#define AXI_TIMER_PERIOD_US         10000
#define AXI_TIMER_PWM_HIGH_TIME_US  2000   // 20% duty cycle

XTmrCtr xTmrCtr_Inst0;
XTmrCtr xTmrCtr_Inst1;
XScuGic xScuGic_Inst;

void xTmrCtr_Int_Handler(void *CallBackRef, u8 TmrCtrNumber)
{
    // Interrupt handler code (if needed)
}

u32 xTmr_US_To_RegValue(u32 US)
{
    u32 Value;
    Value = 50 * US;  // Assuming timer clock is 50 MHz
    return 0xFFFFFFFF - Value;
}

u32 xTmr_US_To_NS(u32 US)
{
    return US * 1000;
}

int xTmrCtr_Init(XTmrCtr *xTmrCtr_Ptr, u32 DeviceId)
{
    int Status, DutyCycle;

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

    // Set the period for the timer
    XTmrCtr_SetResetValue(xTmrCtr_Ptr, AXI_TIMER_CHANNEL, xTmr_US_To_RegValue(AXI_TIMER_PERIOD_US));

    // Set the options for the timer
    XTmrCtr_SetOptions(xTmrCtr_Ptr, AXI_TIMER_CHANNEL, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);

#if AXI_TIMER_PWM_TEST_SW
    // Configure the PWM with the specified period and high time
    DutyCycle = XTmrCtr_PwmConfigure(xTmrCtr_Ptr, xTmr_US_To_NS(AXI_TIMER_PERIOD_US), xTmr_US_To_NS(AXI_TIMER_PWM_HIGH_TIME_US));
    xil_printf("AXI Timer PWM DutyCycle: %d%%\n", DutyCycle);
#endif
    return XST_SUCCESS;
}

int main()
{
    int Status;

    xil_printf("AXI Timer PWM Test!\n");

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

    // Main loop (empty since PWM runs independently)
    while (1)
    {
        sleep(1);
    }

    return 0;
}
