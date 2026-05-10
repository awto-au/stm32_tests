/**
 * lvgl_port.c — LVGL initialisation and display registration for STM32H750B-DK
 *
 * Uses lv_st_ltdc_create_direct() — LVGL writes directly into one of the two
 * AXI SRAM framebuffers; LTDC reads from the other.  No intermediate render
 * buffer needed.  A FreeRTOS task drives lv_timer_handler().
 */

#include "main.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "app_log.h"
#include "ui.h"
#include <string.h>

/* LVGL task config */
#define LVGL_TASK_STACK_WORDS  4096   /* 16 KB — fonts + rendering need headroom */
#define LVGL_TASK_PRIORITY     3

static TaskHandle_t lvgl_task_handle;

#if LV_USE_LOG
#define LVGL_LOG_CONTEXT_LINES 64U
#define LVGL_LOG_LINE_MAX      160U

typedef struct {
    char level;
    char text[LVGL_LOG_LINE_MAX];
} LvglLogLine_t;

__attribute__((section(".logbuf"), aligned(32)))
static LvglLogLine_t lvgl_log_context[LVGL_LOG_CONTEXT_LINES];
static uint16_t lvgl_log_context_head;
static uint16_t lvgl_log_context_count;

static void lvgl_log_context_push(char level, const char *buf)
{
    size_t len;
    LvglLogLine_t *slot;

    if (buf == NULL) {
        return;
    }

    slot = &lvgl_log_context[lvgl_log_context_head];
    len = strnlen(buf, LVGL_LOG_LINE_MAX - 1U);
    memcpy(slot->text, buf, len);
    slot->text[len] = '\0';
    slot->level = level;

    lvgl_log_context_head = (uint16_t)((lvgl_log_context_head + 1U) % LVGL_LOG_CONTEXT_LINES);
    if (lvgl_log_context_count < LVGL_LOG_CONTEXT_LINES) {
        lvgl_log_context_count++;
    }
}

static void lvgl_log_context_dump(void)
{
    uint16_t start;
    uint16_t i;

    if (lvgl_log_context_count == 0U) {
        return;
    }

    APP_LOGE("LVLOG", "error detected, dumping %u buffered LVGL lines",
             (unsigned)lvgl_log_context_count);

    start = (uint16_t)((lvgl_log_context_head + LVGL_LOG_CONTEXT_LINES - lvgl_log_context_count)
                       % LVGL_LOG_CONTEXT_LINES);

    for (i = 0U; i < lvgl_log_context_count; i++) {
        uint16_t idx = (uint16_t)((start + i) % LVGL_LOG_CONTEXT_LINES);
        AppLog_Write(lvgl_log_context[idx].level, "LVCTX", "%s", lvgl_log_context[idx].text);
    }

    lvgl_log_context_count = 0U;
}

static void lvgl_log_cb(lv_log_level_t level, const char *buf)
{
    char lvl = 'I';

    if (level == LV_LOG_LEVEL_TRACE) {
        lvl = 'T';
    } else if (level == LV_LOG_LEVEL_WARN) {
        lvl = 'W';
    } else if (level >= LV_LOG_LEVEL_ERROR) {
        lvl = 'E';
    }

    if ((level == LV_LOG_LEVEL_TRACE) || (level == LV_LOG_LEVEL_INFO)) {
        lvgl_log_context_push(lvl, buf);
        return;
    }

    if (level >= LV_LOG_LEVEL_ERROR) {
        lvgl_log_context_dump();
    }

    AppLog_Write(lvl, "LVLOG", "%s", buf);
}
#endif

static void lvgl_task(void *arg)
{
    (void)arg;
    TickType_t last_ticks = xTaskGetTickCount();
    uint32_t frame_count = 0U;
    APP_LOGI("LVGL", "task started");
    while (1) {
        TickType_t now_ticks = xTaskGetTickCount();
        uint32_t elapsed_ms = (uint32_t)((now_ticks - last_ticks) * portTICK_PERIOD_MS);
        if (elapsed_ms > 0U) {
            lv_tick_inc(elapsed_ms);
            last_ticks = now_ticks;
        }

        uint32_t t0 = DWT->CYCCNT;
        uint32_t sleep_ms = lv_timer_handler();
        uint32_t render_cyc = DWT->CYCCNT - t0;

        frame_count++;
        if ((frame_count % 60U) == 0U) {
            APP_LOGI("LVGL", "render_ms=%lu sleep_ms=%lu",
                     (unsigned long)(render_cyc / 400000UL),
                     (unsigned long)sleep_ms);
        }

        ui_stress_tick();
        /* Yield briefly to peer-priority tasks (AppLog).  Don't sleep
         * longer than 5 ms — the vsync reload is the real pacing mechanism. */
        if (sleep_ms > 5U) sleep_ms = 5U;
        if (sleep_ms > 0U) vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

void lvgl_port_init(void)
{
    /* Enable DMA2D clock and set IRQ priority before lv_init() initializes
     * the DMA2D draw unit.  LVGL enables the IRQ but does not set priority,
     * so priority must be set first to avoid FreeRTOS assertion. */
    __HAL_RCC_DMA2D_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA2D_IRQn, 5, 0);

    lv_init();
    APP_LOGI("LVGL", "lv_init done");

#if LV_USE_LOG
    lv_log_register_print_cb(lvgl_log_cb);
    APP_LOGI("LVGL", "lv_log callback registered");
#endif

    /* Register the LTDC display: direct double-buffer mode.
     * lcd_fb[0] = first framebuffer address (LTDC initially shows this one),
     * lcd_fb[1] = second framebuffer address.
     * Layer index 0. */
    lv_display_t *disp = lv_st_ltdc_create_direct(
        (void *)lcd_fb[0],
        (void *)lcd_fb[1],
        0   /* LTDC layer index */
    );

    lv_display_set_resolution(disp, LCD_WIDTH, LCD_HEIGHT);
    APP_LOGI("LVGL", "display configured %ux%u", (unsigned)LCD_WIDTH, (unsigned)LCD_HEIGHT);

}

void lvgl_port_start(void)
{
    if (xTaskCreate(lvgl_task, "LVGL", LVGL_TASK_STACK_WORDS, NULL,
                    LVGL_TASK_PRIORITY, &lvgl_task_handle) != pdPASS) {
        APP_LOGE("LVGL", "task create failed");
        Error_Handler();
    }
}
