## Overview

This project is an open-source four-channel digital microphone array built on the Raspberry Pi RP2354A and Zephyr RTOS. The board carries four synchronized Infineon IM72D128V01 PDM microphones, with sound ports on a planar cross of 25 mm radius. Using the RP2350 dual Cortex-M33 cores and programmable I/O (PIO), it performs 3.072 MHz parallel PDM sampling, exact CIC4/FIR decimation, 48 kHz on-chip audio DSP, onboard direction-of-arrival (DOA) estimation, and beamforming. It enumerates as a standard USB composite device: a driverless four-channel UAC2 stream, an interactive diagnostics shell, and a dedicated telemetry and control interface, with a cross-platform desktop viewer.

## Features

1. **Synchronized multi-channel capture and decimation**
   - **Parallel PIO sampling.** One state machine drives a shared 3.072 MHz clock on GP6 and samples four PDM lanes (GP7–GP10) together while the clock is low, so the channels stay time-aligned.
   - **Chained DMA into bounded buffers.** Two hardware-chained DMA channels fill 24 aligned 4 KiB buffers. Each block is 128 frames (2.667 ms). A lost block or a bus error stops capture and reports the fault, so a late rearm cannot write past its buffer or hide a gap.
   - **Dual-core filter pipeline.** Core 1 runs a bare-metal CIC4 stage (decimation by 16, lookup-table convolution). Core 0 runs two halfband FIR stages (each decimation by 2) and CIC compensation. Total decimation is 64, producing 48 kHz signed interleaved PCM16.

2. **Onboard DSP and beamforming**
   - **Delay-and-sum beamforming.** Broadside forms the face-normal sum. Manual steering applies a 21-tap sinc fractional delay on the 25 mm cross, with a common delay of 14 samples, azimuth over a full turn, and elevation from 0° to 90°. Beam changes crossfade over 20 ms.
   - **Full-band stationary-noise suppression and notches.** A 256-point real FFT (129 bins) learns and suppresses stationary noise directly at 48 kHz. The mono result is copied into all four USB slots. Filter mode provides a 20 Hz or 80 Hz high-pass and an optional 50 Hz or 60 Hz hum notch.
   - **Saved audio settings.** The last 8 KiB of RP2354A flash holds two alternating sectors with a generation count and CRC32. Noise mode, notch settings, beam direction, and the 129-bin learned spectrum are restored after reboot.

3. **Onboard direction estimation**
   - **Estimate from raw audio.** GCC-PHAT runs on the four raw PCM channels, before beamforming and filtering. Core 1 computes delay and coherence for all six microphone pairs.
   - **Rejection of uncertain frames.** Frames are rejected for low level, clipping, poor coherence, a delay beyond the physical limit (more than 0.45 sample of margin), or a direction too close to the array normal. A rejected frame publishes no angle.
   - **Publication timing.** One analysis frame is four 2048-point windows (8192 frames, 170.7 ms). Results are published about 44 ms after the newest sample.

4. **USB device and interfaces**
   - **One composite device, VID:PID `2fe3:0006`.**
     - **UAC2 audio.** A standard USB Audio Class 2.0 microphone. On Windows, WASAPI exclusive mode records four-channel 48 kHz PCM16, raw or processed.
     - **CDC ACM shell.** Reports capture health (`mic stats`), exports a RAM recording of up to 333 ms (16,000 frames), captures a raw PDM and clock snapshot (`mic raw`), plays a diagnostic ramp (`uacpattern`), and reboots into the BOOTSEL bootloader (`boot`).
     - **MIC/2 telemetry.** A separate CDC port carries ASCII commands and JSON replies. After one subscription it pushes heartbeats, capture status, and direction results, and it accepts beam, noise-learning, and save-and-restart commands.

5. **Desktop viewer (`mic_app`)**
   - Built with Tauri 2, Vue 3, and TypeScript. The desktop build does not need a Python runtime.
   - **Compass.** Shows the estimated azimuth, the steered beam, per-channel level in dBFS, coherence, and fit residual.
   - **Controls.** Changes the beam mode and steering angles, relearns stationary noise for two seconds, and saves the audio configuration before restarting the board.
   - **Trials and replay.** Includes simulation, controlled-source statistics (circular bias, mean absolute error, and 95th-percentile absolute error), JSON and CSV export, and replay of a saved session.

6. **Self-contained build and checks**
   - **In-repository workspace.** Windows (`setup.ps1` / `build.ps1`) and Ubuntu (`setup.sh` / `build.sh`) fetch the pinned Zephyr revision, apply the RP2350 USB driver patch, and install Zephyr SDK 1.0.1 for the host architecture. The workspace stays inside this repository.
   - **Host tests.** C tests compare the CIC and FIR stages with an independent reference and check the tone generator. Python tests cover the host-tool protocols and frame parsing.
