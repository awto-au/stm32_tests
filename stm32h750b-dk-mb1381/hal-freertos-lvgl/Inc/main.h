#ifndef __MAIN_H
#define __MAIN_H

#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

__attribute__((noreturn)) void Error_Handler(void);

/* ---- Board LEDs -----------------------------------------------------------*/
/* LED1 (Green) = PJ2 */
#define LED1_PIN              GPIO_PIN_2
#define LED1_GPIO_PORT        GPIOJ
#define LED1_CLK_ENABLE()     __HAL_RCC_GPIOJ_CLK_ENABLE()

/* LED2 (Red) = PI13 */
#define LED2_PIN              GPIO_PIN_13
#define LED2_GPIO_PORT        GPIOI
#define LED2_CLK_ENABLE()     __HAL_RCC_GPIOI_CLK_ENABLE()

/* ---- Logger UART (Board COM1 / STLINK VCP: USART3 on PB10/PB11) ---------*/
#define LOG_UART_INSTANCE          USART3
#define LOG_UART_CLK_ENABLE()      __HAL_RCC_USART3_CLK_ENABLE()
#define LOG_UART_GPIO_PORT         GPIOB
#define LOG_UART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define LOG_UART_TX_PIN            GPIO_PIN_10
#define LOG_UART_RX_PIN            GPIO_PIN_11
#define LOG_UART_GPIO_AF           GPIO_AF7_USART3
#define LOG_UART_BAUDRATE          2000000U

/* ---- LCD ------------------------------------------------------------------*/
/* 480x272 RGB565 via LTDC */
#define LCD_WIDTH     480U
#define LCD_HEIGHT    272U

/* LCD backlight / display enable */
#define LCD_DISP_PIN          GPIO_PIN_7
#define LCD_DISP_PORT         GPIOD
#define LCD_BL_PIN            GPIO_PIN_0   /* PK0 = LCD_BL */
#define LCD_BL_PORT           GPIOK

/* ---- Framebuffers in AXI SRAM (section .framebuffers) --------------------*/
/* Two RGB565 framebuffers: 480*272*2 = 261,120 bytes each, total ~510 KB */
#define FB_SIZE  (LCD_WIDTH * LCD_HEIGHT * 2U)

/* Debug boot mode: skip UI construction to rule out asset/font pressure. */
#ifndef BOOT_SKIP_UI_ASSETS
#define BOOT_SKIP_UI_ASSETS 0
#endif

/* Debug boot mode: skip LVGL init to isolate QSPI/LTDC/RTOS bring-up. */
#ifndef BOOT_SKIP_LVGL_INIT
#define BOOT_SKIP_LVGL_INIT 0
#endif

extern uint16_t lcd_fb[2][LCD_WIDTH * LCD_HEIGHT];
extern LTDC_HandleTypeDef hltdc;

#endif /* __MAIN_H */
