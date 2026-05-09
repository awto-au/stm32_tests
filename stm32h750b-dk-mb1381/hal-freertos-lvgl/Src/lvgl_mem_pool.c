#include <stdint.h>
#include "lvgl_mem_pool.h"
#include "lv_conf.h"

/* 128 KB pool placed in SRAM1 (0x30000000) via linker .lvgl_heap section.
 * SRAM1 is DMA2D-accessible and separate from the FreeRTOS DTCM heap. */
static uint8_t __attribute__((section(".lvgl_heap"), aligned(32)))
    lv_pool[LV_MEM_SIZE];

void *lvgl_mem_pool_get(unsigned int size)
{
    (void)size;
    return lv_pool;
}
