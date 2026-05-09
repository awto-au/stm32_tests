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
#include "can_init.h"
#include "app_log.h"

#define XIP_ASM_RETURN_MAGIC 0x2AU

/* ---- Framebuffers in AXI SRAM (DMA-accessible, linker section) ------------*/
__attribute__((section(".framebuffers"), aligned(32)))
uint16_t lcd_fb[2][LCD_WIDTH * LCD_HEIGHT];

#define RUN_PRELVGL_SMOKE_TEST 1

/* ---- Private prototypes ---------------------------------------------------*/
static void SystemClock_Config(void);
static void FPU_EnableEarly(void);
static void CPU_CACHE_Enable(void);
static void LED_Init(void);
static void EarlyAliveBlink(void);
static void HeartbeatTask(void *arg);
static void StartupTask(void *arg);

__attribute__((naked, noinline, section(".extflash.text")))
static uint32_t XipAsmReturnMagic(void)
{
    __asm volatile (
        "movs r0, %0\n"
        "bx lr\n"
        :
        : "I" (XIP_ASM_RETURN_MAGIC)
        : "r0"
    );
}

#if RUN_PRELVGL_SMOKE_TEST
static void PreLvgl_SmokeTest(uint16_t *fb);
static void FB_Fill(uint16_t *fb, uint16_t color);
static void FB_DrawPixel(uint16_t *fb, int x, int y, uint16_t color);
static void FB_DrawLine(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color);
static void FB_DrawRect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);
#endif

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
    FPU_EnableEarly();

    /* Enable DWT cycle counter — used for µs-resolution log timestamps.
     * Must be done before AppLog_Init so pre-scheduler messages get
     * meaningful timestamps.  Counter is 32-bit @ 400 MHz (~10.7 s wrap). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT       = 0U;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    CPU_CACHE_Enable();

    HAL_Init();
    SystemClock_Config();

    LED_Init();
    EarlyAliveBlink();

    /* Bring debug UART up as early as possible (before heavy peripheral init). */
    AppLog_Init();
    APP_LOGI("BOOT", "icache+dcache enabled");
    APP_LOGI("BOOT", "hal init + clock config done");
    APP_LOGI("BOOT", "early alive blink done");

    /* Start logger worker before scheduler to make task logs non-blocking. */
    AppLog_StartTask(3U, 1024U);

    /* RTOS heartbeat task: LED flashing should be scheduler-driven. */
    if (xTaskCreate(HeartbeatTask, "HB", 256U, NULL, 4U, NULL) != pdPASS) {
        AppLog_Panic("Heartbeat task create failed");
        Error_Handler();
    }

    /* Heavy board/display init runs under RTOS control. */
    if (xTaskCreate(StartupTask, "START", 2048U, NULL, 1U, NULL) != pdPASS) {
        AppLog_Panic("Startup task create failed");
        Error_Handler();
    }

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

static void FPU_EnableEarly(void)
{
    /* Force-enable CP10/CP11 for all privilege levels.
     * This guards against NOCP faults if CMSIS __FPU_USED gating is not active
     * in SystemInit for this build configuration. */
    SCB->CPACR |= (0xFUL << 20);
    __DSB();
    __ISB();

    /* Enable automatic/lazy FP context save for RTOS task switches. */
    FPU->FPCCR |= (FPU_FPCCR_ASPEN_Msk | FPU_FPCCR_LSPEN_Msk);
    __DSB();
    __ISB();
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

__attribute__((noreturn)) void Error_Handler(void)
{
    /* Light red LED, halt */
    AppLog_Panic("Error_Handler entered");
    HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_PIN, GPIO_PIN_SET);
    __disable_irq();
    while (1) {}
}

static void HeartbeatTask(void *arg)
{
    (void)arg;

    for (;;) {
        HAL_GPIO_TogglePin(LED1_GPIO_PORT, LED1_PIN);
        HAL_GPIO_TogglePin(LED2_GPIO_PORT, LED2_PIN);
        vTaskDelay(pdMS_TO_TICKS(500U));
    }
}

