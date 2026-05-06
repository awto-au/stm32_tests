/*
** Linker script for STM32H750XBHx — LVGL display demo
** Internal Flash: 128K  @ 0x08000000
** DTCM RAM:       128K  @ 0x20000000  (stack/heap/FreeRTOS)
** AXI SRAM:       512K  @ 0x24000000  (LVGL framebuffers — DMA accessible)
** SRAM1:          128K  @ 0x30000000
** SRAM2:          128K  @ 0x30020000
*/

ENTRY(Reset_Handler)

_estack = ORIGIN(DTCM) + LENGTH(DTCM);
_Min_Heap_Size  = 0x2000;
_Min_Stack_Size = 0x2000;

MEMORY
{
  FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
  EXTFLASH (rx)  : ORIGIN = 0x90000000, LENGTH = 64M   /* MT25TL01G Bank-1, QSPI memory-mapped */
  DTCM     (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  AXISRAM  (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
  SRAM1    (xrw) : ORIGIN = 0x30000000, LENGTH = 128K
  SRAM2    (xrw) : ORIGIN = 0x30020000, LENGTH = 128K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >DTCM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >DTCM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >DTCM

  /* LVGL framebuffers in AXI SRAM (DMA-accessible, cache-managed) */
  .framebuffers (NOLOAD) :
  {
    . = ALIGN(32);
    *(.framebuffers)
    *(.framebuffers*)
    . = ALIGN(32);
  } >AXISRAM

  /*
   * External QSPI flash window — read-only data/code placed here via
   *   __attribute__((section(".extflash")))
   * or via linker wildcard below.
   * QSPI_MemoryMapped_Init() must be called from internal flash before any
   * symbol in this region is accessed.
   *
   * LVGL font bitmaps are the dominant rodata consumer — keep them in EXTFLASH
   * from the start so internal flash stays free for code.
   */
  .extflash :
  {
    . = ALIGN(4);
    *(.extflash)
    *(.extflash*)
    /* LVGL library rodata — fonts, image tables, etc. */
    *liblvgl.a:(.rodata .rodata*)
    . = ALIGN(4);
  } >EXTFLASH

  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
