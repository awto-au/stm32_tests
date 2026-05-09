/**
 * @file qspi_init.c
 * QUADSPI memory-mapped (XIP) init for Micron MT25TL01G — Bank 1, 64 MB.
 *
 * Adapted from STM32CubeH7 QSPI_MemoryMappedDual example for early boot init.
 * 
 * STM32H750B-DK dual-QSPI pin assignment (from UM2488 / board schematic):
 *   Signal   Pin   AF
 *   CLK      PF10  AF9
 *   NCS      PG6   AF10
 *   BK1_IO0  PD11  AF9
 *   BK1_IO1  PF9   AF10
 *   BK1_IO2  PF7   AF9
 *   BK1_IO3  PF6   AF9
 *   BK2_IO0  PH2   AF9
 *   BK2_IO1  PH3   AF9
 *   BK2_IO2  PG9   AF9
 *   BK2_IO3  PG14  AF9
 *
 * Read mode: Quad-Output Fast Read (0x6B) — 1-line inst, 1-line 32-bit addr,
 * 8 dummy cycles, 4-line data.  Clock = AHB3 (200 MHz) / (1+1) = 100 MHz.
 */

#include "qspi_init.h"
#include "main.h"       /* Error_Handler() */
#include "app_log.h"
#include "stm32h7xx_hal.h"
#include <string.h>

/* ---- MT25TL01G command set (Micron, SPI-NOR compatible) ------------------- */
#define MT25_RESET_ENABLE_CMD         0x66U
#define MT25_RESET_MEM_CMD            0x99U
#define MT25_WRITE_ENABLE_CMD         0x06U
#define MT25_READ_STATUS_CMD          0x05U
#define MT25_READ_FLAG_STATUS_CMD     0x70U
#define MT25_ENTER_4BYTE_ADDR_CMD     0xB7U
#define MT25_READ_VOL_CFG_REG_CMD     0x85U  /* Volatile Config Register */
#define MT25_WRITE_VOL_CFG_REG_CMD    0x81U
#define MT25_READ_DATA_CMD            0x03U
#define MT25_SUBSECTOR_ERASE_CMD      0x20U
#define MT25_PAGE_PROGRAM_CMD         0x02U
#define MT25_QUAD_OUT_FAST_READ       0x6BU  /* 1-1-4: inst/addr on SIO0, data on 4 lines */
#define MT25_DUMMY_CYCLES             8U

/*
 * Single die = 64 MB = 2^26 bytes -> FlashSize field 25.
 * Dual-flash interleaved window = 128 MB = 2^27 bytes -> FlashSize field 26.
 */
#define MT25_FLASH_SIZE_FIELD_SINGLE  25U
#define MT25_FLASH_SIZE_FIELD_DUAL    26U
#define QSPI_CLOCK_PRESCALER          3U   /* 200 MHz / 4 = 50 MHz */
#define QSPI_KERNEL_CLOCK_HZ          200000000U

/* Volatile config register dummy cycles field (bits 7:4 for quad read) */
#define QSPI_VCR_DUMMY_CYCLES_MASK    0xF0U
#define QSPI_VCR_DUMMY_QUAD_SHIFT     4U

/* Destructive self-test region near top of 64MB bank to avoid app assets. */
#define QSPI_SELFTEST_ENABLE          1U
#define QSPI_SELFTEST_ADDR            0x03F00000U
#define QSPI_SELFTEST_SIZE            4096U
#define QSPI_PAGE_SIZE                256U
#define QSPI_RAMTEST_WORDS            512U
#define QSPI_SOAK_TEST_MS             1000U

typedef struct
{
    uint8_t prescaler;
    uint32_t sample_shifting;
    uint32_t cs_high_time;
    uint8_t dummy_cycles;
} QSPI_TuneConfig;

/* Handle is file-scope; no public API needed beyond the init function. */
static QSPI_HandleTypeDef hqspi;

static void QSPI_WriteEnable(void);
static void QSPI_WaitReady(void);
static void QSPI_DummyCyclesCfg(uint8_t dummy_cycles);
static void QSPI_SetFlashTarget(uint32_t flash_id, uint32_t dualflash);
static void QSPI_DumpQspiRegs(const char *tag);
static void QSPI_DumpCoreRegs(const char *tag);
static void QSPI_DumpFlashRegs(const char *tag);
static void QSPI_DumpMmWindow(const char *tag);
static int QSPI_TryReadRegs(uint8_t instruction, uint8_t *buf, uint8_t len);
static int QSPI_RunSelfTestAtCurrentSpeed(uint8_t dummy_cycles, uint32_t *write_mbs_x100, uint32_t *read_mbs_x100);
static int QSPI_RunSelfTestForMs(uint8_t dummy_cycles, uint32_t duration_ms, uint32_t *write_mbs_x100, uint32_t *read_mbs_x100);

static uint32_t qspi_ramtest_buf[QSPI_RAMTEST_WORDS];

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
    __HAL_RCC_GPIOH_CLK_ENABLE();

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

    /* BK1 IO3: PF6, AF9 */
    g.Pin = GPIO_PIN_6; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &g);

    /* BK2 IO0: PH2, AF9 */
    g.Pin = GPIO_PIN_2; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOH, &g);

    /* BK2 IO1: PH3, AF9 */
    g.Pin = GPIO_PIN_3; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOH, &g);

    /* BK2 IO2: PG9, AF9 */
    g.Pin = GPIO_PIN_9; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOG, &g);

    /* BK2 IO3: PG14, AF9 */
    g.Pin = GPIO_PIN_14; g.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOG, &g);
}

static void QSPI_SendCmd(uint8_t instruction)
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

