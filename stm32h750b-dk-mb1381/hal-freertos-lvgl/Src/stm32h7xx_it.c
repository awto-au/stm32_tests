#include "stm32h7xx_it.h"
#include "stm32h7xx_hal.h"
#include "main.h"
#include "app_log.h"
#include <stdint.h>

/* FreeRTOS exception handlers are mapped via FreeRTOSConfig.h macros:
 *   SVC_Handler    → vPortSVCHandler
 *   PendSV_Handler → xPortPendSVHandler
 *   SysTick_Handler → xPortSysTickHandler
 * so we must NOT define them here. HAL timebase runs from TIM6 instead. */

void NMI_Handler(void) { while(1){} }

static const char kHardFaultName[] = "HardFault";
static const char kMemManageName[] = "MemManage";
static const char kBusFaultName[] = "BusFault";
static const char kUsageFaultName[] = "UsageFault";

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
        "mov r1, lr          \n"
        "ldr r2, =kHardFaultName \n"
        "b Fault_Decode_Common    \n"
    );
}

void MemManage_Handler(void)
{
    __asm volatile (
        "tst lr, #4          \n"
        "ite eq              \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "mov r1, lr          \n"
        "ldr r2, =kMemManageName \n"
        "b Fault_Decode_Common    \n"
    );
}

void BusFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4          \n"
        "ite eq              \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "mov r1, lr          \n"
        "ldr r2, =kBusFaultName \n"
        "b Fault_Decode_Common    \n"
    );
}

void UsageFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4          \n"
        "ite eq              \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "mov r1, lr          \n"
        "ldr r2, =kUsageFaultName \n"
        "b Fault_Decode_Common     \n"
    );
}

void __attribute__((used)) Fault_Decode_Common(uint32_t *stack, uint32_t exc_lr, const char *fault_name)
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
    volatile uint32_t hf_dfsr = SCB->DFSR;
    volatile uint32_t hf_afsr = SCB->AFSR;
    volatile uint32_t hf_mmar = SCB->MMFAR;
    volatile uint32_t hf_bfar = SCB->BFAR;
    volatile uint32_t hf_shcsr = SCB->SHCSR;
    volatile uint32_t hf_icsr = SCB->ICSR;
    AppLog_Panic("%s pc=0x%08lx lr=0x%08lx exc_lr=0x%08lx cfsr=0x%08lx hfsr=0x%08lx",
                 fault_name,
                 (unsigned long)hf_pc,
                 (unsigned long)hf_lr,
                 (unsigned long)exc_lr,
                 (unsigned long)hf_cfsr,
                 (unsigned long)hf_hfsr);
    AppLog_Panic("%s regs r0=0x%08lx r1=0x%08lx r2=0x%08lx r3=0x%08lx",
                 fault_name,
                 (unsigned long)hf_r0,
                 (unsigned long)hf_r1,
                 (unsigned long)hf_r2,
                 (unsigned long)hf_r3);
    AppLog_Panic("%s regs r12=0x%08lx sp=%p xpsr=0x%08lx",
                 fault_name,
                 (unsigned long)hf_r12,
                 (void *)stack,
                 (unsigned long)hf_xpsr);
    AppLog_Panic("%s addr mmar=0x%08lx bfar=0x%08lx shcsr=0x%08lx icsr=0x%08lx",
                 fault_name,
                 (unsigned long)hf_mmar,
                 (unsigned long)hf_bfar,
                 (unsigned long)hf_shcsr,
                 (unsigned long)hf_icsr);
    AppLog_Panic("%s status dfsr=0x%08lx afsr=0x%08lx",
                 fault_name,
                 (unsigned long)hf_dfsr,
                 (unsigned long)hf_afsr);

    if ((hf_hfsr & SCB_HFSR_FORCED_Msk) != 0U) {
        AppLog_Panic("Decode: HFSR.FORCED set (escalated configurable fault)");
    }
    if ((hf_hfsr & SCB_HFSR_VECTTBL_Msk) != 0U) {
        AppLog_Panic("Decode: HFSR.VECTTBL set (vector table read fault)");
    }

    if ((hf_cfsr & SCB_CFSR_UNDEFINSTR_Msk) != 0U) {
        AppLog_Panic("Decode: UFSR.UNDEFINSTR (undefined instruction)");
    }
    if ((hf_cfsr & SCB_CFSR_INVSTATE_Msk) != 0U) {
        AppLog_Panic("Decode: UFSR.INVSTATE (invalid EPSR state)");
    }
    if ((hf_cfsr & SCB_CFSR_INVPC_Msk) != 0U) {
        AppLog_Panic("Decode: UFSR.INVPC (invalid EXC_RETURN / PC)");
    }
    if ((hf_cfsr & SCB_CFSR_NOCP_Msk) != 0U) {
        AppLog_Panic("Decode: UFSR.NOCP (coprocessor disabled)");
    }
    if ((hf_cfsr & SCB_CFSR_UNALIGNED_Msk) != 0U) {
        AppLog_Panic("Decode: UFSR.UNALIGNED");
    }
    if ((hf_cfsr & SCB_CFSR_DIVBYZERO_Msk) != 0U) {
        AppLog_Panic("Decode: UFSR.DIVBYZERO");
    }
    if ((hf_cfsr & SCB_CFSR_IBUSERR_Msk) != 0U) {
        AppLog_Panic("Decode: BFSR.IBUSERR (instruction bus error)");
    }
    if ((hf_cfsr & SCB_CFSR_PRECISERR_Msk) != 0U) {
        AppLog_Panic("Decode: BFSR.PRECISERR (precise data bus error)");
    }
    if ((hf_cfsr & SCB_CFSR_IMPRECISERR_Msk) != 0U) {
        AppLog_Panic("Decode: BFSR.IMPRECISERR (imprecise data bus error)");
    }
    if ((hf_cfsr & SCB_CFSR_BFARVALID_Msk) != 0U) {
        AppLog_Panic("Decode: BFSR.BFARVALID bfar=0x%08lx", (unsigned long)hf_bfar);
    }
    if ((hf_cfsr & SCB_CFSR_IACCVIOL_Msk) != 0U) {
        AppLog_Panic("Decode: MMFSR.IACCVIOL (instruction access violation)");
    }
    if ((hf_cfsr & SCB_CFSR_DACCVIOL_Msk) != 0U) {
        AppLog_Panic("Decode: MMFSR.DACCVIOL (data access violation)");
    }
    if ((hf_cfsr & SCB_CFSR_MMARVALID_Msk) != 0U) {
        AppLog_Panic("Decode: MMFSR.MMARVALID mmar=0x%08lx", (unsigned long)hf_mmar);
    }

    (void)hf_r0; (void)hf_r1; (void)hf_r2; (void)hf_r3;
    (void)hf_r12; (void)hf_lr; (void)hf_pc; (void)hf_xpsr;
    (void)hf_cfsr; (void)hf_hfsr; (void)hf_dfsr; (void)hf_afsr;
    (void)hf_mmar; (void)hf_bfar; (void)hf_shcsr; (void)hf_icsr; (void)exc_lr; (void)fault_name;
    __BKPT(0);   /* break into debugger if attached */
    while(1){}
}

/* LTDC line interrupt — forwarded to HAL for LVGL frame sync */
void LTDC_IRQHandler(void)
{
    HAL_LTDC_IRQHandler(&hltdc);
}
