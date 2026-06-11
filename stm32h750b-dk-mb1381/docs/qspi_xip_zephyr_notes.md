# STM32H750B-DK QSPI / XIP / Zephyr Investigation Notes

## Context

This note collects the key points from the chat about debugging XIP from external QSPI flash on an STM32H750B-DK / STM32H750-class board.

The central issue is:

- Indirect QSPI erase/program/read tests pass.
- Memory-mapped QSPI/XIP execution fails immediately.
- A tiny function placed at `0x90000000` appears to contain valid Thumb instructions, but calling it hardfaults.
- This strongly suggests a problem in the memory-mapped QSPI/XIP path, not the basic indirect command path.

---

## Hardware We Are On

### MCU / board

- Board: **STM32H750B-DK**.
- MCU class: **STM32H750 / STM32H7 Cortex-M7**.
- Internal flash is small for this class of application, so external memory use is important.

### External flash

- External flash type: **QSPI NOR**, not NAND.
- Device family discussed: **Micron MT25TL01G / dual-QSPI NOR arrangement**.
- Memory-mapped base address: `0x90000000`.
- STM32H750B-DK has a dual-QSPI NOR setup, effectively **2 × 512 Mbit Quad-SPI NOR flash**.
- This is not the same as a simple single-QSPI NOR part on one chip-select.

### Other board memory

- The board also has eMMC, but for this specific debugging thread eMMC is not the focus.
- eMMC is block storage, not memory-mapped executable memory.
- For XIP and directly addressed assets, QSPI NOR is the relevant device.

---

## Current Project Locations

### QSPI init path

The QSPI initialisation path is in:

```text
qspi_init.c
```

### Explicit XIP test call

The explicit assembly XIP test call is in:

```text
main.c
```

### Clock / internal flash latency

Internal flash wait states are configured separately in the clock setup.

Known setting mentioned:

```c
FLASH_LATENCY_4
```

This is in `main.c` around line 151.

Important: this setting affects internal flash timing, not the QSPI NOR memory-mapped timing directly.

---

## QSPI / XIP Settings Currently Being Run

### Memory-mapped read opcode

The current memory-mapped read opcode is:

```text
0x6B: Quad Output Fast Read
```

Current bus format:

```text
Instruction: 1-line
Address:     1-line, 32-bit address
Data:        4-line
```

### Timing knobs being swept

The timing matrix currently sweeps:

- QSPI prescaler
- sample shifting
- chip-select high time
- dummy cycles

### Current stable candidate selected by logs

Current candidate from the matrix logs:

```text
prescaler = p=2, about 66 MHz
sample    = half-cycle
CS high   = enum value 1280, interpreted as 6-cycle setting
dummy     = 10
```

This candidate appears stable for the existing indirect self-test, but not for XIP.

### MPU region for QSPI window

Current MPU region for the QSPI window:

```text
Base:       0x90000000
Executable: yes, instruction fetch allowed
Cacheable:  no
Bufferable: no
```

This is a reasonable starting point, but the Zephyr history suggests the exact memory attributes matter. Execute permission alone is not enough.

---

## Where It Fails

A minimal function was placed in the external flash section:

```text
.extflash
```

The function is located at the start of the external QSPI mapped region:

```text
0x90000000
```

Disassembly / byte inspection confirms apparently valid code at that location:

```asm
movs r0, #42
bx lr
```

The expected first 32-bit word for those Thumb instructions, in little-endian memory, is likely:

```text
0x4770202A
```

The call target is logged approximately as:

```text
0x90000001
```

That is correct for calling Thumb code because bit 0 of the function pointer must be set.

Runtime failure:

```text
HardFault immediately on XIP call
Fault PC around 0x90000004
Fault decode includes UNDEFINSTR and IMPRECISERR
```

Interpretation:

- The CPU starts fetching from the external memory window.
- It makes it to, or near, the first couple of halfwords.
- Then it sees an invalid instruction or bus fault during instruction fetch / prefetch / AHB access.
- This strongly suggests the memory-mapped QSPI read stream is not reliably returning correct instruction data under instruction-fetch conditions.

---

## Why the Memory Test Passes but XIP Fails

