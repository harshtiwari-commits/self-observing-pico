cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(selfobs_demo C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(selfobs_demo
    src/main.c
)

# MUST come after add_executable (SDK 2.x attaches generation to the target)
pico_generate_pio_header(selfobs_demo ${CMAKE_CURRENT_LIST_DIR}/pio/demo.pio)

target_link_libraries(selfobs_demo
    pico_stdlib
    hardware_pio
    hardware_dma
    pico_multicore
)

pico_enable_stdio_usb(selfobs_demo 1)
pico_enable_stdio_semihosting(selfobs_demo 0)

pico_add_extra_outputs(selfobs_demo)
