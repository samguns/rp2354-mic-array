# Compute-only Pico SDK benchmark

This is not a complete microphone-array firmware port. It benchmarks the same
PDM conversion and NR C functions from a C++ Pico SDK application on the board.
It has USB CDC stdio, but no UAC2 or PDM acquisition. Results/limitations are in
../docs/sdk-comparison.md. Do not compare its BSS to full Zephyr firmware RAM as
if they had the same features.

Dependencies come from this repository's `zephyrproject/` workspace: the Pico
SDK at `zephyrproject/modules/hal/rpi_pico`, and `arm-zephyr-eabi` from
`zephyrproject/zephyr-sdk-1.0.1`. TinyUSB 0.18.0 is cloned into `deps/tinyusb`
and is intentionally ignored. Create the workspace with `.\setup.ps1` on Windows or `bash setup.sh` on Ubuntu first.

```powershell
git clone --depth 1 --branch 0.18.0 https://github.com/hathach/tinyusb.git sdk-benchmark/deps/tinyusb
cmake -S sdk-benchmark -B build-sdk-benchmark -G Ninja -DPICO_GCC_TRIPLE=arm-zephyr-eabi -DPICO_TOOLCHAIN_PATH="$PWD/zephyrproject/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin" -DPICO_HARD_FLOAT_ABI=1 -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdk-benchmark
```

The post-build converter wraps the generated binary as RP2350 Arm Secure UF2.
Prototype USB identity is VID 2FE3/PID 0006; Windows assigned COM16 during this
session. Send `r` for a benchmark and `b` for ROM BOOTSEL. Connect/open the serial
port within 30 seconds; if never connected, the firmware returns to BOOTSEL.
The watchdog also returns to BOOTSEL on a stalled benchmark. Use an automated
flash/open/read script to avoid the connection timeout.

After testing, restore build/zephyr/zephyr.uf2 using the bootloader. The flash
tool's optional --new-pid accepts a deliberate firmware USB identity change;
use 0x0006 for this benchmark and 0x0005 when restoring Zephyr. It still checks
the source device VID and refuses multiple candidate ports.
