# STM32H750B-DK LVGL Display & FreeRTOS Application

LVGL 9.x graphical demo on STM32H750B-DK (Cortex-M7 @ 400 MHz) using FreeRTOS Kernel with ST's HAL and proven reference patterns.

## Hardware

- **Processor**: STM32H750XBH6 (Cortex-M7 @ 400 MHz, 128 KB internal flash, 512 KB AXI SRAM)
- **Display**: 4.3" RGB-I (LTDC) with capacitive touch panel
- **External Storage**: 64 MB Micron MT25TL01G Quad-SPI NOR flash (Bank 1)
- **RTOS**: FreeRTOS Kernel (heap_4, 96 KB DTCM, 1 kHz tick)
- **UI Framework**: LVGL 9.x with st_ltdc direct-buffer driver

**Board Reference**: [STMicroelectronics STM32H750B-DK Discovery Kit](https://www.st.com/en/evaluation-tools/stm32h750b-dk.html)  
**CubeH7 Examples**: https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H750B-DK

### Optional Board Features (See Sub-Issues)
The STM32H750B-DK includes several peripherals with maintained ST reference implementations:
- **Ethernet 10/100 Mbit/s** (IEEE 802.3-2002)
- **USB OTG FS**
- **SAI Audio Codec** with stereo DAC and MEMS microphone
- **eMMC Storage** (4 GB)
- **SDRAM** (128 Mbit)

## Architecture

### Tick Source (STM32H7xx HAL ↔ FreeRTOS)

**Problem Solved**: Early-boot HAL_Delay() hangs when SysTick is owned by FreeRTOS before scheduler start.

**Solution** (ST CubeH7 Pattern):
- **HAL Timebase**: TIM6 @ 1 MHz (1 ms overflow) → `HAL_IncTick()` in `HAL_TIM_PeriodElapsedCallback()`
- **SysTick**: Owned by FreeRTOS (mapped in `FreeRTOSConfig.h` → `port.c` xPortSysTickHandler)
- **Result**: HAL functions work correctly during pre-scheduler boot and under RTOS

**Implementation**:
- [stm32h7xx_hal_timebase_tim.c](code/stm32h750-lvgl/Src/stm32h7xx_hal_timebase_tim.c) — TIM6-based HAL tick
- [FreeRTOSConfig.h](code/stm32h750-lvgl/Inc/FreeRTOSConfig.h#L55-L60) — SysTick to xPortSysTickHandler mapping

**Reference**: [ST CubeH7 FreeRTOS Examples](https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H750B-DK/Applications/FreeRTOS)

### QSPI Initialization

**Adapted from ST Reference**: Uses interrupt-based auto-polling instead of blocking timeouts, eliminating early-boot hangs.

**Key Functions**:
- `QSPI_WriteEnable()` — Auto-poll WEL bit with interrupt
- `QSPI_WaitReady()` — Auto-poll WIP bit with interrupt  
- `QSPI_DummyCyclesCfg()` — Read/write volatile config register for dummy cycles

**Configuration**:
- Quad-Output Fast Read (0x6B): 1-1-4 mode, 8 dummy cycles, 100 MHz clock
- 4-byte address mode (required for 64 MB)
- MPU: 64 MB cacheable, write-through, XIP-enabled

**ST Reference Implementation**:  
https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H750B-DK/Examples/QSPI/QSPI_MemoryMappedDual

### System Clock

- **Source**: HSE (25 MHz) → PLL1 @ 400 MHz (CPU), 200 MHz (AXI/AHBs)
- **Configuration**: PLLM=5, PLLN=160, PLLP=2, PLLQ=4, PLLR=2
- **Reference**: [code/stm32h750-lvgl/Src/main.c](code/stm32h750-lvgl/Src/main.c#L80-L105)

## Build & Flash

### Prerequisites
```bash
# Clone dependencies (adjust paths in CMakeLists.txt if needed)
mkdir -p ~/Downloads/git
cd ~/Downloads/git
git clone --depth=1 https://github.com/lvgl/lvgl.git
git clone --depth=1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git
```

### Build
```bash
cd code/stm32h750-lvgl
mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ..
ninja
# Output: build/stm32h750_lvgl.elf.elf (~388 KB Debug)
```

### Flash
```bash
# Requires: STM32 Programmer CLI + MT25TL01G external loader (included with STM32CubeIDE)
STM32_Programmer_CLI \
  -c port=SWD sn=<SERIAL> \
  -el MT25TL01G_STM32H750B-DISCO.stldr \
  -w build/stm32h750_lvgl.elf.elf -v -rst
```

**Debug**: OpenOCD + arm-none-eabi-gdb (Cortex-Debug extension in VS Code)

## Development Workflow

1. **QSPI Initialization** → Memory-mapped mode enables XIP from 0x90000000
2. **LTDC Driver** → Direct framebuffer in AXI SRAM (no extra copy)
3. **LVGL Port** → FreeRTOS task calls lv_timer_handler every 1 ms
4. **UI Demo** → Crosshairs, colors, title, progress arc

## Testing  

**Expected Output** (if display works):
- Green LED ON immediately after boot
- Test pattern displays: crosshairs (white), title (blue/yellow text), progress bar (green)

**If Blank Screen**:
1. Verify QSPI initialized: check GDB breakpoint at `QSPI_MemoryMapped_Init()` completion
2. Verify LTDC clock: `__HAL_RCC_LTDC_CLK_ENABLE()` called
3. Check MPU region 0x90000000–0x93FFFFFF is cacheable + XIP-enabled

## Known Issues & Future Work

### FreeRTOS Tick Architecture
- ✅ HAL/RTOS tick collision resolved (TIM6 + SysTick separation)
- ✅ Early-boot initialization working
- ❓ Consider consolidating with ST's maintained templates for new projects

### QSPI Implementation  
- ✅ Interrupt-based polling implemented (no early hangs)
- ⏳ **TODO**: Compare with ST's BSP-layer QSPI helpers (if available)
- ⏳ **TODO**: Profile dummy cycle read performance vs. datasheet guarantees

### Board Peripherals (Sub-Issues)
- [ ] Ethernet: Evaluate LwIP + PHY driver
- [ ] USB OTG FS: Mass storage or CDC device class
- [ ] SAI Audio: MEMS mic capture + DAC output demo
- [ ] eMMC: SD/EMMC driver integration
- [ ] SDRAM: Use for extended heap or frame buffering

## References

**Official ST Resources**:
- [STM32H750B-DK Product Page](https://www.st.com/en/evaluation-tools/stm32h750b-dk.html)
- [STM32CubeH7 Repository](https://github.com/STMicroelectronics/STM32CubeH7) — Complete examples & HAL
- [UM2488 STM32H750B-DK User Manual](https://www.st.com/resource/en/user_manual/um2488-discovery-kit-with-stm32h750-mcu-stmicroelectronics.pdf)
- [STM32H750 Datasheet](https://www.st.com/resource/en/datasheet/stm32h750ib.pdf)

**External Libraries**:
- [LVGL Documentation](https://docs.lvgl.io/)
- [FreeRTOS Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel)
- [ARM CMSIS](https://github.com/ARM-software/CMSIS_5)

**Related ST Examples**:
- [QSPI Memory-Mapped Dual Bank](https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H750B-DK/Examples/QSPI/QSPI_MemoryMappedDual)
- [FreeRTOS Thread Creation](https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H750B-DK/Applications/FreeRTOS/FreeRTOS_ThreadCreation)
- [LTDC Color LCD](https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H750B-DK/Examples/LTDC)

## Contributing

When adding new features:
1. **Check ST's Examples First**: Reference implementations exist for most peripherals at `STM32CubeH7/Projects/STM32H750B-DK/`
2. **Use Interrupt-Based Drivers**: Avoid blocking HAL calls during critical initialization
3. **Document Tick Dependencies**: Any new code using HAL timeouts should work pre- and post-scheduler

---

*Last Updated: May 6, 2026*  
*Maintained Against*: STM32CubeH7 (master), FreeRTOS-Kernel, LVGL 9.x