The current memory test mostly verifies the indirect command path:

```text
erase/program/read using explicit QSPI commands
0x20 sector erase
0x02 page program
0x03 read
```

That proves:

- the flash can be erased,
- the flash can be programmed,
- the flash can be read back via the indirect command engine,
- basic signal integrity and addressing are not completely broken.

But XIP uses a different path:

```text
AHB memory-mapped QSPI engine
read opcode 0x6B
configured dummy cycles
instruction fetch / prefetch / burst behaviour
MPU attributes
cache interactions
```

So this can happen:

```text
Indirect self-test: PASS
Memory-mapped/XIP: FAIL
```

That is exactly what the current logs show.

Important distinction:

```text
QSPI indirect stable != QSPI memory-mapped data stable != QSPI XIP stable
```

The matrix should not mark a candidate as fully stable until all three are tested.

---

## Important ST Reference Note

The local ST `QSPI_MemoryMappedDual` example that was checked is primarily a **data memory-mapped demo**, not necessarily a proof of code execution from external flash.

Its MPU example disables execution on the QSPI region. That is correct for data-only memory-mapped tests, but it does not prove XIP.

Therefore:

```text
Matching the ST data-memory-mapped example is not enough to prove executable XIP.
```

For XIP, the MPU must permit instruction fetch, and the memory type/cacheability/shareability attributes still need to be valid for the STM32H7 bus/cache architecture.

---

## Known Issue Class: STM32H750B-DK + Zephyr + Dual-QSPI XIP

The Zephyr history is directly relevant.

### Zephyr bug: STM32H7 cannot XIP from dual quad NOR in memory-mapped mode

A known Zephyr issue existed around STM32H7 and XIP from dual quad NOR in memory-mapped mode.

The reported symptoms were very similar:

- external NOR mapped at `0x90000000`,
- code placed in external flash,
- instruction/bus fault when executing from that region,
- fault PC near `0x90000000`.

The root issue in Zephyr was that the STM32 QSPI driver did not correctly set dual-flash mode / DFM for memory-mapped dual-quad NOR.

A Zephyr PR added support for the DFM bit in memory-mapped mode, allowing two external quad-NOR memories to be activated simultaneously.

### Why this matters here

STM32H750B-DK is a dual-QSPI board. If the project accidentally treats the flash as a single ordinary NOR chip, the indirect path can still appear partially or mostly sane while the memory-mapped/XIP path fails badly.

Likely failure class:

```text
DualFlash / FlashID / FlashSize / MPU attribute mismatch
```

Rather than only:

```text
dummy-cycle timing issue
```

Dummy-cycle sweeps are useful, but if dual-flash mode or the MPU attributes are wrong, timing sweeps can produce misleading “almost works” results.

---

## Zephyr Board Support Notes

Zephyr has board support for:

```text
stm32h750b_dk
```

The current Zephyr board documentation includes an external flash application target/variant along the lines of:

```text
stm32h750b_dk/stm32h750xx/ext_flash_app
```

The key point is that upstream Zephyr expects this board to be capable of external QSPI app execution, but it needed specific support for dual-QSPI memory-mapped mode and MPU/memmap setup.

Relevant Zephyr configuration clues mentioned in the discussion:

```text
CONFIG_STM32_MEMMAP=y
```

and an EXTMEM MPU-region redefinition using an MPU attribute style equivalent to external memory / IO mapping.

This suggests that in a bare-metal / Cube project, simply marking the region executable may not be enough. The exact MPU memory type and attributes should be tested carefully.

---

## LVGL and External QSPI on STM32H750B-DK

There has also been a Zephyr issue around LVGL apps and external QSPI flash on STM32H750B-DK.

The important part for this project:

- LVGL from external QSPI has been specifically troublesome in Zephyr.
- Some non-LVGL external-flash apps worked while LVGL samples failed.
- However, the current bare-metal tiny-ASM failure is more fundamental than LVGL.

Therefore, before worrying about LVGL, prove:

```text
minimal XIP function returns 42
branch-heavy XIP function works
literal-load XIP function works
small C function works
then larger code / LVGL-related code
```

Do not try to debug LVGL external execution until the tiny XIP gate is stable.

---