static void StartupTask(void *arg)
{
    (void)arg;

    APP_LOGI("BOOT", "startup task begin");

    APP_LOGI("QSPI", "init start");
    QSPI_MemoryMapped_Init();
    APP_LOGI("QSPI", "init done (caches should be OFF for this proof)");

    /* XIP gate test per doc section C - call tiny function from 0x90000000 */
    APP_LOGI("XIP", "--- XIP gate test ---");
    APP_LOGI("XIP", "calling asm fn @0x%08lx (Thumb, bit 0 set)",
             (unsigned long)((uint32_t)XipAsmReturnMagic | 1U));
    
    uint32_t xip_ret = XipAsmReturnMagic();
    
    APP_LOGI("XIP", "XIP call returned 0x%08lx", (unsigned long)xip_ret);
    if (xip_ret != XIP_ASM_RETURN_MAGIC) {
        APP_LOGI("XIP", "FAIL: expected 0x%02lx got 0x%08lx", (unsigned long)XIP_ASM_RETURN_MAGIC, (unsigned long)xip_ret);
        Error_Handler();
    } else {
        APP_LOGI("XIP", "SUCCESS: tiny XIP function worked!");
    }

    APP_LOGI("QBENCH", "running aggressive pre-LVGL benchmark");
    QSPI_RunAggressiveBenchmark();
    APP_LOGI("QBENCH", "benchmark complete, proceeding to LTDC/LVGL");

    if (!CAN_BringupAndSelfTest()) {
        APP_LOGE("CAN", "bring-up failed");
        Error_Handler();
    }

    /* LVGL does unaligned accesses in some fast paths; don't trap them in bring-up. */
    SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
    __DSB();
    __ISB();

    /* Re-enable caches disabled by QSPI proof phase.  Both must be on:
     * I-cache for XIP instruction fetch, D-cache for XIP rodata + AXI SRAM.
     * The QSPI MPU region is cacheable+WT so reads are served from cache after
     * the first miss — otherwise every LDR to 0x90000000 is a QSPI transaction. */
    SCB_EnableICache();
    SCB_EnableDCache();
    __DSB();
    __ISB();
    APP_LOGI("BOOT", "pre-LVGL core cfg: icache=ON dcache=ON unalign-trap=OFF");

    /* Measure cached XIP throughput now that both caches are hot. */
    QSPI_BenchmarkCachedRead();

    LTDC_Init((uint32_t)lcd_fb[0]);
    APP_LOGI("LTDC", "init done, fb0=0x%08lx", (unsigned long)(uint32_t)lcd_fb[0]);

#if RUN_PRELVGL_SMOKE_TEST
    APP_LOGI("SMOKE", "pre-LVGL direct draw test start");
    PreLvgl_SmokeTest((uint16_t *)lcd_fb[0]);
    APP_LOGI("SMOKE", "pre-LVGL direct draw test done");
#endif

#if BOOT_SKIP_LVGL_INIT
    APP_LOGI("LVGL", "lvgl_port_init skipped (BOOT_SKIP_LVGL_INIT=1)");
    APP_LOGI("UI", "ui_init skipped (BOOT_SKIP_LVGL_INIT=1)");
#else
    APP_LOGI("BOOT", "before lvgl_port_init");
    lvgl_port_init();
    APP_LOGI("BOOT", "after lvgl_port_init");
    APP_LOGI("LVGL", "port init done");

    APP_LOGI("BOOT", "before ui_init");
#if BOOT_SKIP_UI_ASSETS
    APP_LOGI("UI", "ui_init skipped (BOOT_SKIP_UI_ASSETS=1)");
#else
    ui_init();
    APP_LOGI("BOOT", "after ui_init");
    APP_LOGI("UI", "ui init done");
#endif
#endif

    APP_LOGI("BOOT", "startup task complete");
    vTaskDelete(NULL);
}

static void EarlyAliveBlink(void)
{
    /* Drive both LEDs with explicit set/reset phases to expose polarity/pin issues. */
    for (uint32_t i = 0; i < 8U; i++) {
        HAL_GPIO_WritePin(LED1_GPIO_PORT, LED1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_PIN, GPIO_PIN_RESET);
        HAL_Delay(120U);

        HAL_GPIO_WritePin(LED1_GPIO_PORT, LED1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_PIN, GPIO_PIN_SET);
        HAL_Delay(120U);
    }
}

/* ---- Pre-LVGL direct framebuffer smoke test ------------------------------ */
#define RGB565(r, g, b) (uint16_t)((((r) & 0x1FU) << 11) | (((g) & 0x3FU) << 5) | ((b) & 0x1FU))

#if RUN_PRELVGL_SMOKE_TEST

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

    /* Quick static sanity frame (no blocking hold). */
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

    /* Keep animated phase brief so total smoke test is about one second. */
    for (uint32_t frame = 0; frame < 16; frame++) {
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
        HAL_Delay(10);
    }
}

#endif
