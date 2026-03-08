cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(64korppu C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(64korppu
    src/main.c
    src/floppy_ctrl.c
    src/mfm_codec.c
    src/fat12.c
    src/iec_protocol.c
    src/cbm_dos.c
    src/d64.c
)

pico_generate_pio_header(64korppu ${CMAKE_CURRENT_LIST_DIR}/pio/iec_bus.pio)
pico_generate_pio_header(64korppu ${CMAKE_CURRENT_LIST_DIR}/pio/mfm_read.pio)
pico_generate_pio_header(64korppu ${CMAKE_CURRENT_LIST_DIR}/pio/mfm_write.pio)

target_include_directories(64korppu PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(64korppu
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_irq
    hardware_timer
    hardware_gpio
)

pico_enable_stdio_usb(64korppu 1)
pico_enable_stdio_uart(64korppu 0)

pico_add_extra_outputs(64korppu)
