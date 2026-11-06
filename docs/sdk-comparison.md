# Pico C/C++ SDK headroom check, 2026-10-14

Recommendation: keep the current Zephyr implementation for now. The same
portable DSP code runs at nearly the same compute speed in the standalone
Pico C/C++ SDK benchmark; no major algorithm speedup was demonstrated. Bare-metal
firmware can avoid kernel/thread/queue overhead, but end-to-end CPU/RAM savings
with equivalent UAC2, DMA acquisition, buffering and diagnostics require an
actual feature-equivalent port. This experiment is not that port.

The [official Pico SDK](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html)
supports C and C++; [its hardware APIs](https://www.raspberrypi.com/documentation/pico-sdk/)
are thin hardware abstractions. Our Zephyr application already uses Pico HAL
PIO/DMA APIs directly, so those operations are not routed through a generic
Zephyr audio/DMA driver. C++ syntax itself is not an optimization.

## Measured experiment

The standalone benchmark in sdk-benchmark uses the locally available Pico SDK
2.3.0 source, TinyUSB 0.18.0 (commit 86ad6e56c1700e85f1c5678607a762cfe3aa2f47),
C++ main and the exact same C `noise_reduce.c` and `pdm_filter.c`. Both builds
use GNU 14.3.0, -O2 for DSP, Cortex-M33 hardware floating point and 150 MHz.
The local SDK is the Zephyr HAL copy; the benchmark maps its renamed
`pico_irq_is_enabled` API for USB stdio compatibility. The shared checkout was
not modified; TinyUSB is downloaded only into the workspace's ignored deps folder.

The SDK firmware was built, temporarily flashed to the actual board, and timed
three runs of 1000 blocks through USB CDC. It includes a USB recovery command and
watchdog/connection timeout. The final Zephyr image was then restored and checked.

| Stage, 128 PCM frames per block | Zephyr, live acquisition, UAC idle | SDK microbenchmark mean |
|---|---:|---:|
| Four-channel PDM-to-PCM | 1866 us | 1874.19 us |
| Mono high-pass + 50 Hz notch | 110 us | 99.43 us |
| Learned spectral suppression + filters | 969 us | 946.68 us |

Zephyr NR timing includes copying mono into four UAC slots; SDK timing measures
only `nr_process`. Zephyr processes live microphone input while SDK processes
synthetic buffers. Timers include preemption; averages are observations, not
cycle-accurate isolated kernels or proof of a fixed percentage advantage.
The compute rates are close enough that a framework switch is not justified
by DSP speed alone.

With actual UAC recording active, observed Zephyr means were 2348 us for PDM
and 1222 us for NR, including interrupt/thread preemption. These spans consume
about 3.57 ms of the 8-ms worker budget, but they are not a CPU utilization
measurement and omit USB work outside those spans. A feature-equivalent TinyUSB
UAC2 implementation would be needed to compare that USB/scheduling overhead.

## Memory interpretation

SDK benchmark ELF has 33760 bytes of text and 17920 bytes of BSS. It contains no
live microphone capture, UAC2 endpoint, acquisition ring, CDC hex/binary recorder,
speaker diagnostics or Zephyr shell. Comparing that BSS directly to the 401984-byte
Zephyr linker RAM figure would be misleading; SDK stack/reserved memory is also
not included in ELF BSS alone.

The Zephyr raw DMA data, legacy recording workspace and speaker samples alone
occupy 321536 bytes, about 80% of its linked RAM. Other application stream/probe
buffers and DSP state add more. These payload buffers are application choices,
not intrinsic RTOS overhead. Current changes already reclaim 86888 bytes net
without dropping UAC2 or diagnostic functionality.

A smaller production buffer/diagnostic profile should be evaluated before a
full migration. SDK could still reduce OS overhead or simplify explicit multicore
scheduling, but neither additional full-system RAM savings nor equivalent USB
reliability has been measured here. Keep the passing Zephyr raw/UAC regression
tests as acceptance criteria for any later full SDK port.

Artifacts: sdk-benchmark/main.cpp and CMakeLists.txt; build-sdk-benchmark outputs;
build-tests/sdk-benchmark-results.txt and nr-sdk-comparison.json. Dependencies
and compiler paths are specified in the benchmark's build notes. The active
firmware is Zephyr, not the temporary SDK benchmark.
