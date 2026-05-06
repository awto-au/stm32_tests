/**
 * main.c — STM32H750B-DK LVGL display demo
 *
 * Flow:
 *  1. CPU cache enable, HAL init, 400 MHz system clock
 *  2. LTDC init (framebuffers in AXI SRAM at .framebuffers section)
 *  3. LVGL init + display registration (FreeRTOS task drives lv_timer_handler)
 *  4. UI creation
 *  5. FreeRTOS scheduler start
 */

#include "main.h"
#include "ltdc_init.h"
#include "lvgl_port.h"
#include "ui.h"
#include "qspi_init.h"

/* ---- Framebuffers in AXI SRAM (DMA-accessible, linker section) ------------*/
__attribute__((section(".framebuffers"), aligned(32)))
uint8_t lcd_fb[2][FB_SIZE];

/* ---- Private prototypes ---------------------------------------------------*/
static void SystemClock_Config(void);
static void CPU_CACHE_Enable(void);
static void LED_Init(void);

/* ---- FreeRTOS hooks -------------------------------------------------------*/
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    Error_Handler();
}

void vApplicationMallocFailedHook(void)
{
    Error_Handler();
}

/* ---- Main -----------------------------------------------------------------*/
int main(void)
{
    CPU_CACHE_Enable();

    HAL_Init();
    SystemClock_Config();

    /* Bring up QSPI flash in memory-mapped mode before anything in EXTFLASH */
    QSPI_MemoryMapped_Init();

    LED_Init();
    HAL_GPIO_WritePin(LED1_GPIO_PORT, LED1_PIN, GPIO_PIN_SET);   /* green on */

    /* Init LTDC with first framebuffer */
    LTDC_Init((uint32_t)lcd_fb[0]);

    /* Init LVGL + start LVGL FreeRTOS task */
    lvgl_port_init();

    /* Build the UI */
    ui_init();

    /* Start scheduler — never returns */
    vTaskStartScheduler();

    /* Should never reach here */
    Error_Handler();
    return 0;
}

/* ---- System clock: HSE→PLL1 @ 400 MHz ------------------------------------*/
static void SystemClock_Config(void)
{
    RCC_ClkInitTypeDef  clk  = {0};
    RCC_OscInitTypeDef  osc  = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState            = RCC_HSE_ON;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM            = 5;
    osc.PLL.PLLN            = 160;
    osc.PLL.PLLFRACN        = 0;
    osc.PLL.PLLP            = 2;
    osc.PLL.PLLQ            = 4;
    osc.PLL.PLLR            = 2;
    osc.PLL.PLLVCOSEL       = RCC_PLL1VCOWIDE;
    osc.PLL.PLLRGE          = RCC_PLL1VCIRANGE_2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                         RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                         RCC_CLOCKTYPE_PCLK2  | RCC_CLOCKTYPE_D3PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider  = RCC_HCLK_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
}

static void CPU_CACHE_Enable(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();
}

static void LED_Init(void)
{
    GPIO_InitTypeDef g = {0};

    LED1_CLK_ENABLE();
    g.Pin   = LED1_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED1_GPIO_PORT, &g);
    HAL_GPIO_WritePin(LED1_GPIO_PORT, LED1_PIN, GPIO_PIN_RESET);

    LED2_CLK_ENABLE();
    g.Pin = LED2_PIN;
    HAL_GPIO_Init(LED2_GPIO_PORT, &g);
    HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_PIN, GPIO_PIN_RESET);
}

void Error_Handler(void)
{
    /* Light red LED, halt */
    HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_PIN, GPIO_PIN_SET);
    __disable_irq();
    while (1) {}
}
