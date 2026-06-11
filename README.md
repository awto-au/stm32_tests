# stm32_tests

STM32 development-board test and bring-up projects.

## Projects

| Directory | Board | Stack | Status |
|---|---|---|---|
| [`stm32h750b-dk-mb1381/hal-freertos-lvgl`](stm32h750b-dk-mb1381/hal-freertos-lvgl/) | STM32H750B-DK (MB1381) | STM32 HAL + FreeRTOS + LVGL 9.x, QSPI XIP | Active — LVGL OOM debug in progress |

### stm32h750b-dk-mb1381/hal-freertos-lvgl

Runs full-featured LVGL 9.x (all fonts, widgets, themes — ~273 KB) on a part with
only 128 KB internal flash, by executing the LVGL library directly from the
board's MT25TL01G QSPI NOR flash via memory-mapped XIP. Highlights:

- Linker-script split: `EXCLUDE_FILE(liblvgl.a)` keeps LVGL out of internal
  flash; CMake `configure_file()` injects the archive path to claim its
  text/rodata into the QSPI region. No LVGL source changes.
- Empirical MT25TL01G dual-die QSPI notes (0xEB 1-4-4 mode-byte timing quirk,
  measured bandwidth figures) — see git history of `Src/qspi_init.c`.
- TIM6 HAL timebase with SysTick reserved for FreeRTOS.
- `tools/flash.py`: auto-detects STM32CubeProgrammer, flashes internal +
  QSPI regions from one ELF, and captures the boot log over the ST-Link VCP.

See the project [README](stm32h750b-dk-mb1381/hal-freertos-lvgl/README.md) for
build and flash instructions, and
[docs-architecture.md](stm32h750b-dk-mb1381/hal-freertos-lvgl/docs-architecture.md)
for the original architecture write-up (tick source, QSPI init, clock tree).

### stm32h750b-dk-mb1381/board-cad

MB1381B board design files for the STM32H750B-DK:

- `MB1381B/Altium_Designer/` — ST's published Altium source plus KiCad
  conversions of the PCB and all schematic sheets.
- `mb1381-bdp/`, `mb1381-bom/`, `mb1381-manufacturing/` — ST board design
  package, BOM, and manufacturing outputs (extracted from st.com downloads).
- `mb1381-h750xb-b01-schematic.pdf`, `mb1381-h750xb-b04.pdf` — schematic PDFs.

### stm32h750b-dk-mb1381/docs

- [`qspi_xip_zephyr_notes.md`](stm32h750b-dk-mb1381/docs/qspi_xip_zephyr_notes.md)
  — notes on a Zephyr-based QSPI XIP approach for the same board.

## Licensing

Mixed, per file:

- Files derived from STMicroelectronics sources (startup code,
  `system_stm32h7xx.c`, HAL configuration headers, linker script skeleton)
  remain under their original ST/BSD-3-Clause terms — see per-file headers.
- Files derived from LVGL (`lv_conf.h` template) remain under the MIT license.
- FreeRTOS, LVGL, and the STM32 HAL themselves are external dependencies
  (cloned separately, not vendored here) under their own licenses.
- Board design files under `stm32h750b-dk-mb1381/board-cad/` are
  STMicroelectronics material published for the STM32H750B-DK and remain
  ST's property under the terms accompanying the original st.com downloads;
  the KiCad conversions are derived from that material.
- Original project code carries no separate license grant unless stated in
  the file.
