/**
 * @file qspi_init.h
 * QUADSPI memory-mapped mode init for MT25TL01G (Bank 1, 64 MB @ 0x90000000)
 * STM32H750B-DK pin mapping:
 *   CLK=PF10  NCS=PG6  IO0=PD11  IO1=PF9  IO2=PF7  IO3=PF6
 */
#ifndef QSPI_INIT_H
#define QSPI_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the QUADSPI peripheral for XIP (execute-in-place) memory-mapped
 * read from a Micron MT25TL01G (single bank, 64 MB).
 *
 * After this call the entire 64 MB window at 0x90000000–0x93FFFFFF is
 * accessible to the CPU and DMA as ordinary read-only memory.
 *
 * Must be called from internal flash, before any code linked to EXTFLASH
 * is invoked.
 */
void QSPI_MemoryMapped_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* QSPI_INIT_H */
