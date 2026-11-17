cmake_minimum_required(VERSION 3.13)

# HC89S003A ICP probe for a Raspberry Pi Pico. Usage: ../README.md.
set(PICO_BOARD pico CACHE STRING "Target board")

include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)

project(hc_icp_probe C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(hc_icp_probe
    main.c
    hc_icp.c
    icp_hal_pico.c
    ihex.c
)

target_compile_options(hc_icp_probe PRIVATE -Wall -Wextra)

# Off by default: the probe stays read-only until first contact has passed
# (PROTOCOL.md §8). On, it builds hc_icp_writer.uf2 instead,
# so the two images cannot be mistaken for each other.
option(ICP_WRITE "Build the erase/program commands" OFF)
if(ICP_WRITE)
    target_compile_definitions(hc_icp_probe PRIVATE ICP_WRITE)
    set_target_properties(hc_icp_probe PROPERTIES OUTPUT_NAME hc_icp_writer)
endif()

target_link_libraries(hc_icp_probe
    pico_stdlib
    hardware_clocks
)

pico_enable_stdio_usb(hc_icp_probe 1)
pico_enable_stdio_uart(hc_icp_probe 0)

pico_add_extra_outputs(hc_icp_probe)
