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

/* LVGL task config */
#define LVGL_TASK_STACK_WORDS  4096   /* 16 KB — fonts + rendering need headroom */
#define LVGL_TASK_PRIORITY     3

static TaskHandle_t lvgl_task_handle;

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms > 10) sleep_ms = 10;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

void lvgl_port_init(void)
{
    lv_init();

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

    /* Start LVGL task */
    xTaskCreate(lvgl_task, "LVGL", LVGL_TASK_STACK_WORDS, NULL,
                LVGL_TASK_PRIORITY, &lvgl_task_handle);
}
