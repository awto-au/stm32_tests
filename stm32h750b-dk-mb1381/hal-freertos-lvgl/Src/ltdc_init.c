/**
 * ltdc_init.c — STM32H750B-DK LTDC initialisation for 480×272 RGB565 LCD
 *
 * Panel: RK043FN48H (or compatible) — same timing as used in ST examples.
 * Clock: LTDC pixel clock ≈ 9 MHz from PLLSAI2 (derived from HSE=25 MHz).
 *
 * GPIO assignments (from UM2488 Table 11):
 *  LCD_CLK   = PI14  AF14
 *  LCD_HSYNC = PI12  AF14
 *  LCD_VSYNC = PI9   AF14
 *  LCD_DE    = PK7   AF14
 *  LCD_R0..R7 = PI15, PJ0..1, PH9, PJ3..6
 *  LCD_G0..G7 = PJ7..11, PI0..1, PK2
 *  LCD_B0..B7 = PJ12..15, PK3..6
 *  LCD_DISP  = PD7   (GPIO out, active high)
 *  LCD_BL    = PK0   (GPIO out, active high)
 */

#include "main.h"
#include "ltdc_init.h"

LTDC_HandleTypeDef hltdc;

/* ---- LCD panel timing (RK043FN48H) ----------------------------------------*/
#define HSYNC_W   41U
#define HBP       13U
#define ACTIVE_W  LCD_WIDTH
#define HFP       32U

#define VSYNC_H    10U
#define VBP         2U
#define ACTIVE_H   LCD_HEIGHT
#define VFP         2U

static void LCD_GPIO_Init(void);
static void LCD_ClockConfig(void);
static void LCD_ResetPulse(void);

void LTDC_Init(uint32_t fb_addr)
{
    LTDC_LayerCfgTypeDef layer = {0};

    LCD_GPIO_Init();
    LCD_ResetPulse();
    LCD_ClockConfig();

    /* --- LTDC base ------------------------------------------------------- */
    hltdc.Instance = LTDC;

    hltdc.Init.HSPolarity         = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity         = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity         = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity         = LTDC_PCPOLARITY_IPC;

    hltdc.Init.HorizontalSync     = HSYNC_W - 1;
    hltdc.Init.VerticalSync       = VSYNC_H - 1;
    hltdc.Init.AccumulatedHBP     = HSYNC_W + HBP - 1;
    hltdc.Init.AccumulatedVBP     = VSYNC_H + VBP - 1;
    hltdc.Init.AccumulatedActiveW = HSYNC_W + HBP + ACTIVE_W - 1;
    hltdc.Init.AccumulatedActiveH = VSYNC_H + VBP + ACTIVE_H - 1;
    hltdc.Init.TotalWidth         = HSYNC_W + HBP + ACTIVE_W + HFP - 1;
    hltdc.Init.TotalHeigh         = VSYNC_H + VBP + ACTIVE_H + VFP - 1;

    hltdc.Init.Backcolor.Red   = 0;
    hltdc.Init.Backcolor.Green = 0;
    hltdc.Init.Backcolor.Blue  = 0;

    if (HAL_LTDC_Init(&hltdc) != HAL_OK) {
        Error_Handler();
    }

    /* --- Layer 0: RGB565 full-screen --------------------------------------- */
    layer.WindowX0 = 0;
    layer.WindowX1 = LCD_WIDTH;
    layer.WindowY0 = 0;
    layer.WindowY1 = LCD_HEIGHT;
    layer.PixelFormat         = LTDC_PIXEL_FORMAT_RGB565;
    layer.Alpha               = 255;
    layer.Alpha0              = 0;
    layer.BlendingFactor1     = LTDC_BLENDING_FACTOR1_CA;
    layer.BlendingFactor2     = LTDC_BLENDING_FACTOR2_CA;
    layer.FBStartAdress       = fb_addr;
    layer.ImageWidth          = LCD_WIDTH;
    layer.ImageHeight         = LCD_HEIGHT;
    layer.Backcolor.Red       = 0;
    layer.Backcolor.Green     = 0;
    layer.Backcolor.Blue      = 0;

    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0) != HAL_OK) {
        Error_Handler();
    }

    /* Enable LTDC line interrupt (used by LVGL driver for frame sync) */
    HAL_NVIC_SetPriority(LTDC_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);

    /* Turn on display and backlight */
    HAL_GPIO_WritePin(LCD_DISP_PORT, LCD_DISP_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_BL_PORT,   LCD_BL_PIN,   GPIO_PIN_SET);
}

static void LCD_ResetPulse(void)
{
    /* Use DISP as a hard panel gate to force a known startup state. */
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DISP_PORT, LCD_DISP_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(LCD_DISP_PORT, LCD_DISP_PIN, GPIO_PIN_SET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(LCD_DISP_PORT, LCD_DISP_PIN, GPIO_PIN_RESET);
    HAL_Delay(5U);
}

static void LCD_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOJ_CLK_ENABLE();
    __HAL_RCC_GPIOK_CLK_ENABLE();

    /* Common AF14 LTDC settings */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF14_LTDC;

    /* PI: CLK(14) HSYNC(12) VSYNC(9) G5(0) G6(1) R0(15) */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOI, &g);

    /* PH: R3(9) */
    g.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOH, &g);

    /* PJ: R1-R2(0,1), R4-R7(3,4,5,6), G0-G4(7,8,9,10,11), B0-B3(12,13,14,15) */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
            GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
            GPIO_PIN_12| GPIO_PIN_13| GPIO_PIN_14| GPIO_PIN_15;
    HAL_GPIO_Init(GPIOJ, &g);

    /* PK: G7(2) B4-B7(3,4,5,6) DE(7) BL(0-output) */
    g.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOK, &g);

    /* LCD_BL = PK0 — GPIO output */
    g.Mode      = GPIO_MODE_OUTPUT_PP;
    g.Alternate = 0;
    g.Pin       = LCD_BL_PIN;
    HAL_GPIO_Init(LCD_BL_PORT, &g);
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);

    /* LCD_DISP = PD7 — GPIO output */
    g.Pin = LCD_DISP_PIN;
    HAL_GPIO_Init(LCD_DISP_PORT, &g);
    HAL_GPIO_WritePin(LCD_DISP_PORT, LCD_DISP_PIN, GPIO_PIN_RESET);
}

static void LCD_ClockConfig(void)
{
    /* Match the STM32H750B-DK LTDC example clocking.
     * HSE = 25 MHz, PLL3M = 5  -> 5 MHz VCO input
     * PLL3N = 160              -> 800 MHz VCO output
     * PLL3R = 83               -> ~9.63 MHz LTDC clock
     */
    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection   = RCC_PERIPHCLK_LTDC;
    clk.PLL3.PLL3M             = 5;
    clk.PLL3.PLL3N             = 160;
    clk.PLL3.PLL3FRACN         = 0;
    clk.PLL3.PLL3P             = 2;
    clk.PLL3.PLL3Q             = 2;
    clk.PLL3.PLL3R             = 83;
    clk.PLL3.PLL3RGE           = RCC_PLL3VCIRANGE_2;
    clk.PLL3.PLL3VCOSEL        = RCC_PLL3VCOWIDE;
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) {
        Error_Handler();
    }
    __HAL_RCC_LTDC_CLK_ENABLE();
}
