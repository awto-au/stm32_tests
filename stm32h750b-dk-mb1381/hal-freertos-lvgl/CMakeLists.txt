cmake_minimum_required(VERSION 3.22)
project(stm32h750_lvgl C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_STRICT_WARNINGS "Enable stricter warnings for app sources" ON)
option(ENABLE_GCC_ANALYZER "Enable GCC static analyzer (-fanalyzer) for app sources" OFF)
option(ENABLE_UBSAN "Enable UBSan runtime checks (trap on error)" OFF)

# ---- External library paths -------------------------------------------------
set(H7_ROOT    "${CMAKE_SOURCE_DIR}/deps/STM32Cube_FW_H7")
set(LVGL_DIR   "${CMAKE_SOURCE_DIR}/lvgl")
set(RTOS_DIR   "${CMAKE_SOURCE_DIR}/deps/FreeRTOS-Kernel")

if(NOT EXISTS "${H7_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c")
    set(H7_ROOT "$ENV{HOME}/STM32Cube/Repository/STM32Cube_FW_H7")
endif()

if(NOT EXISTS "${RTOS_DIR}/tasks.c")
    set(RTOS_DIR "$ENV{HOME}/Downloads/git/FreeRTOS-Kernel")
endif()

if(NOT EXISTS "${LVGL_DIR}/CMakeLists.txt")
    set(LVGL_DIR "$ENV{HOME}/Downloads/git/lvgl")
endif()

if(NOT EXISTS "${LVGL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LVGL source not found. Expected '${CMAKE_SOURCE_DIR}/lvgl' or '$ENV{HOME}/Downloads/git/lvgl'.")
endif()

if(NOT EXISTS "${H7_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c")
    message(FATAL_ERROR "STM32CubeH7 source not found. Expected '${CMAKE_SOURCE_DIR}/deps/STM32Cube_FW_H7' or '$ENV{HOME}/STM32Cube/Repository/STM32Cube_FW_H7'.")
endif()

if(NOT EXISTS "${RTOS_DIR}/tasks.c")
    message(FATAL_ERROR "FreeRTOS-Kernel source not found. Expected '${CMAKE_SOURCE_DIR}/deps/FreeRTOS-Kernel' or '$ENV{HOME}/Downloads/git/FreeRTOS-Kernel'.")
endif()

set(HAL_DRV    "${H7_ROOT}/Drivers/STM32H7xx_HAL_Driver")
set(CMSIS_DEV  "${H7_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx")
set(CMSIS_INC  "${H7_ROOT}/Drivers/CMSIS/Include")

# ---- LVGL via add_subdirectory ----------------------------------------------
# lv_conf.h lives in our project root (CMAKE_SOURCE_DIR), LVGL picks it up
# automatically when LV_CONF_INCLUDE_SIMPLE is set (done in lv_conf.h guard).
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl EXCLUDE_FROM_ALL)

# ---- Generate linker script with resolved LVGL archive path -----------------
set(LVGL_ARCHIVE "lvgl/lib/liblvgl.a")
configure_file(
    ${CMAKE_SOURCE_DIR}/stm32h750xb_flash.ld.in
    ${CMAKE_BINARY_DIR}/stm32h750xb_flash.ld
    @ONLY
)

# ---- HAL sources ------------------------------------------------------------
set(HAL_SOURCES
    ${HAL_DRV}/Src/stm32h7xx_hal.c
    ${HAL_DRV}/Src/stm32h7xx_hal_rcc.c
    ${HAL_DRV}/Src/stm32h7xx_hal_rcc_ex.c
    ${HAL_DRV}/Src/stm32h7xx_hal_cortex.c
    ${HAL_DRV}/Src/stm32h7xx_hal_gpio.c
    ${HAL_DRV}/Src/stm32h7xx_hal_pwr.c
    ${HAL_DRV}/Src/stm32h7xx_hal_pwr_ex.c
    ${HAL_DRV}/Src/stm32h7xx_hal_flash.c
    ${HAL_DRV}/Src/stm32h7xx_hal_flash_ex.c
    ${HAL_DRV}/Src/stm32h7xx_hal_dma.c
    ${HAL_DRV}/Src/stm32h7xx_hal_dma_ex.c
    ${HAL_DRV}/Src/stm32h7xx_hal_mdma.c
    ${HAL_DRV}/Src/stm32h7xx_hal_tim.c
    ${HAL_DRV}/Src/stm32h7xx_hal_tim_ex.c
    ${HAL_DRV}/Src/stm32h7xx_hal_ltdc.c
    ${HAL_DRV}/Src/stm32h7xx_hal_ltdc_ex.c
    ${HAL_DRV}/Src/stm32h7xx_hal_dsi.c
    ${HAL_DRV}/Src/stm32h7xx_hal_qspi.c
    ${HAL_DRV}/Src/stm32h7xx_hal_fdcan.c
    ${HAL_DRV}/Src/stm32h7xx_hal_uart.c
    ${HAL_DRV}/Src/stm32h7xx_hal_uart_ex.c
)

# ---- FreeRTOS kernel sources ------------------------------------------------
set(RTOS_SOURCES
    ${RTOS_DIR}/tasks.c
    ${RTOS_DIR}/queue.c
    ${RTOS_DIR}/list.c
    ${RTOS_DIR}/timers.c
    ${RTOS_DIR}/event_groups.c
    ${RTOS_DIR}/stream_buffer.c
    ${RTOS_DIR}/portable/GCC/ARM_CM7/r0p1/port.c
    ${RTOS_DIR}/portable/MemMang/heap_4.c
)