## ST BSP Settings Worth Comparing Against

The STM32H750B-DK BSP QSPI driver should be used as a comparison reference.

Critical settings to compare against:

```c
qspi_init.DualFlashMode = QSPI_DUALFLASH_ENABLE;
```

Also check:

```c
hQspi->Init.FifoThreshold = 1;
hQspi->Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_4_CYCLE;
hQspi->Init.ClockMode = QSPI_CLOCK_MODE_0;
hQspi->Init.FlashID = QSPI_FLASH_ID_1;
hQspi->Init.DualFlash = Config->DualFlashMode;
```

ST BSP behaviour also includes:

- setting 4-byte address mode,
- configuring dummy cycles in the flash device,
- configuring QSPI mode,
- selecting sample shifting based on transfer rate.

Key warning:

It is not enough to set dummy cycles only in the STM32 QSPI memory-mapped command. The Micron flash device itself also has latency/dummy-cycle configuration registers. ST’s BSP does a dummy-cycle configuration step before enabling the intended mode.

---

## Most Likely Root Causes to Check

Prioritised list:

1. **DualFlash mode not enabled or mismatched**
2. **Wrong FlashID / wrong dual-flash interleaving assumption**
3. **Wrong FlashSize for the combined dual-flash device**
4. **Only one flash die/device placed into correct 4-byte address / dummy-cycle / QE state**
5. **Memory-mapped read command not correct for the configured dual-flash mode**
6. **MPU attributes not suitable for instruction fetch from QSPI window**
7. **I-cache / prefetch / speculative fetch interaction**
8. **Dummy cycles too low for XIP burst/instruction-fetch behaviour**
9. **QSPI clock too fast for the current sampling mode**
10. **Less likely: actual board hardware fault**

---

## Direct Checks to Print Before Enabling Memory-Mapped Mode

Add debug prints immediately before enabling memory-mapped QSPI:

```c
printf("QSPI DualFlash=%lu\r\n", hqspi.Init.DualFlash);
printf("QSPI FlashID=%lu\r\n", hqspi.Init.FlashID);
printf("QSPI FlashSize=%lu\r\n", hqspi.Init.FlashSize);
printf("QSPI Prescaler=%lu\r\n", hqspi.Init.ClockPrescaler);
printf("QSPI SampleShift=%lu\r\n", hqspi.Init.SampleShifting);
printf("QSPI CSHT=%lu\r\n", hqspi.Init.ChipSelectHighTime);
printf("QSPI ClockMode=%lu\r\n", hqspi.Init.ClockMode);
```

Expected direction for this board:

```text
DualFlash   = ENABLE
FlashID     = QSPI_FLASH_ID_1
ClockMode   = MODE_0
CSHT        = at least 4 cycles initially, maybe more while debugging
SampleShift = half-cycle for STR mode
Addressing  = 4-byte address mode
Dummy config = configured both in STM32 command and flash device registers
```

---

## Add Memory-Mapped Verification Before XIP

The current matrix should not rely only on indirect `0x03` readback.

After enabling QSPI memory-mapped mode, do direct CPU loads from `0x90000000`.

### Byte verification

```c
static int qspi_mmap_verify(uint32_t addr, const uint8_t *expected, size_t len)
{
    volatile const uint8_t *p = (volatile const uint8_t *)addr;

    for (size_t i = 0; i < len; i++) {
        uint8_t v = p[i];
        if (v != expected[i]) {
            printf("MMAP VERIFY FAIL @ +0x%08lx: got 0x%02x expected 0x%02x\r\n",
                   (unsigned long)i, v, expected[i]);
            return -1;
        }
    }

    printf("MMAP VERIFY PASS len=%lu\r\n", (unsigned long)len);
    return 0;
}
```

### Word verification

Instruction fetch is closer to aligned halfword/word/burst behaviour than byte-by-byte reads, so also test aligned 32-bit loads:

