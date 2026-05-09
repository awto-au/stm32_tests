#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "log_uart.h"
#include "stm32h7xx.h"  /* DWT->CYCCNT, CoreDebug */

#define APP_LOG_LINE_MAX      160U
#define APP_LOG_QUEUE_DEPTH    24U

typedef struct {
    uint16_t len;
    char text[APP_LOG_LINE_MAX];
} AppLogLine_t;

static StaticQueue_t log_queue_ctrl;
__attribute__((section(".logbuf"), aligned(32)))
static uint8_t log_queue_storage[APP_LOG_QUEUE_DEPTH * sizeof(AppLogLine_t)];
static QueueHandle_t log_queue;
static TaskHandle_t log_task_handle;

static void AppLog_Task(void *arg)
{
    (void)arg;
    AppLogLine_t line;

    for (;;) {
        if (xQueueReceive(log_queue, &line, portMAX_DELAY) == pdPASS) {
            LogUart_WriteBlocking((const uint8_t *)line.text, (size_t)line.len);
        }
    }
}

void AppLog_Init(void)
{
    if (log_queue != NULL) {
        return;
    }

    LogUart_Init();

    log_queue = xQueueCreateStatic(
        APP_LOG_QUEUE_DEPTH,
        sizeof(AppLogLine_t),
        log_queue_storage,
        &log_queue_ctrl
    );

    APP_LOGI("BOOT", "logger init done");
}

void AppLog_StartTask(UBaseType_t priority, uint16_t stack_words)
{
    if ((log_queue == NULL) || (log_task_handle != NULL)) {
        return;
    }

    (void)xTaskCreate(AppLog_Task, "LOG", stack_words, NULL, priority, &log_task_handle);
    APP_LOGI("BOOT", "logger task started prio=%lu", (unsigned long)priority);
}

void AppLog_Write(char level, const char *module, const char *fmt, ...)
{
    AppLogLine_t line;
    uint32_t ts;
    const char *task_name = "BOOT";
    int pos = 0;
    va_list ap;

    if (module == NULL) {
        module = "GEN";
    }

    /* Raw DWT cycle count — 400 MHz, 32-bit, ~10.7 s wrap.  No division,
     * no truncation: highest resolution with zero runtime cost. */
    ts = DWT->CYCCNT;
    if ((xPortIsInsideInterrupt() == pdFALSE) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) {
        task_name = pcTaskGetName(NULL);
    }

    pos = snprintf(line.text, sizeof(line.text), "[%c][%lu][%s][%s] ",
                   level, (unsigned long)ts, task_name, module);
    if (pos < 0) {
        return;
    }

    va_start(ap, fmt);
    pos += vsnprintf(&line.text[pos], sizeof(line.text) - (size_t)pos, fmt, ap);
    va_end(ap);

    if (pos < 0) {
        return;
    }

    if ((size_t)pos > (sizeof(line.text) - 3U)) {
        pos = (int)(sizeof(line.text) - 3U);
    }
    line.text[pos++] = '\r';
    line.text[pos++] = '\n';
    line.text[pos] = '\0';
    line.len = (uint16_t)pos;

    if ((xPortIsInsideInterrupt() == pdTRUE) ||
        (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) ||
        (log_task_handle == NULL)) {
        LogUart_WriteBlocking((const uint8_t *)line.text, (size_t)line.len);
        return;
    }

    if (xQueueSend(log_queue, &line, 0U) != pdPASS) {
        static const char drop_msg[] = "[W][        0][LOG][DROP] queue full\r\n";
        LogUart_WriteBlocking((const uint8_t *)drop_msg, sizeof(drop_msg) - 1U);
    }
}

void AppLog_Panic(const char *fmt, ...)
{
    char buf[APP_LOG_LINE_MAX];
    int pos = 0;
    va_list ap;

    pos = snprintf(buf, sizeof(buf), "[E][PANIC] ");
    if (pos < 0) {
        return;
    }

    va_start(ap, fmt);
    pos += vsnprintf(&buf[pos], sizeof(buf) - (size_t)pos, fmt, ap);
    va_end(ap);

    if (pos < 0) {
        return;
    }

    if ((size_t)pos > (sizeof(buf) - 3U)) {
        pos = (int)(sizeof(buf) - 3U);
    }
    buf[pos++] = '\r';
    buf[pos++] = '\n';
    buf[pos] = '\0';

    LogUart_WriteBlocking((const uint8_t *)buf, (size_t)pos);
}
