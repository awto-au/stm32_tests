# STM32H750B-DK — LVGL 9.x + QSPI XIP Demo

Running LVGL 9.x on the STM32H750B Discovery Kit with the full LVGL feature set — all fonts, themes, and widgets — without fitting inside the 128 KB internal flash by executing LVGL directly from the onboard QSPI flash chip.

## What is running

| Component | Detail |
|---|---|
| MCU | STM32H750XBHx, Cortex-M7 @ 400 MHz |
| Display | 480×272 LCD via LTDC, double-buffered directly into AXI SRAM |
| LVGL | 9.x (main branch), `lv_st_ltdc` direct-buffer driver |
| RTOS | FreeRTOS-Kernel, heap_4, 96 KB heap in DTCM |
| External flash | Micron MT25TL01G 1 Gbit QSPI NOR, Bank 1 @ 0x90000000 |
| Build system | CMake 3.22 + Ninja, arm-none-eabi-gcc 15.2 |

### Memory layout

```
Region          Start        Size       Content
─────────────────────────────────────────────────────────
Internal FLASH  0x08000000   128 KB     Boot stub: vectors, startup,
                                        HAL, FreeRTOS, app code
EXTFLASH (QSPI) 0x90000000   64 MB      LVGL library (text + rodata)
DTCM SRAM       0x20000000   128 KB     FreeRTOS heap (96 KB)
AXI SRAM        0x24000000   512 KB     Two 480×272 RGB565 framebuffers
                                        (255 KB each, DMA-accessible)
```

After the full LVGL restore the sizes are:

- Internal flash used: **~20 KB** of 128 KB
- EXTFLASH used: **~273 KB** of 64 MB (64 MB remaining for assets)

### How QSPI XIP works

1. `QSPI_MemoryMapped_Init()` runs in `main()` before the scheduler, immediately after the clock is up.
2. It resets the MT25TL01G, switches to 4-byte address mode, then puts the QUADSPI peripheral into memory-mapped mode using **Quad IO Fast Read (opcode 0xEB)** at 100 MHz (AHB3 200 MHz ÷ 2) for 6× small-read throughput vs legacy 0x6B mode.
3. The MPU is configured with a 64 MB Normal-memory, cacheable, write-through region at `0x90000000` so the I-cache and D-cache work correctly.
4. From that point on, the CPU fetches LVGL instructions transparently from QSPI as if it were internal flash — no software copy step.

### HAL tick under FreeRTOS

This project follows the STM32CubeH7 FreeRTOS application pattern: **HAL timekeeping runs from `TIM6`, while SysTick is reserved for the FreeRTOS kernel**.

- `HAL_Delay()` and HAL timeout polling use `uwTick`, incremented by `TIM6_DAC_IRQHandler()` via `HAL_IncTick()`.
- `SysTick_Handler` is owned by the FreeRTOS port (`xPortSysTickHandler`) and is not used as the HAL timebase.
- This avoids the common pre-scheduler deadlock where `HAL_Delay()` is called before the scheduler starts but the active SysTick handler only services the RTOS tick.

The implementation lives in `Src/stm32h7xx_hal_timebase_tim.c` and is adapted from the STM32CubeH7 FreeRTOS examples for STM32H750B-DK.

### Linker split

The CMake build generates a linker script from `stm32h750xb_flash.ld.in` using `configure_file()`. The generated script uses `EXCLUDE_FILE(lvgl/lib/liblvgl.a)` in the internal FLASH `.text`/`.rodata` sections to keep LVGL out of internal flash, then claims LVGL with:

```ld
.extflash 0x90000000 :
{
    lvgl/lib/liblvgl.a:*(.text .text*)
    lvgl/lib/liblvgl.a:*(.rodata .rodata*)
} >EXTFLASH
```

No LVGL source changes are needed.

## Source References

### Recommended Approach: Clone from GitHub

This project uses patterns directly from **STM32CubeH7 v1.13.0** (Feb 2026 release), which includes:

- **FreeRTOS Kernel v10.6.2** (latest stable with improved tick handling)
- **CMSIS-RTOS V2** support (modern real-time kernel API)
- **Improved timer configuration** (configUSE_TIMERS enabled by default)
- **Verified H7 HAL patterns** (all tested on STM32H750B-DK hardware)

**Always clone from GitHub** — the official maintained source:

```bash
git clone --depth=1 https://github.com/STMicroelectronics/STM32CubeH7.git
```

Do NOT download ZIP files from ST's website; they become outdated and duplicate the GitHub repository.

### FreeRTOS Configuration

Key settings adapted from STM32CubeH7 v1.13.0 [FreeRTOS_ThreadCreation example](https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H750B-DK/Applications/FreeRTOS):

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `configTICK_RATE_HZ` | 1000 | 1 ms RTOS tick |
| `configTOTAL_HEAP_SIZE` | 96 KB | DTCM allocation |
| `configMAX_PRIORITIES` | 56 | Modern default |
| `configUSE_TIMERS` | 1 | Software timers enabled |
| `configUSE_OS2_THREAD_*` | 1 | CMSIS-RTOS V2 support |

