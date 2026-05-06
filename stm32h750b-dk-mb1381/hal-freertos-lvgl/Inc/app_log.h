#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

void AppLog_Init(void);
void AppLog_StartTask(UBaseType_t priority, uint16_t stack_words);
void AppLog_Write(char level, const char *module, const char *fmt, ...);
void AppLog_Panic(const char *fmt, ...);

#define APP_LOGI(module, fmt, ...) AppLog_Write('I', module, fmt, ##__VA_ARGS__)
#define APP_LOGW(module, fmt, ...) AppLog_Write('W', module, fmt, ##__VA_ARGS__)
#define APP_LOGE(module, fmt, ...) AppLog_Write('E', module, fmt, ##__VA_ARGS__)

#endif /* APP_LOG_H */
