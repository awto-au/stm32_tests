#include "stm32h7xx_hal.h"

static TIM_HandleTypeDef tim6_handle;

HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
    RCC_ClkInitTypeDef clock_config = {0};
    uint32_t apb1_prescaler;
    uint32_t tim_clock;
    uint32_t tim_prescaler;
    uint32_t flash_latency;

    if (tick_priority >= (1UL << __NVIC_PRIO_BITS)) {
        return HAL_ERROR;
    }

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, tick_priority, 0U);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    uwTickPrio = tick_priority;

    __HAL_RCC_TIM6_CLK_ENABLE();

    HAL_RCC_GetClockConfig(&clock_config, &flash_latency);
    apb1_prescaler = clock_config.APB1CLKDivider;
    if (apb1_prescaler == RCC_HCLK_DIV1) {
        tim_clock = HAL_RCC_GetPCLK1Freq();
    } else {
        tim_clock = 2U * HAL_RCC_GetPCLK1Freq();
    }

    /* Run TIM6 at 1 MHz, then overflow every 1 ms. */
    tim_prescaler = (tim_clock / 1000000U) - 1U;

    tim6_handle.Instance = TIM6;
    tim6_handle.Init.Period = 999U;
    tim6_handle.Init.Prescaler = tim_prescaler;
    tim6_handle.Init.ClockDivision = 0;
    tim6_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    tim6_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&tim6_handle) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_TIM_Base_Start_IT(&tim6_handle);
}

void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&tim6_handle, TIM_IT_UPDATE);
}

void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&tim6_handle, TIM_IT_UPDATE);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        HAL_IncTick();
    }
}

void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&tim6_handle);
}