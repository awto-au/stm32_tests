#ifndef __STM32H7xx_IT_H
#define __STM32H7xx_IT_H

#include <stdint.h>

void NMI_Handler(void);
void HardFault_Handler(void);
void HardFault_Decode(uint32_t *stack);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void TIM6_DAC_IRQHandler(void);
/* SVC_Handler, PendSV_Handler, SysTick_Handler are mapped to FreeRTOS in FreeRTOSConfig.h */
void LTDC_IRQHandler(void);

#endif /* __STM32H7xx_IT_H */
