#include "log_uart.h"
#include "main.h"

UART_HandleTypeDef hlog_uart;

void LogUart_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    LOG_UART_GPIO_CLK_ENABLE();
    LOG_UART_CLK_ENABLE();

    gpio.Pin = LOG_UART_TX_PIN | LOG_UART_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = LOG_UART_GPIO_AF;
    HAL_GPIO_Init(LOG_UART_GPIO_PORT, &gpio);

    hlog_uart.Instance = LOG_UART_INSTANCE;
    hlog_uart.Init.BaudRate = 115200;
    hlog_uart.Init.WordLength = UART_WORDLENGTH_8B;
    hlog_uart.Init.StopBits = UART_STOPBITS_1;
    hlog_uart.Init.Parity = UART_PARITY_NONE;
    hlog_uart.Init.Mode = UART_MODE_TX_RX;
    hlog_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    hlog_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    hlog_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    hlog_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    hlog_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&hlog_uart) != HAL_OK) {
        Error_Handler();
    }

    HAL_UARTEx_SetTxFifoThreshold(&hlog_uart, UART_TXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_SetRxFifoThreshold(&hlog_uart, UART_RXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_DisableFifoMode(&hlog_uart);
}

void LogUart_WriteBlocking(const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }
    (void)HAL_UART_Transmit(&hlog_uart, (uint8_t *)data, (uint16_t)len, HAL_MAX_DELAY);
}

int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    LogUart_WriteBlocking(&c, 1U);
    return ch;
}

int __io_getchar(void)
{
    uint8_t c = 0;
    if (HAL_UART_Receive(&hlog_uart, &c, 1U, HAL_MAX_DELAY) == HAL_OK) {
        return (int)c;
    }
    return -1;
}
