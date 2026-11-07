# RP2354A microphone array — Zephyr

中文说明：[README-CN.md](README-CN.md)

Four synchronized IM72D128V01 PDM microphones on the custom RP2354A board.
Zephyr, its modules, and the SDK all live in `zephyrproject/` inside this
repository. The schematic and open-source hardware project are hosted on
[EasyEDA Pro](https://pro.lceda.cn/editor#id=b3406129141d4149bf3fd8f035bb6d27). The
build target is `rpi_pico2/rp2350a/m33`; `app.overlay` sets this chip's 2 MiB
flash, pin wiring, and USB functions. See [RP2354A board](docs/rp2354a-board.md).

## USB

One full-speed composite device, VID:PID `2fe3:0006`, product
`Pico 2 Mic Array UAC2`, manufacturer `Pico Array Prototype`, configuration
`Audio, shell and telemetry`:

| Function | USB interfaces | Windows label |
|---|---|---|
| Diagnostics shell | CDC 0 / data 1 | Mic Array Shell |
| MIC/2 telemetry and controls | CDC 2 / data 3 | Mic Array Telemetry |
| Four-channel 48 kHz PCM16 | UAC2 microphone | Pico 2 Mic Array UAC2 |

COM numbers come from the PC. `mic_app` picks the telemetry port from the
VID, PID, and interface, then checks the MIC/2 handshake. UAC2 is independent
of both serial ports. Record the four raw channels with WASAPI exclusive; the
Windows shared mixer presents stereo. See
[CDC telemetry](docs/cdc-telemetry-service.md).

With no saved audio record, the USB stream is the four microphones.
Beamforming, noise reduction, and direction estimation are off. A saved record
restores noise mode, hum filter, input channel, beam mode, azimuth, assumed
elevation, and the learned 129-bin noise spectrum before capture starts.
Direction estimation is separate and stays off until enabled. Saving is
`mic_app` Save & restart, or the MIC/2 command `SETTINGS_SAVE`, and it runs
only while UAC is idle. The last 8 KiB of flash holds two alternating 4 KiB
records with a generation count and CRC32. A normal UF2 update leaves that
partition in place; erasing the whole flash clears it. See
[persistent audio settings](docs/persistent-audio-settings.md).

The linked image in `build/zephyr/zephyr_final.map` uses 144,916 bytes of the
2,088,960-byte firmware partition and 502,472 of 532,480 bytes of RAM, leaving
30,008 bytes. `CONFIG_DMA=n` because this application owns the DMA controller.
`CONFIG_ASSERT=n` and `CONFIG_SPIN_VALIDATE=n` keep debug kernel checks out of
the realtime build. Capture sequence checks, USB health counters, configuration
bounds, and the settings CRC stay on.

## Signal path

One PIO0 state machine clocks GP6 and samples GP7–GP10 together. Every
microphone SELECT pin is grounded, so the data pins are driven while CLOCK is
low. The program samples on the fifth of ten PIO cycles, late in that low
half, with GPIO input synchronizers enabled. Each FIFO word holds eight
chronological 4-bit samples, earliest in bits 0..3, in order U1, U2, U3, U4.

At the nominal 150 MHz system clock the PIO divider is 1250/256: 3.072 MHz
PDM, then 48,000 PCM frames/s after decimation by 64. The rate follows the
12 MHz crystal. The fractional divider adds cycle-to-cycle jitter.

Two hardware-chained DMA channels fill 24 buffers. Each destination is a
1024-word, 4 KiB area, aligned to 4 KiB, and the write address wraps inside
it. A late rearm cannot run past that buffer before the fault path stops the
stream. One block is 128 PCM frames, 2.667 ms. The pool holds 64 ms. A missing
block, a missed rearm, a DMA bus error, or a stalled PIO RX FIFO stops
capture, increments `faults`, and invalidates an in-progress RAM recording.
The shell remains available. Reboot to capture again.

Conversion:

1. CIC4, decimation 16, as the exact 61-tap convolution through byte lookup tables. Core 1 runs this on the incoming block.
2. 31-tap halfband FIR, decimation 2, to 96 kHz. Core 0.
3. 63-tap halfband FIR, decimation 2, to 48 kHz. Core 0.
4. Three-tap CIC compensation at 48 kHz. Core 0.

Core 1 is a bare RP2350 worker started through the ROM FIFO handshake. It does
not run Zephyr, take interrupts, or touch DMA or USB. Queued beamforming and
GCC-PHAT windows also run there. Core 0 owns scheduling, the FIR stages, noise
reduction, USB, and DMA block lifetime. The two cores overlap by one block,
which adds 2.667 ms. The raw block stays owned until both cores finish. Hot
filter code is in SRAM.

Capture discards the first 48 blocks, 128 ms, while the microphones and the
CIC settle. Each later block is used as follows.

- Direction estimation reads the raw four-channel PCM, before the beamformer.
  It is off at boot. An analysis frame is four 2048-point windows, 8192 frames
  (170.7 ms). Manual beamforming skips the next frame. Rejected frames carry
  no angle, and the estimate does not move the beam. See
  [onboard direction](docs/onboard-direction.md).
- The beamformer runs on core 1. Off passes the four channels through.
  Broadside sums the face-normal direction. Manual applies a 21-tap fractional
  delay on the 25 mm cross below, with a common delay of 14 samples and a
  20 ms crossfade. Changing the beam clears the learned noise profile. See
  [manual beamforming](docs/manual-beamforming.md).
- Noise reduction, when enabled, runs on core 0 after the beamformer and copies
  its mono result into all four UAC slots. Off leaves the four channels
  distinct. Filter mode is an 80 Hz high-pass and an optional 50 or 60 Hz notch
  on one selected channel. Suppress mode is a 20 Hz high-pass, the same notch,
  and learned stationary-noise suppression on the 129 bins of a 256-point FFT.
  Suppress adds one 128-sample hop, another 2.667 ms. Learning takes two
  seconds; keep the sound you want to preserve out of that interval. See
  [noise reduction](docs/noise-reduction.md).

UAC2 uses a 2048-frame ring and holds a 32 ms reserve (1536 frames) before
streaming. Full-speed packets are 47, 48, or 49 frames so the device follows
the host clock. UAC carries the beamformed and noise-reduced stream. CDC
snapshots are the PCM from before the beamformer.

```text
nr config off|filter|suppress [hum=0|50|60] [channel=1..4]
nr learn
nr stats
bf config off|broadside|manual <azimuth-deg> [elevation-deg=0]
bf stats
doa config on|off
doa stats
```

Close UAC before `nr config` or `uacpattern`. Beam changes are accepted during
a UAC recording. `bf stats` prints mode, tenth-degree azimuth and elevation,
revision, pending and fade counts, and each channel's delay in milliseconds.

## Array geometry

Sound ports sit on a 25 mm cross. In the exposed-port view, USB-C upper right:

| Channel | Microphone | GPIO | X, Y (mm) | Position |
|---|---|---|---|---|
| 1 | U1 | GP7 | (0, 25) | top |
| 2 | U2 | GP8 | (−25, 0) | left |
| 3 | U3 | GP9 | (0, −25) | bottom |
| 4 | U4 | GP10 | (25, 0) | right |

Azimuth is 0° right, 90° top, 180° left, and 270° bottom. Elevation 0 lies in
the board plane; 90 points straight out of the port face and is the broadside
direction. Elevation is an assumed steering angle. The same frame is used by
the beamformer, the direction estimator, and `mic_app`. Coordinates and the
diagram are in [array geometry](docs/array-geometry.md),
[array-geometry.svg](docs/array-geometry.svg), and
[array-geometry.json](docs/array-geometry.json).

```powershell
python tools/extract_geometry.py
```

## Wiring

GPIO numbers are not QFN package pins. Source: `src/board_pins.h` and the [EasyEDA Pro hardware project](https://pro.lceda.cn/editor#id=b3406129141d4149bf3fd8f035bb6d27).

| Net | GPIO | U5 QFN-60 pin | Goes to |
|---|---|---|---|
| SCLK | GP6 | 9 | U1–U4 CLOCK |
| DATA1 | GP7 | 10 | U1, PCM channel 1 |
| DATA2 | GP8 | 12 | U2, PCM channel 2 |
| DATA3 | GP9 | 13 | U3, PCM channel 3 |
| DATA4 | GP10 | 14 | U4, PCM channel 4 |
| DIN | GP11 | 15 | U7 pin 3, external amplifier |
| BCLK | GP12 | 16 | U7 pin 4 |
| LRC | GP13 | 17 | U7 pin 5 |

U7 pins 1 and 2 are VBUS and GND for that amplifier. U6 is the ME6217C33M5G
3.3 V regulator. USB D−/D+ connect to U5 package pins 51 and 52. The crystal
is 12 MHz. GP0–GP5 are free; the overlay disables `uart0`, `i2c0`, `i2c1`,
and `adc`.

Flash is the in-package 2 MiB. Firmware occupies `0x000000–0x1FDFFF`. The
`audio-settings` partition is `0x1FE000–0x1FFFFF`.

## Build and flash

Zephyr, its modules, and the SDK stay in `zephyrproject/` inside this
repository. `setup.ps1` and `setup.sh` do the same work: check out Zephyr
`27150c9dd348690a6b41d3992dd5d3d1458fdc66`, update the modules named by that
manifest, apply `patches/zephyr-udc-rpi-pico.patch`, and install Zephyr SDK
1.0.1 with the Arm toolchain. The patch acknowledges USB start-of-frame and
keeps a queued control transfer from being reported as an endpoint error.
`zephyrproject/` is gitignored. The image is `build/zephyr/zephyr.uf2`, and
the CMake cache stays in `build/.cache`.

### Windows

Install Git, Python 3, CMake, Ninja, and 7-Zip. Then install west:

```powershell
python -m pip install west
```

`setup.ps1` downloads `zephyr-sdk-1.0.1_windows-x86_64_minimal.7z` and runs
`setup.cmd /t arm-zephyr-eabi /h /c`.

```powershell
.\setup.ps1
.\build.ps1
.\build.ps1 -Pristine
```

Hold BOOTSEL while connecting the board, then copy the UF2 to the mass-storage
volume that appears. Copy it only to that volume. Once this firmware is
running, `boot` in the shell restarts into BOOTSEL with mass storage and
picoboot both enabled. The serial ports disconnect until the new image
enumerates.

```powershell
python tools/flash.py <shell-port>
```

The script checks the UF2 header, sends `boot`, and copies onto the single
drive whose `INFO_UF2.TXT` contains `Board-ID: RP2350`. More than one such
drive stops the script. It then waits for the same USB serial number on the
shell interface, Windows `MI_00`. The COM number may change.

### Ubuntu

Install the host packages, then west. `~/.local/bin` has to be on `PATH`.

```bash
sudo apt install --no-install-recommends git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget curl python3-dev python3-pip python3-setuptools python3-wheel xz-utils file make gcc g++
python3 -m pip install --user west
bash setup.sh
bash build.sh
bash build.sh --pristine
```

`setup.sh` reads `uname -m` and downloads the matching minimal archive,
`zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz` or
`zephyr-sdk-1.0.1_linux-aarch64_minimal.tar.xz`. It then runs
`zephyrproject/zephyr-sdk-1.0.1/setup.sh -t arm-zephyr-eabi -h -c`.

Hold BOOTSEL while connecting the board and copy `build/zephyr/zephyr.uf2`
onto the RP2350 mass-storage volume. `tools/flash.py` is the Windows helper.
The shell is a CDC ACM device; host tools that take a COM port take the
corresponding `/dev/ttyACM*` node instead.

## Shell and recording

CDC ignores the baud rate; 115200 is a fine terminal setting. Capture starts
without a terminal. After warmup, `mic stats` shows `ready=1`, an increasing
`blocks` count, and `dropped=0 faults=0`. `max_process_us` stays within the
2.667 ms block. Peaks are the latest block's per-channel absolute PCM16
values, not SPL. Close the terminal before a host script opens the port.

```text
mic stats
mic record 1000
mic status
mic dump
mic raw
uacstats
uacpattern on
doa capture
doa dump
```

`mic record` accepts 1..333 ms, up to 16,000 frames in the shared diagnostic
buffer. The host tool records, then reads the hex dump, and checks the header
and length. The text transfer has no extra checksum. A second record is
rejected while one is active. A dropped block marks that take `INVALID`.

```powershell
python -m pip install pyserial
python tools/record.py <shell-port> capture.wav --ms 250
python tools/raw_pdm.py <shell-port> --output build-tests/raw-pdm
```

`tools/raw_pdm.py` counts GP6 edges, exports 10.67 ms of all four PDM lanes
plus the firmware PCM, and rebuilds PCM with a separate bitwise CIC and
integer FIR. `uacpattern` replaces the microphones with a diagnostic ramp;
turn it off before treating a recording as microphone audio. `doa capture`
holds one raw frame for `doa dump`.

Continuous four-channel recording uses UAC2:

```powershell
python tools/uac_wasapi.py --list
python tools/uac_wasapi.py build-tests/continuous-48k.wav --seconds 60
```

The WAV is exclusive-mode PCM16 at 48 kHz. A JSON sidecar records Windows
packet flags and position gaps. An unfinished recording is left as
`.partial.wav`.

Host checks, without the board:

```powershell
New-Item -ItemType Directory -Force build-tests | Out-Null
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/filter_test.c src/pdm_filter.c -lm -o build-tests/filter_test.exe
.\build-tests\filter_test.exe
python -m unittest discover -s tests -p "test_*.py" -v
```

The C test compares the lookup CIC with a scalar reference, including overflow
and block edges, then checks channel isolation, polarity, DC gain, and a
synthetic sigma-delta sine. The Python tests cover the host tools. They do
not measure pin timing or on-device throughput.

## Desktop viewer

[`mic_app`](mic_app/README.md) is a Tauri, Vue, and TypeScript application.
Build it with `npm run desktop:build` inside `mic_app`; the resulting
`mic_app/release/mic_app.exe` is not part of the source tree. It opens Mic Array Telemetry only. From
there you can enable direction estimation, choose Off, Broadside, or Manual,
set azimuth and assumed elevation, apply the beam, and relearn noise.
Save & restart writes the audio record and reconnects. Simulation draws the
same interface and does not open a port. Audacity or `tools/uac_wasapi.py`
can record UAC while the app is connected. Leave the shell port closed while
the app is using the board.

Microphone: [Infineon IM72D128](https://www.infineon.com/part/IM72D128).
Chip: [Raspberry Pi RP2350](https://www.raspberrypi.com/documentation/microcontrollers/microcontroller-chips.html).

## License

The firmware, host tools, desktop application, hardware drawings, and
documentation in this repository are under the MIT license. See [LICENSE](LICENSE).

Zephyr and the Zephyr SDK are Apache-2.0. The setup scripts download them
into `zephyrproject/`, which is not part of this repository.
`patches/zephyr-udc-rpi-pico.patch` modifies Zephyr's RP2350 USB device
driver; that file remains under the Apache-2.0 terms. The Pico SDK used by
`sdk-benchmark` is the workspace's `hal_rpi_pico` module (BSD-3-Clause).
Packages installed for `mic_app` keep their own licenses.

`U6.pdf` and `speaker.pdf` are vendor documents and are not distributed here.
