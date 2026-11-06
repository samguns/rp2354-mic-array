cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD pico2 CACHE STRING "")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "")
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../zephyrproject/modules/hal/rpi_pico" CACHE PATH "")
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/tinyusb" CACHE PATH "")
set(PICO_NO_PICOTOOL 1 CACHE BOOL "")
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
project(dsp_sdk_benchmark C CXX ASM)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
pico_sdk_init()
add_executable(dsp_sdk_benchmark main.cpp ../src/noise_reduce.c ../src/pdm_filter.c)
target_include_directories(dsp_sdk_benchmark PRIVATE ../src)
target_compile_options(dsp_sdk_benchmark PRIVATE -O2)
target_compile_definitions(dsp_sdk_benchmark PRIVATE
 irq_is_enabled=pico_irq_is_enabled
 USBD_VID=0x2fe3 USBD_PID=0x0006
 USBD_PRODUCT="Pico_SDK_DSP_Benchmark")
target_link_libraries(dsp_sdk_benchmark pico_stdlib hardware_clocks m)
pico_enable_stdio_usb(dsp_sdk_benchmark 1)
pico_enable_stdio_uart(dsp_sdk_benchmark 0)
pico_add_extra_outputs(dsp_sdk_benchmark)
add_custom_command(TARGET dsp_sdk_benchmark POST_BUILD
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/make_uf2.py
 ${CMAKE_CURRENT_BINARY_DIR}/dsp_sdk_benchmark.bin
 ${CMAKE_CURRENT_BINARY_DIR}/dsp_sdk_benchmark.uf2)
