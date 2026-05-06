/**
 * @file qspi_init.c
 * QUADSPI memory-mapped (XIP) init for Micron MT25TL01G — Bank 1, 64 MB.
 *
 * Adapted from STM32CubeH7 QSPI_MemoryMappedDual example for early boot init.
 * 
 * STM32H750B-DK Bank-1 pin assignment (from UM2488 / board schematic):
 *   Signal   Pin   AF
 *   CLK      PF10  AF9
 *   NCS      PG6   AF10
 *   IO0      PD11  AF9
 *   IO1      PF9   AF10
 *   IO2      PF7   AF9
 *   IO3      PF6   AF9
 *
 * Read mode: Quad-Output Fast Read (0x6B) — 1-line inst, 1-line 32-bit addr,
 * 8 dummy cycles, 4-line data.  Clock = AHB3 (200 MHz) / (1+1) = 100 MHz.
 */

#include "qspi_init.h"
#include "main.h"       /* Error_Handler() */
#include "app_log.h"
#include "stm32h7xx_hal.h"

/* ---- MT25TL01G command set (Micron, SPI-NOR compatible) ------------------- */
#define MT25_RESET_ENABLE_CMD         0x66U
#define MT25_RESET_MEM_CMD            0x99U
#define MT25_WRITE_ENABLE_CMD         0x06U
#define MT25_READ_STATUS_CMD          0x05U
#define MT25_ENTER_4BYTE_ADDR_CMD     0xB7U
#define MT25_READ_VOL_CFG_REG_CMD     0x85U  /* Volatile Config Register */
#define MT25_WRITE_VOL_CFG_REG_CMD    0x81U
#define MT25_QUAD_OUT_FAST_READ       0x6BU  /* 1-1-4: inst/addr on SIO0, data on 4 lines */
#define MT25_DUMMY_CYCLES             8U

/*
 * Single bank = 64 MB = 2^26 bytes.
 * QUADSPI FlashSize field = number_of_address_bits – 1 = 25.
 */
#define MT25_FLASH_SIZE_FIELD         25U
#define QSPI_CLOCK_PRESCALER          3U   /* 200 MHz / 4 = 50 MHz (conservative for XIP debug) */

/* Volatile config register dummy cycles field (bits 7:4 for quad read) */
#define QSPI_VCR_DUMMY_CYCLES_MASK    0xF0U
#define QSPI_VCR_DUMMY_QUAD_SHIFT     4U

/* Handle is file-scope; no public API needed beyond the init function. */
static QSPI_HandleTypeDef hqspi;

/* ---- Helpers -------------------------------------------------------------- */

static void QSPI_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    /* Peripheral + GPIO clocks */
    __HAL_RCC_QSPI_CLK_ENABLE();
    __HAL_RCC_QSPI_FORCE_RESET();
    __HAL_RCC_QSPI_RELEASE_RESET();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    g.Mode  = GPIO_MODE_AF_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    /* CLK: PF10, AF9 */
    g.Pin = GPIO_PIN_10; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &g);

    /* NCS: PG6, AF10 — pull-up keeps flash deselected when idle */
    g.Pin = GPIO_PIN_6; g.Pull = GPIO_PULLUP; g.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOG, &g);
    g.Pull = GPIO_NOPULL;

    /* IO0: PD11, AF9 */
    g.Pin = GPIO_PIN_11; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOD, &g);

    /* IO1: PF9, AF10 */
    g.Pin = GPIO_PIN_9; g.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOF, &g);

    /* IO2: PF7, AF9 */
    g.Pin = GPIO_PIN_7; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &g);

    /* IO3: PF6, AF9 */
    g.Pin = GPIO_PIN_6; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &g);
}