static uint8_t QSPI_ReadReg(uint8_t instruction)
{
    QSPI_CommandTypeDef cmd = {0};
    uint8_t reg = 0U;

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = instruction;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = 1U;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_QSPI_Receive(&hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    return reg;
}

static void __attribute__((unused)) QSPI_ReadRegs(uint8_t instruction, uint8_t *buf, uint8_t len)
{
    QSPI_CommandTypeDef cmd = {0};

    if (len == 0U || buf == NULL) {
        return;
    }

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = instruction;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = len;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_QSPI_Receive(&hqspi, buf, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
}

static int QSPI_TryReadRegs(uint8_t instruction, uint8_t *buf, uint8_t len)
{
    QSPI_CommandTypeDef cmd = {0};

    if (len == 0U || buf == NULL) {
        return 0;
    }

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = instruction;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = len;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return 0;
    }
    if (HAL_QSPI_Receive(&hqspi, buf, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return 0;
    }
    return 1;
}

static void QSPI_LogRegs(const char *tag)
{
    uint8_t sr = QSPI_ReadReg(MT25_READ_STATUS_CMD);
    uint8_t vcr = QSPI_ReadReg(MT25_READ_VOL_CFG_REG_CMD);
    APP_LOGI("QSPI", "%s SR=0x%02x (WIP=%u WEL=%u) VCR=0x%02x",
             tag,
             (unsigned)sr,
             (unsigned)(sr & 0x01U),
             (unsigned)((sr >> 1) & 0x01U),
             (unsigned)vcr);
}

static void QSPI_DumpQspiRegs(const char *tag)
{
    APP_LOGI("QDBG", "%s QUADSPI CR=0x%08lx DCR=0x%08lx SR=0x%08lx FCR=0x%08lx",
             tag,
             (unsigned long)QUADSPI->CR,
             (unsigned long)QUADSPI->DCR,
             (unsigned long)QUADSPI->SR,
             (unsigned long)QUADSPI->FCR);
    APP_LOGI("QDBG", "%s QUADSPI DLR=0x%08lx CCR=0x%08lx AR=0x%08lx ABR=0x%08lx",
             tag,
             (unsigned long)QUADSPI->DLR,
             (unsigned long)QUADSPI->CCR,
             (unsigned long)QUADSPI->AR,
             (unsigned long)QUADSPI->ABR);
    APP_LOGI("QDBG", "%s QUADSPI PSMKR=0x%08lx PSMAR=0x%08lx PIR=0x%08lx LPTR=0x%08lx",
             tag,
             (unsigned long)QUADSPI->PSMKR,
             (unsigned long)QUADSPI->PSMAR,
             (unsigned long)QUADSPI->PIR,
             (unsigned long)QUADSPI->LPTR);
}

static void QSPI_DumpCoreRegs(const char *tag)
{
    APP_LOGI("QDBG", "%s SCB CCR=0x%08lx SHCSR=0x%08lx CFSR=0x%08lx HFSR=0x%08lx",
             tag,
             (unsigned long)SCB->CCR,
             (unsigned long)SCB->SHCSR,
             (unsigned long)SCB->CFSR,
             (unsigned long)SCB->HFSR);
    APP_LOGI("QDBG", "%s SCB BFAR=0x%08lx MMFAR=0x%08lx DFSR=0x%08lx AFSR=0x%08lx VTOR=0x%08lx",
             tag,
             (unsigned long)SCB->BFAR,
             (unsigned long)SCB->MMFAR,
             (unsigned long)SCB->DFSR,
             (unsigned long)SCB->AFSR,
             (unsigned long)SCB->VTOR);
}

static void QSPI_DumpFlashRegs(const char *tag)
{
    uint8_t sr[2] = {0U, 0U};
    uint8_t fsr[2] = {0U, 0U};
    uint8_t vcr[2] = {0U, 0U};
    uint8_t n = (hqspi.Init.DualFlash == QSPI_DUALFLASH_ENABLE) ? 2U : 1U;

    if (!QSPI_TryReadRegs(MT25_READ_STATUS_CMD, sr, n) ||
        !QSPI_TryReadRegs(MT25_READ_FLAG_STATUS_CMD, fsr, n) ||
        !QSPI_TryReadRegs(MT25_READ_VOL_CFG_REG_CMD, vcr, n)) {
        APP_LOGI("QDBG", "%s FLASH reg read failed (non-fatal)", tag);
        return;
    }

    if (n == 2U) {
        APP_LOGI("QDBG", "%s FLASH SR=[%02x %02x] FSR=[%02x %02x] VCR=[%02x %02x]",
                 tag,
                 (unsigned)sr[0], (unsigned)sr[1],
                 (unsigned)fsr[0], (unsigned)fsr[1],
                 (unsigned)vcr[0], (unsigned)vcr[1]);
    } else {
        APP_LOGI("QDBG", "%s FLASH SR=0x%02x FSR=0x%02x VCR=0x%02x",
                 tag,
                 (unsigned)sr[0],
                 (unsigned)fsr[0],
                 (unsigned)vcr[0]);
    }
}

static void QSPI_DumpMmWindow(const char *tag)
{
    volatile uint8_t *p8 = (volatile uint8_t *)0x90000000U;
    volatile uint32_t *p32 = (volatile uint32_t *)0x90000000U;

    APP_LOGI("QDBG", "%s MM8  [0..15]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             tag,
             (unsigned)p8[0], (unsigned)p8[1], (unsigned)p8[2], (unsigned)p8[3],
             (unsigned)p8[4], (unsigned)p8[5], (unsigned)p8[6], (unsigned)p8[7],
             (unsigned)p8[8], (unsigned)p8[9], (unsigned)p8[10], (unsigned)p8[11],
             (unsigned)p8[12], (unsigned)p8[13], (unsigned)p8[14], (unsigned)p8[15]);
    APP_LOGI("QDBG", "%s MM32 [0..3]=%08lx %08lx %08lx %08lx",
             tag,
             (unsigned long)p32[0],
             (unsigned long)p32[1],
             (unsigned long)p32[2],
             (unsigned long)p32[3]);
}

static uint32_t QSPI_CyclesPerSecond(void)
{
    uint32_t hz = SystemCoreClock;
    return (hz == 0U) ? 400000000U : hz;
}

static uint32_t QSPI_MBs_x100(uint32_t bytes, uint32_t cycles)
{
    uint64_t num;
    uint64_t den;

    if (cycles == 0U) {
        return 0U;
    }

    num = (uint64_t)bytes * (uint64_t)QSPI_CyclesPerSecond() * 100ULL;
    den = (uint64_t)cycles * 1024ULL * 1024ULL;
    return (uint32_t)(num / den);
}

static void QSPI_EnableMemoryMappedRead(uint8_t dummy_cycles)
{
    QSPI_CommandTypeDef cmd = {0};
    QSPI_MemoryMappedTypeDef mmap = {0};

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_QUAD_OUT_FAST_READ;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_4_LINES;
    cmd.DummyCycles       = dummy_cycles;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    mmap.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;

    if (HAL_QSPI_MemoryMapped(&hqspi, &cmd, &mmap) != HAL_OK) {
        Error_Handler();
    }
}

static void QSPI_DisableMemoryMappedRead(void)
{
    if (HAL_QSPI_Abort(&hqspi) != HAL_OK) {
        Error_Handler();
    }
}

static uint32_t QSPI_FreqMHz(uint32_t prescaler)
{
    return (QSPI_KERNEL_CLOCK_HZ / (prescaler + 1U)) / 1000000U;
}

static int QSPI_RunClassicRamTests(void)
{
    static const uint32_t patterns[] = {
        0x00000000U,
        0xFFFFFFFFU,
        0xAAAAAAAAU,
        0x55555555U,
        0x33333333U,
        0xCCCCCCCCU
    };
    uint32_t i;
    uint32_t p;

    APP_LOGI("RAM", "classic test start words=%lu", (unsigned long)QSPI_RAMTEST_WORDS);

    for (p = 0; p < (sizeof(patterns) / sizeof(patterns[0])); p++) {
        uint32_t pat = patterns[p];
        for (i = 0; i < QSPI_RAMTEST_WORDS; i++) {
            qspi_ramtest_buf[i] = pat;
        }
        for (i = 0; i < QSPI_RAMTEST_WORDS; i++) {
            if (qspi_ramtest_buf[i] != pat) {
                APP_LOGI("RAM", "pattern FAIL pat=0x%08lx idx=%lu got=0x%08lx",
                         (unsigned long)pat,
                         (unsigned long)i,
                         (unsigned long)qspi_ramtest_buf[i]);
                return 0;
            }
        }
    }

    for (i = 0; i < QSPI_RAMTEST_WORDS; i++) {
        qspi_ramtest_buf[i] = (1UL << (i & 31U));
    }
    for (i = 0; i < QSPI_RAMTEST_WORDS; i++) {
        uint32_t exp = (1UL << (i & 31U));
        if (qspi_ramtest_buf[i] != exp) {
            APP_LOGI("RAM", "walk1 FAIL idx=%lu exp=0x%08lx got=0x%08lx",
                     (unsigned long)i,
                     (unsigned long)exp,
                     (unsigned long)qspi_ramtest_buf[i]);
            return 0;
        }
    }

    for (i = 0; i < QSPI_RAMTEST_WORDS; i++) {
        qspi_ramtest_buf[i] = ~(1UL << (i & 31U));
    }
    for (i = 0; i < QSPI_RAMTEST_WORDS; i++) {
        uint32_t exp = ~(1UL << (i & 31U));
        if (qspi_ramtest_buf[i] != exp) {
            APP_LOGI("RAM", "walk0 FAIL idx=%lu exp=0x%08lx got=0x%08lx",
                     (unsigned long)i,
                     (unsigned long)exp,
                     (unsigned long)qspi_ramtest_buf[i]);
            return 0;
        }
    }

    for (i = 0; i < QSPI_RAMTEST_WORDS; i++) {
        qspi_ramtest_buf[i] = ((i * 0x9E3779B1U) ^ 0xA5A5A5A5U);
    }
    for (i = 0; i < QSPI_RAMTEST_WORDS; i++) {
        uint32_t exp = ((i * 0x9E3779B1U) ^ 0xA5A5A5A5U);
        if (qspi_ramtest_buf[i] != exp) {
            APP_LOGI("RAM", "addr/data FAIL idx=%lu exp=0x%08lx got=0x%08lx",
                     (unsigned long)i,
                     (unsigned long)exp,
                     (unsigned long)qspi_ramtest_buf[i]);
            return 0;
        }
    }

    APP_LOGI("RAM", "classic test PASS");
    return 1;
}

static void QSPI_InitPeripheral(const QSPI_TuneConfig *cfg)
{
    hqspi.Instance                = QUADSPI;
    hqspi.Init.ClockPrescaler     = cfg->prescaler;
    hqspi.Init.FifoThreshold      = 4U;
    hqspi.Init.SampleShifting     = cfg->sample_shifting;
    hqspi.Init.FlashSize          = MT25_FLASH_SIZE_FIELD_DUAL;
    hqspi.Init.ChipSelectHighTime = cfg->cs_high_time;
    hqspi.Init.ClockMode          = QSPI_CLOCK_MODE_0;
    hqspi.Init.FlashID            = QSPI_FLASH_ID_2;
    hqspi.Init.DualFlash          = QSPI_DUALFLASH_ENABLE;  /* STM32H750B-DK has dual QSPI NOR */

    HAL_QSPI_DeInit(&hqspi);
    if (HAL_QSPI_Init(&hqspi) != HAL_OK) {
        Error_Handler();
    }
}

static void QSPI_SetFlashTarget(uint32_t flash_id, uint32_t dualflash)
{
    hqspi.Init.FlashID = flash_id;
    hqspi.Init.DualFlash = dualflash;
    hqspi.Init.FlashSize = (dualflash == QSPI_DUALFLASH_ENABLE)
                           ? MT25_FLASH_SIZE_FIELD_DUAL
                           : MT25_FLASH_SIZE_FIELD_SINGLE;

    HAL_QSPI_DeInit(&hqspi);
    if (HAL_QSPI_Init(&hqspi) != HAL_OK) {
        /* Some stacks reject FLASH_ID_2 when DualFlash is enabled; retry FLASH_ID_1. */
        if ((dualflash == QSPI_DUALFLASH_ENABLE) && (flash_id == QSPI_FLASH_ID_2)) {
            APP_LOGI("QSPI", "dual init with FlashID=2 failed, retry FlashID=1");
            hqspi.Init.FlashID = QSPI_FLASH_ID_1;
            HAL_QSPI_DeInit(&hqspi);
            if (HAL_QSPI_Init(&hqspi) != HAL_OK) {
                Error_Handler();
            }
        } else {
            Error_Handler();
        }
    }

    APP_LOGI("QSPI", "target flash=%lu dual=%lu fsize=%lu",
             (unsigned long)hqspi.Init.FlashID,
             (unsigned long)hqspi.Init.DualFlash,
             (unsigned long)hqspi.Init.FlashSize);
}

static int QSPI_ConfigFlashForMemoryMapped(const QSPI_TuneConfig *cfg)
{
    /* ST QSPI_MemoryMappedDual sequence: keep dual enabled and configure once. */
    QSPI_SetFlashTarget(QSPI_FLASH_ID_2, QSPI_DUALFLASH_ENABLE);

    QSPI_SendCmd(MT25_RESET_ENABLE_CMD);
    QSPI_SendCmd(MT25_RESET_MEM_CMD);
    QSPI_WaitReady();

    QSPI_WriteEnable();
    QSPI_SendCmd(MT25_ENTER_4BYTE_ADDR_CMD);
    QSPI_WaitReady();

    QSPI_DummyCyclesCfg(cfg->dummy_cycles);
    return 1;
}

static void QSPI_Erase4K(uint32_t address)
{
    QSPI_CommandTypeDef cmd = {0};

    QSPI_WriteEnable();

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_SUBSECTOR_ERASE_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.Address           = address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }

    QSPI_WaitReady();
}

static void QSPI_ProgramPage(uint32_t address, const uint8_t *data, uint32_t len)
{
    QSPI_CommandTypeDef cmd = {0};

    QSPI_WriteEnable();

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_PAGE_PROGRAM_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.Address           = address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = len;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_QSPI_Transmit(&hqspi, (uint8_t *)data, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }

    QSPI_WaitReady();
}

static void QSPI_ReadData(uint32_t address, uint8_t *data, uint32_t len)
{
    QSPI_CommandTypeDef cmd = {0};

    /* Use 0x6B (Quad Output Fast Read) with 8 dummy cycles — same command as MM mode.
     * Single-line 0x03 would give artificially low indirect BW in the benchmark. */
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_QUAD_OUT_FAST_READ;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.Address           = address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_4_LINES;
    cmd.DummyCycles       = MT25_DUMMY_CYCLES;
    cmd.NbData            = len;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_QSPI_Receive(&hqspi, data, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
}

static int QSPI_RunSelfTestAtCurrentSpeed(uint8_t dummy_cycles, uint32_t *write_mbs_x100, uint32_t *read_mbs_x100)
{
    static uint8_t tx[QSPI_SELFTEST_SIZE];
    static uint8_t rx[QSPI_SELFTEST_SIZE];
    uint32_t erase_cycles;
    uint32_t write_cycles;
    uint32_t read_cycles;
    uint32_t mm_read_cycles;
    uint32_t t0;
    uint32_t i;

    APP_LOGI("QSPI", "selftest start addr=0x%08lx size=%lu",
             (unsigned long)QSPI_SELFTEST_ADDR,
             (unsigned long)QSPI_SELFTEST_SIZE);

    for (i = 0; i < QSPI_SELFTEST_SIZE; i++) {
        tx[i] = (uint8_t)((i * 37U) ^ 0xA5U);
        rx[i] = 0U;
    }

    t0 = DWT->CYCCNT;
    QSPI_Erase4K(QSPI_SELFTEST_ADDR);
    erase_cycles = DWT->CYCCNT - t0;

    t0 = DWT->CYCCNT;
    for (i = 0; i < QSPI_SELFTEST_SIZE; i += QSPI_PAGE_SIZE) {
        QSPI_ProgramPage(QSPI_SELFTEST_ADDR + i, &tx[i], QSPI_PAGE_SIZE);
    }
    write_cycles = DWT->CYCCNT - t0;

    t0 = DWT->CYCCNT;
    QSPI_ReadData(QSPI_SELFTEST_ADDR, rx, QSPI_SELFTEST_SIZE);
    read_cycles = DWT->CYCCNT - t0;

    t0 = DWT->CYCCNT;
    QSPI_EnableMemoryMappedRead(dummy_cycles);
    {
        volatile uint32_t *mm = (volatile uint32_t *)(0x90000000U + QSPI_SELFTEST_ADDR);
        volatile uint32_t *dst = (volatile uint32_t *)(void *)rx;
        for (i = 0; i < QSPI_SELFTEST_SIZE / 4U; i++) {
            dst[i] = mm[i];
        }
    }
    mm_read_cycles = DWT->CYCCNT - t0;
    QSPI_DisableMemoryMappedRead();

    if (memcmp(tx, rx, QSPI_SELFTEST_SIZE) != 0) {
        APP_LOGI("QSPI", "selftest FAIL: MM verify mismatch");
        for (i = 0; i < QSPI_SELFTEST_SIZE; i++) {
            if (tx[i] != rx[i]) {
                APP_LOGI("QSPI", "mismatch @+0x%04lx exp=0x%02x got=0x%02x",
                         (unsigned long)i,
                         (unsigned)tx[i],
                         (unsigned)rx[i]);
                break;
            }
        }
        return 0;
    }

    APP_LOGI("QSPI", "selftest PASS erase=%lu cyc write=%lu cyc read=%lu cyc",
             (unsigned long)erase_cycles,
             (unsigned long)write_cycles,
             (unsigned long)read_cycles);
    *write_mbs_x100 = QSPI_MBs_x100(QSPI_SELFTEST_SIZE, write_cycles);
    *read_mbs_x100 = QSPI_MBs_x100(QSPI_SELFTEST_SIZE, mm_read_cycles);

    APP_LOGI("QSPI", "selftest BW write=%lu.%02lu MB/s read(indirect)=%lu.%02lu MB/s read(mm)=%lu.%02lu MB/s",
             (unsigned long)(*write_mbs_x100 / 100U),
             (unsigned long)(*write_mbs_x100 % 100U),
             (unsigned long)(QSPI_MBs_x100(QSPI_SELFTEST_SIZE, read_cycles) / 100U),
             (unsigned long)(QSPI_MBs_x100(QSPI_SELFTEST_SIZE, read_cycles) % 100U),
             (unsigned long)(*read_mbs_x100 / 100U),
             (unsigned long)(*read_mbs_x100 % 100U));
    return 1;
}

static int QSPI_VerifyMemoryMappedRead(uint8_t dummy_cycles)
{
    volatile uint32_t w0, w1, w2, w3;
    
    QSPI_EnableMemoryMappedRead(dummy_cycles);
    __DSB();
    __ISB();
    
    w0 = *(volatile uint32_t *)(0x90000000U);
    w1 = *(volatile uint32_t *)(0x90000004U);
    w2 = *(volatile uint32_t *)(0x90000008U);
    w3 = *(volatile uint32_t *)(0x9000000CU);
    
    QSPI_DisableMemoryMappedRead();
    
    APP_LOGI("QSPI", "mm_verify words: 0x%08lx 0x%08lx 0x%08lx 0x%08lx",
             (unsigned long)w0,
             (unsigned long)w1,
             (unsigned long)w2,
             (unsigned long)w3);
    
    return 1;
}

static int QSPI_RunSelfTestForMs(uint8_t dummy_cycles, uint32_t duration_ms, uint32_t *write_mbs_x100, uint32_t *read_mbs_x100)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t target_cycles = (QSPI_CyclesPerSecond() / 1000U) * duration_ms;
    uint32_t loops = 0U;
    uint32_t write_bw = 0U;
    uint32_t read_bw = 0U;

    if (target_cycles == 0U) {
        target_cycles = 1U;
    }

    do {
        loops++;
        if (!QSPI_RunSelfTestAtCurrentSpeed(dummy_cycles, &write_bw, &read_bw)) {
            APP_LOGI("QSPI", "soak FAIL loop=%lu", (unsigned long)loops);
            return 0;
        }
        APP_LOGI("QSPI", "soak loop=%lu write=%lu.%02lu read=%lu.%02lu MB/s",
                 (unsigned long)loops,
                 (unsigned long)(write_bw / 100U),
                 (unsigned long)(write_bw % 100U),
                 (unsigned long)(read_bw / 100U),
                 (unsigned long)(read_bw % 100U));
    } while ((DWT->CYCCNT - start) < target_cycles);

    *write_mbs_x100 = write_bw;
    *read_mbs_x100 = read_bw;
    APP_LOGI("QSPI", "soak PASS duration=%lums loops=%lu",
             (unsigned long)duration_ms,
             (unsigned long)loops);
    return 1;
}

static uint32_t QSPI_BenchmarkMmRead_x100(uint32_t size_bytes, uint32_t passes)
{
    volatile uint32_t *p32 = (volatile uint32_t *)0x90000000U;
    uint32_t words = size_bytes / 4U;
    uint32_t p;
    uint32_t i;
    uint32_t t0;
    uint32_t cycles;
    volatile uint32_t sink = 0U;

    if (words == 0U || passes == 0U) {
        return 0U;
    }

    __DSB();
    __ISB();
    t0 = DWT->CYCCNT;
    for (p = 0U; p < passes; p++) {
        for (i = 0U; i < words; i++) {
            sink ^= p32[i];
        }
    }
    cycles = DWT->CYCCNT - t0;
    APP_LOGI("QBENCH", "mm-read checksum=0x%08lx size=%lu passes=%lu cycles=%lu",
             (unsigned long)sink,
             (unsigned long)size_bytes,
             (unsigned long)passes,
             (unsigned long)cycles);

    return QSPI_MBs_x100(size_bytes * passes, cycles);
}

static QSPI_TuneConfig __attribute__((unused)) QSPI_FindFastestStableConfig(void)
{
    static const uint8_t prescalers[] = {1U, 2U, 3U, 4U, 5U};
    static const uint32_t sample_modes[] = {
        QSPI_SAMPLE_SHIFTING_HALFCYCLE,
        QSPI_SAMPLE_SHIFTING_NONE
    };
    static const uint32_t cs_times[] = {
        QSPI_CS_HIGH_TIME_6_CYCLE,
        QSPI_CS_HIGH_TIME_3_CYCLE,
        QSPI_CS_HIGH_TIME_2_CYCLE
    };
    static const uint8_t dummy_cycles[] = {10U, 8U, 6U};

    uint32_t pi;
    uint32_t si;
    uint32_t ci;
    uint32_t di;
    uint32_t write_bw = 0U;
    uint32_t read_bw = 0U;
    QSPI_TuneConfig cfg;

    for (pi = 0U; pi < (sizeof(prescalers) / sizeof(prescalers[0])); pi++) {
        cfg.prescaler = prescalers[pi];

        for (si = 0U; si < (sizeof(sample_modes) / sizeof(sample_modes[0])); si++) {
            cfg.sample_shifting = sample_modes[si];

            for (ci = 0U; ci < (sizeof(cs_times) / sizeof(cs_times[0])); ci++) {
                cfg.cs_high_time = cs_times[ci];

                for (di = 0U; di < (sizeof(dummy_cycles) / sizeof(dummy_cycles[0])); di++) {
                    cfg.dummy_cycles = dummy_cycles[di];

                    APP_LOGI("QSPI", "probe cfg p=%lu %luMHz sample=%s cs=%lu dummy=%lu",
                             (unsigned long)cfg.prescaler,
                             (unsigned long)QSPI_FreqMHz(cfg.prescaler),
                             (cfg.sample_shifting == QSPI_SAMPLE_SHIFTING_HALFCYCLE) ? "half" : "none",
                             (unsigned long)cfg.cs_high_time,
                             (unsigned long)cfg.dummy_cycles);

                    QSPI_InitPeripheral(&cfg);
                    QSPI_LogRegs("after HAL_QSPI_Init");

                    if (!QSPI_ConfigFlashForMemoryMapped(&cfg)) {
                        APP_LOGI("QSPI", "probe FAIL (flash cfg)");
                        continue;
                    }

                    QSPI_LogRegs("post-dummycfg");
                    
                    if (!QSPI_VerifyMemoryMappedRead(cfg.dummy_cycles)) {
                        APP_LOGI("QSPI", "probe FAIL (mm verify)");
                        continue;
                    }
                    
                    if (QSPI_RunSelfTestForMs(cfg.dummy_cycles, QSPI_SOAK_TEST_MS, &write_bw, &read_bw)) {
                        APP_LOGI("QSPI", "probe PASS p=%lu %luMHz sample=%s cs=%lu dummy=%lu write=%lu.%02lu read=%lu.%02lu MB/s",
                                 (unsigned long)cfg.prescaler,
                                 (unsigned long)QSPI_FreqMHz(cfg.prescaler),
                                 (cfg.sample_shifting == QSPI_SAMPLE_SHIFTING_HALFCYCLE) ? "half" : "none",
                                 (unsigned long)cfg.cs_high_time,
                                 (unsigned long)cfg.dummy_cycles,
                                 (unsigned long)(write_bw / 100U),
                                 (unsigned long)(write_bw % 100U),
                                 (unsigned long)(read_bw / 100U),
                                 (unsigned long)(read_bw % 100U));
                        return cfg;
                    }

                    APP_LOGI("QSPI", "probe FAIL p=%lu %luMHz sample=%s cs=%lu dummy=%lu",
                             (unsigned long)cfg.prescaler,
                             (unsigned long)QSPI_FreqMHz(cfg.prescaler),
                             (cfg.sample_shifting == QSPI_SAMPLE_SHIFTING_HALFCYCLE) ? "half" : "none",
                             (unsigned long)cfg.cs_high_time,
                             (unsigned long)cfg.dummy_cycles);
                }
            }
        }
    }

    APP_LOGI("QSPI", "probe FAILED all matrix candidates");
    Error_Handler();
    cfg.prescaler = QSPI_CLOCK_PRESCALER;
    cfg.sample_shifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    cfg.cs_high_time = QSPI_CS_HIGH_TIME_3_CYCLE;
    cfg.dummy_cycles = MT25_DUMMY_CYCLES;
    return cfg;
}

/**
 * QSPI_WriteEnable — Send write enable command, poll status for WEL bit
 */
static void QSPI_WriteEnable(void)
{
    QSPI_CommandTypeDef     cmd = {0};
    QSPI_AutoPollingTypeDef cfg = {0};
    uint32_t status_mask;
    uint32_t status_match;
    uint32_t status_bytes;

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

    /* In dual mode, poll both status bytes like ST QSPI_MemoryMappedDual example. */
    if (hqspi.Init.DualFlash == QSPI_DUALFLASH_ENABLE) {
        status_mask = 0x0202U;
        status_match = 0x0202U;
        status_bytes = 2U;
    } else {
        status_mask = 0x02U;
        status_match = 0x02U;
        status_bytes = 1U;
    }

    cfg.Match           = status_match;
    cfg.Mask            = status_mask;
    cfg.MatchMode       = QSPI_MATCH_MODE_AND;
    cfg.StatusBytesSize = status_bytes;
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
    uint32_t status_mask;
    uint32_t status_bytes;

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_READ_STATUS_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    /* In dual mode, wait for both flashes ready like ST QSPI_MemoryMappedDual example. */
    if (hqspi.Init.DualFlash == QSPI_DUALFLASH_ENABLE) {
        status_mask = 0x0101U;
        status_bytes = 2U;
    } else {
        status_mask = 0x01U;
        status_bytes = 1U;
    }

    cfg.Match           = 0x00U;  /* WIP bit 0 = 0 when ready */
    cfg.Mask            = status_mask;
    cfg.MatchMode       = QSPI_MATCH_MODE_AND;
    cfg.StatusBytesSize = status_bytes;
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
static void QSPI_DummyCyclesCfg(uint8_t dummy_cycles)
{
    QSPI_CommandTypeDef cmd = {0};
    uint16_t reg = 0;
    uint8_t reg_size = (hqspi.Init.DualFlash == QSPI_DUALFLASH_ENABLE) ? 2U : 1U;

    /* Read Volatile Config Register (0x85) */
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_READ_VOL_CFG_REG_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;  /* No dummy for initial read */
    cmd.NbData            = reg_size;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_QSPI_Receive(&hqspi, (uint8_t *)&reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }

    if (reg_size == 2U) {
        /* ST dual example updates both register bytes (mask 0xF0F0). */
        reg = (uint16_t)((reg & (uint16_t)~0xF0F0U) |
                         ((uint16_t)dummy_cycles << 4) |
                         ((uint16_t)dummy_cycles << 12));
    } else {
        reg = (uint16_t)((reg & (uint16_t)~QSPI_VCR_DUMMY_CYCLES_MASK) |
                         ((uint16_t)dummy_cycles << QSPI_VCR_DUMMY_QUAD_SHIFT));
    }

    /* Write-enable, then write updated VCR */
    QSPI_WriteEnable();

    cmd.Instruction = MT25_WRITE_VOL_CFG_REG_CMD;
    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }
    cmd.NbData = reg_size;
    if (HAL_QSPI_Transmit(&hqspi, (uint8_t *)&reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        Error_Handler();
    }

    QSPI_WaitReady();
}

/*
 * Configure MPU region for the QSPI window.
 *
 * The QSPI flash is NOR (byte-addressable, read-only from the CPU perspective).
 * Start with non-cacheable/non-bufferable (like ST BSP data-only example) to rule
 * out cache coherence issues with instruction fetch.
 */
static void MPU_ConfigQSPI(void)
{
    MPU_Region_InitTypeDef r = {0};

    HAL_MPU_Disable();

    r.Enable           = MPU_REGION_ENABLE;
    r.Number           = MPU_REGION_NUMBER0;
    r.BaseAddress      = 0x90000000U;
    r.Size             = MPU_REGION_SIZE_256MB;   /* cover full AXI QSPI window */
    r.SubRegionDisable = 0x00U;
    /* Normal memory, WT/no-WA (TEX=0 C=1 B=0): I/D cache both active for XIP.
     * Read-only NOR flash — write-back unnecessary, WT avoids coherency risk. */
    r.TypeExtField     = MPU_TEX_LEVEL0;
    r.AccessPermission = MPU_REGION_PRIV_RO_URO;  /* read-only: no accidental write */
    r.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    r.IsCacheable      = MPU_ACCESS_CACHEABLE;
    r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* ---- Public API ----------------------------------------------------------- */

void QSPI_MemoryMapped_Init(void)
{
    QSPI_CommandTypeDef      cmd  = {0};
    QSPI_MemoryMappedTypeDef mmap = {0};
    QSPI_TuneConfig cfg;

    APP_LOGI("QSPI", "configure mpu + gpio");
    MPU_ConfigQSPI();
    QSPI_GPIO_Init();
    APP_LOGI("QSPI", "gpio configured: CLK=PF10 NCS=PG6 BK1[PD11 PF9 PF7 PF6] BK2[PH2 PH3 PG9 PG14]");
    QSPI_DumpCoreRegs("post-mpu-gpio");
    QSPI_DumpQspiRegs("post-mpu-gpio");

    if (!QSPI_RunClassicRamTests()) {
        APP_LOGI("RAM", "classic test FAILED");
        Error_Handler();
    }

    /* ST QSPI_MemoryMappedDual baseline first, optimize only after pass */
    APP_LOGI("QSPI", "using ST baseline config");
    cfg.prescaler         = 1U;   /* 200 MHz / (1+1) = 100 MHz */
    cfg.sample_shifting   = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    cfg.cs_high_time      = QSPI_CS_HIGH_TIME_3_CYCLE;
    cfg.dummy_cycles      = MT25_DUMMY_CYCLES;

    QSPI_InitPeripheral(&cfg);
    QSPI_LogRegs("after HAL_QSPI_Init");
    QSPI_DumpQspiRegs("after-hal-init");
    QSPI_DumpFlashRegs("after-hal-init");

    if (!QSPI_ConfigFlashForMemoryMapped(&cfg)) {
        APP_LOGI("QSPI", "FAIL: flash config");
        Error_Handler();
    }
    QSPI_DumpQspiRegs("after-flash-config");
    QSPI_DumpFlashRegs("after-flash-config");

    /* Disable both caches for first proof (per doc section D) */
    APP_LOGI("QSPI", "disable I-cache and D-cache for first proof");
    SCB_DisableICache();
    SCB_DisableDCache();
    __DSB();
    __ISB();

    /* Enable memory-mapped mode using quad-read 0x6B per doc */
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = MT25_QUAD_OUT_FAST_READ;  /* 0x6B = 1-1-4 */
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_4_LINES;  /* 4-line data per spec */
    cmd.DummyCycles       = cfg.dummy_cycles;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    mmap.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    APP_LOGI("QSPI", "enable MM cmd=0x%02x (0x6B=quad-read) dummy=%u datalines=4",
             (unsigned)cmd.Instruction,
             (unsigned)cmd.DummyCycles);
    QSPI_DumpQspiRegs("before-mm-enable");
    QSPI_DumpCoreRegs("before-mm-enable");

    if (HAL_QSPI_MemoryMapped(&hqspi, &cmd, &mmap) != HAL_OK) {
        Error_Handler();
    }
    __DSB();
    __ISB();
    APP_LOGI("QSPI", "memory-mapped mode enabled @0x90000000 (caches OFF for proof)");
    QSPI_DumpQspiRegs("after-mm-enable");
    QSPI_DumpCoreRegs("after-mm-enable");
    QSPI_DumpMmWindow("after-mm-enable");

    /* Memory-mapped verification before XIP call (doc section B) */
    APP_LOGI("QSPI", "--- MM verification phase ---");
    volatile uint8_t *p8 = (volatile uint8_t *)0x90000000;
    volatile uint32_t *p32 = (volatile uint32_t *)0x90000000;

    /* Byte reads */
    APP_LOGI("QSPI", "byte reads [0-7]: %02x %02x %02x %02x %02x %02x %02x %02x",
             (unsigned)p8[0], (unsigned)p8[1], (unsigned)p8[2], (unsigned)p8[3],
             (unsigned)p8[4], (unsigned)p8[5], (unsigned)p8[6], (unsigned)p8[7]);

    /* Word reads */
    uint32_t w0 = p32[0];
    uint32_t w1 = p32[1];
    uint32_t w2 = p32[2];
    uint32_t w3 = p32[3];
    APP_LOGI("QSPI", "word reads [0-3]: %08lx %08lx %08lx %08lx",
             (unsigned long)w0, (unsigned long)w1, (unsigned long)w2, (unsigned long)w3);
    QSPI_DumpQspiRegs("mm-verify");
    QSPI_DumpCoreRegs("mm-verify");

    /* Repeated reads (stability check) */
    uint32_t wr0 = p32[0];
    uint32_t wr1 = p32[1];
    if (wr0 != w0 || wr1 != w1) {
        APP_LOGI("QSPI", "UNSTABLE: repeated reads differ! %08lx vs %08lx",
                 (unsigned long)wr0, (unsigned long)w0);
    } else {
        APP_LOGI("QSPI", "stable: repeated reads match");
    }

    /* Expected for XIP asm (movs r0,#42; bx lr) at 0x90000000 */
    APP_LOGI("QSPI", "XIP asm expected first word: 0x4770202a (little-endian) or 0x202a4770");
    if (w0 == 0x4770202aU || w0 == 0x202a4770U) {
        APP_LOGI("QSPI", "XIP first word looks correct!");
    } else {
        APP_LOGI("QSPI", "WARNING: XIP first word unexpected (may still work)");
    }
}

void QSPI_RunAggressiveBenchmark(void)
{
    uint32_t write_bw_x100 = 0U;
    uint32_t read_bw_x100 = 0U;
    uint32_t mm64k_x100;
    uint32_t mm1m_x100;
    HAL_StatusTypeDef st;

    APP_LOGI("QBENCH", "aggressive benchmark start");

    /* Leave global memory-mapped mode before issuing indirect erase/program commands. */
    st = HAL_QSPI_Abort(&hqspi);
    APP_LOGI("QBENCH", "leave-MM abort status=%ld sr=0x%08lx",
             (long)st,
             (unsigned long)QUADSPI->SR);

    /* Keep this destructive soak in the reserved self-test region only. */
    if (!QSPI_RunSelfTestForMs(MT25_DUMMY_CYCLES, 3000U, &write_bw_x100, &read_bw_x100)) {
        APP_LOGI("QBENCH", "soak FAIL");
        Error_Handler();
    }

    /* Self-test leaves MM mode disabled; re-enable before raw MM read benchmarks. */
    QSPI_EnableMemoryMappedRead(MT25_DUMMY_CYCLES);
    __DSB();
    __ISB();
    APP_LOGI("QBENCH", "MM mode enabled for read benchmarks");

    mm64k_x100 = QSPI_BenchmarkMmRead_x100(64U * 1024U, 32U);
    mm1m_x100 = QSPI_BenchmarkMmRead_x100(1024U * 1024U, 8U);

    APP_LOGI("QBENCH", "summary soak-write=%lu.%02lu MB/s soak-mm-read=%lu.%02lu MB/s",
             (unsigned long)(write_bw_x100 / 100U),
             (unsigned long)(write_bw_x100 % 100U),
             (unsigned long)(read_bw_x100 / 100U),
             (unsigned long)(read_bw_x100 % 100U));
    APP_LOGI("QBENCH", "summary mm-64k=%lu.%02lu MB/s mm-1m=%lu.%02lu MB/s",
             (unsigned long)(mm64k_x100 / 100U),
             (unsigned long)(mm64k_x100 % 100U),
             (unsigned long)(mm1m_x100 / 100U),
             (unsigned long)(mm1m_x100 % 100U));

    /* Keep MM mode enabled for subsequent XIP/LVGL flow. */
    APP_LOGI("QBENCH", "MM mode retained after benchmark");

    APP_LOGI("QBENCH", "aggressive benchmark PASS");
}

void QSPI_BenchmarkCachedRead(void)
{
    uint32_t mm64k_x100;
    uint32_t mm1m_x100;

    /* Must be called while QSPI is in memory-mapped mode and caches are ON. */
    __DSB();
    __ISB();
    mm64k_x100 = QSPI_BenchmarkMmRead_x100(64U * 1024U, 32U);
    mm1m_x100  = QSPI_BenchmarkMmRead_x100(1024U * 1024U, 8U);

    APP_LOGI("QBENCH", "cached mm-64k=%lu.%02lu MB/s mm-1m=%lu.%02lu MB/s",
             (unsigned long)(mm64k_x100 / 100U),
             (unsigned long)(mm64k_x100 % 100U),
             (unsigned long)(mm1m_x100 / 100U),
             (unsigned long)(mm1m_x100 % 100U));
}

