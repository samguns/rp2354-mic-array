cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pico2_mic_array)
target_include_directories(app PRIVATE src)
target_sources(app PRIVATE src/doa.c src/doa_control.c src/telemetry.c)
target_sources(app PRIVATE src/main.c src/dsp_parallel.c src/beamform.c src/noise_reduce.c src/nr_control.c src/usb_audio.c src/stream.c src/capture.c src/pdm_filter.c)

target_link_libraries(app PRIVATE m)

target_sources(app PRIVATE src/audio_settings.c)