```c
static int qspi_mmap_verify_words(uint32_t addr, const uint32_t *expected, size_t words)
{
    volatile const uint32_t *p = (volatile const uint32_t *)addr;

    for (size_t i = 0; i < words; i++) {
        uint32_t v = p[i];
        if (v != expected[i]) {
            printf("MMAP WORD FAIL @ +0x%08lx: got 0x%08lx expected 0x%08lx\r\n",
                   (unsigned long)(i * 4),
                   (unsigned long)v,
                   (unsigned long)expected[i]);
            return -1;
        }
    }

    printf("MMAP WORD VERIFY PASS words=%lu\r\n", (unsigned long)words);
    return 0;
}
```

### First-word XIP sanity check

Before calling the function:

```c
volatile uint32_t w = *(volatile uint32_t *)0x90000000;
printf("XIP first word = 0x%08lx\r\n", (unsigned long)w);
```

Expected for:

```asm
movs r0, #42
bx lr
```

is likely:

```text
0x4770202A
```

If this sometimes reads anything else, XIP cannot work reliably.

---

## Make the Matrix Pass Criteria Stricter

The current matrix effectively says:

```text
indirect erase/program/read PASS
```

That should become:

```text
indirect program/read PASS
memory-mapped byte read PASS
memory-mapped halfword/word read PASS
repeated memory-mapped read PASS
asm XIP call PASS
```

Only after all of those pass should a candidate be marked stable.

Suggested candidate test order:

```text
for each qspi timing config:
    qspi_deinit()
    qspi_init_with_candidate()

    indirect_erase_program_verify()
    if fail: continue

    qspi_enable_memory_mapped()

    mmap_byte_verify()
    if fail: continue

    mmap_word_verify()
    if fail: continue

    repeated_mmap_verify_loop()
    if fail: continue

    asm_xip_call()
    if hardfault: candidate rejected

    candidate stable
```

Recommended matrix labels:

```text
QSPI indirect stable
QSPI mmap data stable
QSPI XIP stable
```

Do not collapse these into one “QSPI stable” result.

---

## XIP Gate Function

Add this immediately before the XIP call:

```c
typedef int (*xip_func_t)(void);

static void qspi_xip_gate(void)
{
    volatile uint32_t w0 = *(volatile uint32_t *)0x90000000;
    volatile uint32_t w1 = *(volatile uint32_t *)0x90000004;
    volatile uint32_t w2 = *(volatile uint32_t *)0x90000008;
    volatile uint32_t w3 = *(volatile uint32_t *)0x9000000C;

    printf("XIP dump: %08lx %08lx %08lx %08lx\r\n",
           (unsigned long)w0,
           (unsigned long)w1,
           (unsigned long)w2,
           (unsigned long)w3);

    __DSB();
    __ISB();

    xip_func_t f = (xip_func_t)(0x90000000u | 1u);

    printf("Calling XIP func @ %p\r\n", f);

    int r = f();

    printf("XIP returned %d\r\n", r);
}
```

Expected result:

```text
XIP dump: 4770202A ...
Calling XIP func @ 0x90000001
XIP returned 42
```

Until this works reliably, do not trust MB/s results or indirect self-test results as proof of XIP stability.

---

## Conservative XIP-Only Test Config

Before chasing max speed, get any stable XIP result.

Try something conservative:

```text
prescaler: slower than current, maybe p=4 or p=5
sample shift: half-cycle
CS high time: high, 6 cycles or more
dummy cycles: 12, 14, or 16
opcode: 0x6B initially, unless board/BSP indicates another mode
MPU: executable, non-cacheable, non-bufferable first
I-cache: off for first proof
D-cache: off for first proof
```

Once the tiny XIP function returns `42`, then gradually restore performance:

1. Turn I-cache back on.
2. Re-test XIP gate.
3. Increase QSPI speed.
4. Reduce dummy cycles only after repeated XIP tests pass.
5. Try cacheable MPU attributes only after the basic mode is stable.

---

## Add Stronger XIP Test Functions

The tiny test is good:

```asm
movs r0, #42
bx lr
```

But after it works, add branch and literal-load tests because Zephyr discussions included cases where code looked readable but branches / instruction fetch behaviour still failed.

Example branch/literal test:

```asm
.thumb
.global xip_branch_test
.type xip_branch_test, %function
xip_branch_test:
    b 1f
    movs r0, #1
1:
    ldr r0, =0x12345678
    bx lr
```

Expected return:

```text
0x12345678
```

