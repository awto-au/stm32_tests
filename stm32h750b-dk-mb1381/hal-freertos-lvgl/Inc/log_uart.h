#ifndef LOG_UART_H
#define LOG_UART_H

#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

extern UART_HandleTypeDef hlog_uart;

void LogUart_Init(void);
void LogUart_WriteBlocking(const uint8_t *data, size_t len);

#endif /* LOG_UART_H */