**Note on CubeMX2-Generated Code**: STM32CubeMX 2.x (IDE-based project generator) may generate newer FreeRTOS patterns with different APIs (e.g., HAL v2.x naming conventions, RTOS v11.x pre-release features). If you regenerate this project via CubeMX2:
- Compare the generated `FreeRTOSConfig.h` against the reference (v1.13.0 above)
- Verify `stm32h7xx_hal_timebase_tim.c` uses TIM6 (required for pre-scheduler HAL_Delay compatibility)
- Test early-boot QSPI initialization carefully (timing dependencies are critical)

### Building

### Prerequisites

Clone these outside the repo (paths are hard-coded in `CMakeLists.txt`):

```sh
# STM32 HAL + CMSIS (always from GitHub, master branch)
mkdir -p ~/STM32Cube/Repository
git clone --depth=1 https://github.com/STMicroelectronics/STM32CubeH7.git \
    ~/STM32Cube/Repository/STM32Cube_FW_H7
cd ~/STM32Cube/Repository/STM32Cube_FW_H7
git submodule update --init --depth=1 \
    Drivers/STM32H7xx_HAL_Driver \
    Drivers/CMSIS/Device/ST/STM32H7xx

# LVGL (from GitHub)
mkdir -p ~/Downloads/git
git clone --depth=1 https://github.com/lvgl/lvgl.git ~/Downloads/git/lvgl

# FreeRTOS (from GitHub)
git clone --depth=1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git \
    ~/Downloads/git/FreeRTOS-Kernel
```

Toolchain: `arm-none-eabi-gcc` must be on `PATH`.

### Configure and build

```sh
cd code/stm32h750-lvgl
cmake --preset Release
cmake --build build/Release -j$(nproc)
```

Outputs: `build/Release/stm32h750_lvgl.elf.elf`

## Flashing

Uses STM32CubeProgrammer with the MT25TL01G external loader so both the internal flash region and the QSPI region are programmed from the single ELF in one command.

```sh
PROG=/opt/st/stm32cubeide_1.18.0/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.200.202503041107/tools/bin/STM32_Programmer_CLI
EL=/opt/st/stm32cubeide_1.18.0/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.200.202503041107/tools/bin/ExternalLoader/MT25TL01G_STM32H750B-DISCO.stldr

$PROG -c port=SWD sn=003400223137510E33333639 \
      -el "$EL" \
      -w build/Release/stm32h750_lvgl.elf.elf \
      -v -rst
```

Replace the serial number (`sn=`) with your board's SN from `STM32_Programmer_CLI -c port=SWD -List`.

The `-el` external loader flag is **required** — without it the programmer cannot erase or write the QSPI region.

## LVGL configuration highlights (`lv_conf.h`)

Settings that differ from the LVGL default and must be preserved:

| Setting | Value | Reason |
|---|---|---|
| `LV_COLOR_DEPTH` | 16 | RGB565 display |
| `LV_USE_OS` | `LV_OS_FREERTOS` | FreeRTOS threading |
| `LV_USE_STDLIB_MALLOC` | `LV_STDLIB_CLIB` | Uses system `malloc` (newlib/FreeRTOS), avoids 64 KB static TLSF pool |
| `LV_USE_ST_LTDC` | 1 | LTDC direct-buffer driver |
| `LV_DRAW_BUF_ALIGN` | 32 | 32-byte cache-line alignment for DMA |
| `LV_USE_FREERTOS_TASK_NOTIFY` | 1 | Task notifications instead of semaphores |
| `LV_DISABLE_API_MAPPING` | — | Set as CMake compile definition, **not** in `lv_conf.h`; prevents LVGL v8 compatibility header errors |

Everything else — all Montserrat fonts (8–48 pt), SW complex renderer, arc, themes, all colour format decoders — is enabled at reference-design defaults.

## Source layout

```
code/stm32h750-lvgl/
├── CMakeLists.txt              Build definition
├── CMakePresets.json           Debug / Release presets
├── lv_conf.h                   LVGL configuration
├── stm32h750xb_flash.ld.in     Linker script template (CMake substitutes archive path)
├── Inc/
│   ├── main.h
│   ├── ltdc_init.h
│   ├── lvgl_port.h
│   ├── ui.h
│   ├── qspi_init.h             QSPI XIP init declaration
│   ├── FreeRTOSConfig.h
│   └── stm32h7xx_hal_conf.h
└── Src/
    ├── main.c                  Entry point, clock, QSPI init, LTDC init, scheduler
    ├── qspi_init.c             QUADSPI peripheral + MT25TL01G memory-mapped init
    ├── stm32h7xx_hal_timebase_tim.c  HAL 1 ms tick on TIM6 for FreeRTOS compatibility
    ├── ltdc_init.c             LTDC layer setup (480×272 RGB565)
    ├── lvgl_port.c             lv_st_ltdc registration, LVGL FreeRTOS task
    ├── ui.c                    Demo UI: arc widget, multi-font labels
    ├── stm32h7xx_it.c          Interrupt handlers (SysTick owned by FreeRTOS)
    └── system_stm32h7xx.c      SystemInit
```
