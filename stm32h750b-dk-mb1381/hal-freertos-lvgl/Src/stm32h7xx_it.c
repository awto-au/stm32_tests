#include "stm32h7xx_it.h"
#include "stm32h7xx_hal.h"
#include "main.h"
#include <stdint.h>

/* FreeRTOS exception handlers are mapped via FreeRTOSConfig.h macros:
 *   SVC_Handler    → vPortSVCHandler
 *   PendSV_Handler → xPortPendSVHandler
 *   SysTick_Handler → xPortSysTickHandler
 * so we must NOT define them here. HAL timebase runs from TIM6 instead. */

void NMI_Handler(void) { while(1){} }

/* HardFault: capture stacked context so a debugger can inspect it.
 * If a debugger is not attached the variables are visible in a watch window
 * after halting with JTAG / OpenOCD. */
void HardFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4          \n"
        "ite eq              \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "b HardFault_Decode  \n"
    );
}

void __attribute__((used)) HardFault_Decode(uint32_t *stack)
{
    volatile uint32_t hf_r0   = stack[0];
    volatile uint32_t hf_r1   = stack[1];
    volatile uint32_t hf_r2   = stack[2];
    volatile uint32_t hf_r3   = stack[3];
    volatile uint32_t hf_r12  = stack[4];
    volatile uint32_t hf_lr   = stack[5];
    volatile uint32_t hf_pc   = stack[6];  /* faulting instruction */
    volatile uint32_t hf_xpsr = stack[7];
    volatile uint32_t hf_cfsr = SCB->CFSR;
    volatile uint32_t hf_hfsr = SCB->HFSR;
    volatile uint32_t hf_mmar = SCB->MMFAR;
    volatile uint32_t hf_bfar = SCB->BFAR;
    (void)hf_r0; (void)hf_r1; (void)hf_r2; (void)hf_r3;
    (void)hf_r12; (void)hf_lr; (void)hf_pc; (void)hf_xpsr;
    (void)hf_cfsr; (void)hf_hfsr; (void)hf_mmar; (void)hf_bfar;
    __BKPT(0);   /* break into debugger if attached */
    while(1){}
}

void MemManage_Handler(void)  { __BKPT(0); while(1){} }
void BusFault_Handler(void)   { __BKPT(0); while(1){} }
void UsageFault_Handler(void) { __BKPT(0); while(1){} }

/* LTDC line interrupt — forwarded to HAL for LVGL frame sync */
void LTDC_IRQHandler(void)
{
    HAL_LTDC_IRQHandler(&hltdc);
}
