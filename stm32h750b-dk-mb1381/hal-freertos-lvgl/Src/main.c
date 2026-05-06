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
#include "app_log.h"

/* ---- Framebuffers in AXI SRAM (DMA-accessible, linker section) ------------*/
__attribute__((section(".framebuffers"), aligned(32)))
uint8_t lcd_fb[2][FB_SIZE];

/* ---- Private prototypes ---------------------------------------------------*/
static void SystemClock_Config(void);
static void CPU_CACHE_Enable(void);
static void LED_Init(void);
static void PreLvgl_SmokeTest(uint16_t *fb);
static void FB_Fill(uint16_t *fb, uint16_t color);
static void FB_DrawPixel(uint16_t *fb, int x, int y, uint16_t color);
static void FB_DrawLine(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color);
static void FB_DrawRect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);

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
    APP_LOGI("BOOT", "icache+dcache enabled");

    HAL_Init();
    SystemClock_Config();
    AppLog_Init();
    APP_LOGI("BOOT", "hal init + clock config done");

    /* Bring up QSPI flash in memory-mapped mode before anything in EXTFLASH */
    APP_LOGI("QSPI", "init start");
    QSPI_MemoryMapped_Init();
    APP_LOGI("QSPI", "init done");

    LED_Init();
    HAL_GPIO_WritePin(LED1_GPIO_PORT, LED1_PIN, GPIO_PIN_SET);   /* green on */
    APP_LOGI("BOOT", "leds initialized, green on");

    /* Init LTDC with first framebuffer */
    LTDC_Init((uint32_t)lcd_fb[0]);
    APP_LOGI("LTDC", "init done, fb0=0x%08lx", (unsigned long)(uint32_t)lcd_fb[0]);

    /* Smoke test: draw directly to framebuffer for 10s before LVGL startup */
    APP_LOGI("SMOKE", "pre-LVGL direct draw test start");
    PreLvgl_SmokeTest((uint16_t *)lcd_fb[0]);
    APP_LOGI("SMOKE", "pre-LVGL direct draw test done");

    /* Init LVGL + start LVGL FreeRTOS task */
    lvgl_port_init();
    APP_LOGI("LVGL", "port init done");

    /* Build the UI */
    ui_init();
    APP_LOGI("UI", "ui init done");

    /* Start logger worker task before scheduler */
    AppLog_StartTask(2U, 1024U);
    APP_LOGI("BOOT", "starting scheduler");

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
    AppLog_Panic("Error_Handler entered");
    HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_PIN, GPIO_PIN_SET);
    __disable_irq();
    while (1) {}
}

/* ---- Pre-LVGL direct framebuffer smoke test ------------------------------ */
#define RGB565(r, g, b) (uint16_t)((((r) & 0x1FU) << 11) | (((g) & 0x3FU) << 5) | ((b) & 0x1FU))

static void FB_Fill(uint16_t *fb, uint16_t color)
{
    uint32_t pixels = (uint32_t)LCD_WIDTH * (uint32_t)LCD_HEIGHT;
    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = color;
    }
}

static void FB_DrawPixel(uint16_t *fb, int x, int y, uint16_t color)
{
    if ((x < 0) || (y < 0) || (x >= (int)LCD_WIDTH) || (y >= (int)LCD_HEIGHT)) {
        return;
    }
    fb[(y * (int)LCD_WIDTH) + x] = color;
}

static void FB_DrawLine(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        FB_DrawPixel(fb, x0, y0, color);
        if ((x0 == x1) && (y0 == y1)) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void FB_DrawRect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    if ((w <= 0) || (h <= 0)) {
        return;
    }
    FB_DrawLine(fb, x, y, x + w - 1, y, color);
    FB_DrawLine(fb, x, y + h - 1, x + w - 1, y + h - 1, color);
    FB_DrawLine(fb, x, y, x, y + h - 1, color);
    FB_DrawLine(fb, x + w - 1, y, x + w - 1, y + h - 1, color);
}

static void PreLvgl_SmokeTest(uint16_t *fb)
{
    const uint16_t black = RGB565(0, 0, 0);
    const uint16_t white = RGB565(31, 63, 31);
    const uint16_t red   = RGB565(31, 0, 0);
    const uint16_t green = RGB565(0, 63, 0);
    const uint16_t blue  = RGB565(0, 0, 31);
    const uint16_t cyan  = RGB565(0, 63, 31);
    const uint16_t yellow= RGB565(31, 63, 0);

    /* Phase 1: static geometry and color bars (2 seconds) */
    FB_Fill(fb, black);
    FB_DrawRect(fb, 0, 0, LCD_WIDTH, LCD_HEIGHT, white);
    FB_DrawLine(fb, 0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, red);
    FB_DrawLine(fb, LCD_WIDTH - 1, 0, 0, LCD_HEIGHT - 1, green);
    FB_DrawRect(fb, (LCD_WIDTH / 2) - 40, (LCD_HEIGHT / 2) - 20, 80, 40, yellow);

    for (int y = 10; y < 34; y++) {
        for (int x = 20; x < 120; x++) fb[(y * (int)LCD_WIDTH) + x] = red;
        for (int x = 120; x < 220; x++) fb[(y * (int)LCD_WIDTH) + x] = green;
        for (int x = 220; x < 320; x++) fb[(y * (int)LCD_WIDTH) + x] = blue;
        for (int x = 320; x < 420; x++) fb[(y * (int)LCD_WIDTH) + x] = white;
    }

    SCB_CleanDCache();
    HAL_Delay(2000);

    /* Phase 2: animated vertical bar + frame/crosshair (8 seconds) */
    for (uint32_t frame = 0; frame < 200; frame++) {
        int bar_x = (int)(frame % LCD_WIDTH);

        FB_Fill(fb, black);
        FB_DrawRect(fb, 0, 0, LCD_WIDTH, LCD_HEIGHT, white);
        FB_DrawLine(fb, LCD_WIDTH / 2, 0, LCD_WIDTH / 2, LCD_HEIGHT - 1, yellow);
        FB_DrawLine(fb, 0, LCD_HEIGHT / 2, LCD_WIDTH - 1, LCD_HEIGHT / 2, yellow);

        for (int y = 8; y < (int)LCD_HEIGHT - 8; y++) {
            for (int w = 0; w < 8; w++) {
                FB_DrawPixel(fb, bar_x + w, y, cyan);
            }
        }

        SCB_CleanDCache();
        HAL_Delay(40);
    }
}