This tests:

- branch target fetch,
- literal pool access,
- more than one sequential instruction fetch,
- data read from external code region.

---

## eMMC vs QSPI for Code and LVGL Assets

Although the board has eMMC, eMMC is not preferred for XIP code.

### eMMC

Good for:

```text
large images
themes
logs
screenshots
firmware/resource update packages
large optional assets
filesystem-backed resources
```

Not good for:

```text
execute-in-place code
direct pointer access to const image/font data
instant random memory access
```

Reason: eMMC is block storage behind SDMMC. It is normally accessed via sector/block reads and a filesystem.

### QSPI NOR

Good for:

```text
XIP code
memory-mapped const assets
LVGL images/icons/fonts used constantly
compiled-in resource blobs
```

Example usage model:

```c
const uint8_t *asset = (const uint8_t *)0x90040000;
```

### SDRAM / internal SRAM

Good for:

```text
LVGL draw buffers
framebuffer
runtime decoded images
hot working buffers
```

### Practical development layout

Recommended while debugging:

```text
Internal flash:
  boot code
  clock setup
  MPU/cache setup
  QSPI/eMMC/SDRAM init
  LVGL core code initially

QSPI NOR:
  memory-mapped const assets
  small test XIP functions until stable
  later, selected external code if needed

SDRAM:
  LVGL draw buffers
  framebuffer if using full/partial buffering

emMMC:
  big image packs
  themes
  logs
  screenshots
  update packages
```

For this investigation, ignore eMMC until QSPI XIP is proven.

---

## Practical Root-Cause Direction

Current best guess:

```text
configuration issue, not bad flash silicon
```

Most likely cluster:

```text
DualFlash mode / FlashID / FlashSize / 4-byte address / flash dummy config / MPU attributes
```

The current fault is very similar to known Zephyr issues before the dual-QSPI memory-mapped mode fixes.

Therefore the next debugging effort should focus less on random timing sweeps and more on matching:

- ST BSP QSPI init assumptions,
- Zephyr dual-QSPI memory-mapped assumptions,
- correct MPU attributes for executable external memory.

---

## Immediate Action Checklist

### A. Compare against ST BSP

Check in `qspi_init.c`:

```text
DualFlash enabled
FlashID QSPI_FLASH_ID_1
FlashSize correct for combined dual flash
ClockMode 0
CSHT at least 4 cycles, maybe 6+ while debugging
SampleShift half-cycle for STR
4-byte address mode entered
quad-enable / QSPI mode configured on both flash devices
flash dummy cycles configured inside flash device
```

### B. Add direct memory-mapped data tests

Before XIP call:

```text
byte reads from 0x90000000
halfword reads from 0x90000000
word reads from 0x90000000
repeated reads
first 16 or 32 bytes dump
```

### C. Add XIP gate

Call tiny function only after memory-mapped reads match exactly.

Expected:

```text
XIP returned 42
```

### D. Disable caches for first proof

For the first proof:

```text
I-cache off
D-cache off
MPU executable but non-cacheable/non-bufferable
very conservative QSPI speed
extra dummy cycles
```

Then reintroduce features one at a time.

### E. Add branch/literal XIP test

After `return 42` works, add the branch/literal test returning:

```text
0x12345678
```

### F. Only then test LVGL/code relocation

LVGL is too large and complicated to use as the first XIP proof.

---

## Final Takeaway

The board should be capable of external QSPI app execution, but STM32H750B-DK is a dual-QSPI design and that makes XIP setup fragile.

The observed behaviour:

```text
indirect self-test PASS
memory-mapped tiny XIP function FAIL
UNDEFINSTR / IMPRECISERR near 0x90000004
```

is consistent with known STM32H7 / Zephyr dual-QSPI memory-mapped XIP issues.

The likely fix path is:

```text
match ST BSP dual-flash setup
verify 4-byte address and dummy-cycle config on the flash devices
make memory-mapped reads part of the matrix
use conservative MPU/cache settings
make the tiny XIP function the final stability gate
```

Do not mark any QSPI config as stable until it passes:

```text
indirect read/write
memory-mapped data read
repeated aligned word read
minimal XIP execution
branch/literal XIP execution
```