# ---- Application sources ----------------------------------------------------
set(APP_SOURCES
    Src/main.c
    Src/app_log.c
    Src/log_uart.c
    Src/freertos_static_alloc.c
    Src/stm32h7xx_it.c
    Src/stm32h7xx_hal_timebase_tim.c
    Src/system_stm32h7xx.c
    Src/ltdc_init.c
    Src/lvgl_port.c
    Src/ui.c
    Src/syscalls.c
    Src/sysmem.c
    Src/qspi_init.c
    Src/can_init.c
    Src/lvgl_mem_pool.c
    Src/startup_stm32h750xbhx.s
)

set(APP_C_SOURCES
    Src/main.c
    Src/app_log.c
    Src/log_uart.c
    Src/freertos_static_alloc.c
    Src/stm32h7xx_it.c
    Src/stm32h7xx_hal_timebase_tim.c
    Src/system_stm32h7xx.c
    Src/ltdc_init.c
    Src/lvgl_port.c
    Src/ui.c
    Src/syscalls.c
    Src/sysmem.c
    Src/qspi_init.c
    Src/can_init.c
    Src/lvgl_mem_pool.c
)

add_executable(${PROJECT_NAME}.elf
    ${APP_SOURCES}
    ${HAL_SOURCES}
    ${RTOS_SOURCES}
)

# ---- Include paths ----------------------------------------------------------
target_include_directories(${PROJECT_NAME}.elf PRIVATE
    Inc
    ${CMAKE_SOURCE_DIR}              # lv_conf.h lives here
)

target_include_directories(${PROJECT_NAME}.elf SYSTEM PRIVATE
    ${HAL_DRV}/Inc
    ${HAL_DRV}/Inc/Legacy
    ${CMSIS_DEV}/Include
    ${CMSIS_INC}
    ${RTOS_DIR}/include
    ${RTOS_DIR}/portable/GCC/ARM_CM7/r0p1
)

# ---- Compiler defines -------------------------------------------------------
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    USE_HAL_DRIVER
    STM32H750xx
    LV_DISABLE_API_MAPPING
)

# ---- Compiler flags ---------------------------------------------------------
set(CPU_FLAGS -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb)

target_compile_options(${PROJECT_NAME}.elf PRIVATE
    ${CPU_FLAGS}
    -Wall
    -fdata-sections
    -ffunction-sections
    $<$<CONFIG:Debug>:-g3 -O0>
    $<$<CONFIG:Release>:-Os>
)

# ---- Strict warnings for our application code only -------------------------
set(APP_STRICT_WARNING_FLAGS
    -Wextra
    -Wundef
    -Wshadow
    -Wformat=2
    -Wstrict-prototypes
    -Werror=implicit-function-declaration
)

if(ENABLE_STRICT_WARNINGS)
    list(APPEND APP_STRICT_WARNING_FLAGS
        -Wconversion
        -Wcast-align
        -Wdouble-promotion
    )
endif()

if(ENABLE_GCC_ANALYZER)
    list(APPEND APP_STRICT_WARNING_FLAGS -fanalyzer)
endif()

set_source_files_properties(${APP_C_SOURCES} PROPERTIES COMPILE_OPTIONS "${APP_STRICT_WARNING_FLAGS}")

set(APP_RUNTIME_CHECK_FLAGS)
if(ENABLE_UBSAN)
    set(APP_RUNTIME_CHECK_FLAGS
        -fsanitize=undefined
        -fno-sanitize-recover=all
        -fsanitize-undefined-trap-on-error
    )

    set_source_files_properties(${APP_C_SOURCES} PROPERTIES COMPILE_OPTIONS
        "${APP_STRICT_WARNING_FLAGS};${APP_RUNTIME_CHECK_FLAGS}")
    target_compile_options(lvgl PRIVATE ${APP_RUNTIME_CHECK_FLAGS})
    target_link_options(${PROJECT_NAME}.elf PRIVATE ${APP_RUNTIME_CHECK_FLAGS})
    message(STATUS "UBSan enabled with trap-on-error flags")
endif()

# ---- LVGL needs same CPU flags ----------------------------------------------
target_compile_definitions(lvgl PRIVATE
    USE_HAL_DRIVER
    STM32H750xx
    LV_DISABLE_API_MAPPING
)
target_compile_options(lvgl PRIVATE ${CPU_FLAGS})
target_compile_options(lvgl PRIVATE -mno-unaligned-access)
target_include_directories(lvgl PRIVATE
    ${CMAKE_SOURCE_DIR}/Inc
    ${CMAKE_SOURCE_DIR}
)
target_include_directories(lvgl SYSTEM PRIVATE
    ${HAL_DRV}/Inc
    ${HAL_DRV}/Inc/Legacy
    ${CMSIS_DEV}/Include
    ${CMSIS_INC}
    ${RTOS_DIR}/include
    ${RTOS_DIR}/portable/GCC/ARM_CM7/r0p1
)

# ---- Link LVGL --------------------------------------------------------------
target_link_libraries(${PROJECT_NAME}.elf PRIVATE lvgl)

# ---- Linker -----------------------------------------------------------------
target_link_options(${PROJECT_NAME}.elf PRIVATE
    ${CPU_FLAGS}
    -T${CMAKE_BINARY_DIR}/stm32h750xb_flash.ld
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map,--cref
    --specs=nano.specs
    --specs=nosys.specs
)

# ---- Post-build -------------------------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE}              $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "Generating hex/bin and size report"
)