static void QSPI_SendCmd1(uint8_t instruction)
{
    QSPI_CommandTypeDef cmd = {0};
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = instruction;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * QSPI_WriteEnable — Send write enable command, poll status for WEL bit
 */
static void QSPI_WriteEnable(void)
{
    QSPI_CommandTypeDef     cmd = {0};
    QSPI_AutoPollingTypeDef cfg = {0};

    /* Send WRITE_ENABLE (0x06) */
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_WRITE_ENABLE_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }

    /* Poll status register for WEL bit (bit 1) */
    cfg.Match           = 0x02U;  /* WEL bit 1 = 1 when write enabled */
    cfg.Mask            = 0x02U;
    cfg.MatchMode       = QSPI_MATCH_MODE_AND;
    cfg.StatusBytesSize = 1U;
    cfg.Interval        = 0x10U;
    cfg.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

    cmd.Instruction     = MT25_READ_STATUS_CMD;
    cmd.DataMode        = QSPI_DATA_1_LINE;

    if (HAL_QSPI_AutoPolling(&hqspi, &cmd, &cfg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * QSPI_WaitReady — Poll status register until WIP bit clears (memory ready)
 */
static void QSPI_WaitReady(void)
{
    QSPI_CommandTypeDef     cmd = {0};
    QSPI_AutoPollingTypeDef cfg = {0};

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_READ_STATUS_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    cfg.Match           = 0x00U;  /* WIP bit 0 = 0 when ready */
    cfg.Mask            = 0x01U;
    cfg.MatchMode       = QSPI_MATCH_MODE_AND;
    cfg.StatusBytesSize = 1U;
    cfg.Interval        = 0x10U;
    cfg.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

    if (HAL_QSPI_AutoPolling(&hqspi, &cmd, &cfg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * QSPI_DummyCyclesCfg — Read and update volatile config register dummy cycles
 * ST reference: configures dummy cycles in memory's volatile register
 */
static void QSPI_DummyCyclesCfg(void)
{
    QSPI_CommandTypeDef cmd = {0};
    uint8_t reg = 0;

    /* Read Volatile Config Register (0x85) */
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_READ_VOL_CFG_REG_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;  /* No dummy for initial read */
    cmd.NbData            = 1;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_QSPI_Receive(&hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }

    /* Update dummy cycles bits (bits 7:4 = quad read dummy cycles) */
    reg = (reg & ~QSPI_VCR_DUMMY_CYCLES_MASK) | (MT25_DUMMY_CYCLES << QSPI_VCR_DUMMY_QUAD_SHIFT);

    /* Write-enable, then write updated VCR */
    QSPI_WriteEnable();

    cmd.Instruction = MT25_WRITE_VOL_CFG_REG_CMD;
    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_QSPI_Transmit(&hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }

    QSPI_WaitReady();
}

/*
 * Configure MPU region for the QSPI window.
 *
 * The QSPI flash is NOR (byte-addressable, read-only from the CPU perspective).
 * Use Normal memory, write-through, no write-allocate so the I-cache and
 * D-cache work correctly without needing cache invalidation on every access.
 */
static void MPU_ConfigQSPI(void)
{
    MPU_Region_InitTypeDef r = {0};

    HAL_MPU_Disable();

    r.Enable           = MPU_REGION_ENABLE;
    r.Number           = MPU_REGION_NUMBER0;
    r.BaseAddress      = 0x90000000U;
    r.Size             = MPU_REGION_SIZE_64MB;
    r.SubRegionDisable = 0x00U;
    r.TypeExtField     = MPU_TEX_LEVEL0;          /* Normal memory            */
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE; /* allow XIP          */
    r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    r.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;/* conservative XIP debug    */
    r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE; /* write-through (C=1,B=0)*/
    HAL_MPU_ConfigRegion(&r);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* ---- Public API ----------------------------------------------------------- */

void QSPI_MemoryMapped_Init(void)
{
    QSPI_CommandTypeDef      cmd  = {0};
    QSPI_MemoryMappedTypeDef mmap = {0};

    APP_LOGI("QSPI", "configure mpu + gpio");
    MPU_ConfigQSPI();
    QSPI_GPIO_Init();

    hqspi.Instance                = QUADSPI;
    hqspi.Init.ClockPrescaler     = QSPI_CLOCK_PRESCALER;
    hqspi.Init.FifoThreshold      = 1U;
    hqspi.Init.SampleShifting     = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    hqspi.Init.FlashSize          = MT25_FLASH_SIZE_FIELD;
    hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_3_CYCLE;
    hqspi.Init.ClockMode          = QSPI_CLOCK_MODE_0;
    hqspi.Init.FlashID            = QSPI_FLASH_ID_1;      /* Bank 1 only     */
    hqspi.Init.DualFlash          = QSPI_DUALFLASH_DISABLE;

    HAL_QSPI_DeInit(&hqspi);
    if (HAL_QSPI_Init(&hqspi) != HAL_OK) {
        Error_Handler();
    }
    APP_LOGI("QSPI", "hal qspi init done @100MHz");

    /* Software reset — bring flash to a known state */
    QSPI_SendCmd1(MT25_RESET_ENABLE_CMD);
    QSPI_SendCmd1(MT25_RESET_MEM_CMD);
    /* Note: removed HAL_Delay(1) — tRST ≥ 30 µs spec can be met by command processing */

    QSPI_WaitReady();
    APP_LOGI("QSPI", "reset complete");

    /* Write-enable then enter 4-byte address mode (required for >16 MB) */
    QSPI_WriteEnable();
    QSPI_SendCmd1(MT25_ENTER_4BYTE_ADDR_CMD);
    QSPI_WaitReady();
    APP_LOGI("QSPI", "4-byte mode enabled");

    /* Configure dummy cycles in flash's volatile register */
    QSPI_DummyCyclesCfg();
    APP_LOGI("QSPI", "dummy cycles configured=%u", (unsigned)MT25_DUMMY_CYCLES);

    /* Enable memory-mapped mode: Quad-Output Fast Read (0x6B) -------------- */
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_QUAD_OUT_FAST_READ;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_4_LINES;
    cmd.DummyCycles       = MT25_DUMMY_CYCLES;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    mmap.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;

    if (HAL_QSPI_MemoryMapped(&hqspi, &cmd, &mmap) != HAL_OK) {
        Error_Handler();
    }
    __DSB();
    __ISB();
    SCB_InvalidateICache();
    __DSB();
    __ISB();
    APP_LOGI("QSPI", "memory-mapped mode enabled @0x90000000");
}
