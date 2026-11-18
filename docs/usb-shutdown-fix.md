# USB playback shutdown timing fix — 2026-11-18

The Windows duplex shutdown regression is addressed in
`patches/zephyr-udc-rpi-pico.patch`, in both RX and TX buffer preparation.

The USB DPRAM crosses the system and USB clock domains. The
[RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf),
section 12.7.3.7.1, requires buffer metadata to settle for at least one USB
clock before setting `AVAILABLE`. The pinned Zephyr driver used three NOPs
based on a 125 MHz system clock. This board runs at 150 MHz: three cycles
are 20 ns, while one 48 MHz USB clock is about 20.83 ns. That delay does not
provide the specified minimum interval.

Both paths now use `k_busy_wait(1)` between the metadata write and the
`AVAILABLE` write. The delay is independent of system clock frequency.
The contiguous allocator correction, SOF acknowledgement, and cancellation
cleanup remain in the combined patch. Diagnostic tracing is absent from the
tested firmware.

## Hardware verification

Board serial: `E66580A7C72D6127`; USB `2fe3:0007`; Windows 11 built-in UAC2
driver. Capture is four-channel 48 kHz PCM16, playback stereo 48 kHz PCM16.
Playback was silent; the device generated a diagnostic pattern that verifies
channel order and every sample counter after startup silence.

| Test | Result |
|---|---|
| 40 separate four-second captures overlapping two-second playback | Passed |
| 30 playback stops within one continuous 66-second capture | Passed |
| 50 playback stops within one continuous 106-second capture, DSP enabled | Passed |
| 185-second loaded capture overlapping 180-second playback, including shutdown | Passed |
| Final shell-driven flash followed by three playback stops during capture | Passed |

DSP load included manual beamforming at 45 degrees, learned full-band noise
suppression, and continuous direction estimation. All tests above had zero
sample/channel errors, WASAPI discontinuities, microphone drops/faults,
playback underruns/overruns/faults, and USB errors or recoveries. Both audio
terminals were inactive after closing them. These results use the build
without diagnostic tracing, which had changed the failure's timing.

The 185-second test verified 8,878,704 pattern frames after startup silence.
Its final playback stop caused no discontinuity or recovery.

The native handoff regression executes the production RX/TX functions against
a model requiring four CPU cycles at 150 MHz before transferring buffer
ownership. It also checks payload length, data PID, and rejection of already
available buffers. The host suite ran 66 tests: 64 passed, with two skips
for unavailable historical artifacts. The complete patch reproduces the local
driver from the pinned clean Zephyr source.

## Repeat the shutdown regression

Close mic_app, serial terminals, and audio applications using this device.
Use the current shell port and a new output directory:

```powershell
python tools/validate_uac_shutdown.py --port COM11 --cycles 50 --output build-tests/shutdown-check
```

This holds capture open while repeatedly starting and stopping playback. It
checks the WAV, Windows packet metadata, firmware health, terminal shutdown,
and `usb_recoveries=0`. A recovery fails the test. The diagnostic pattern is
temporary and cleared afterward; settings are not saved.

## Recovery and BOOTSEL

The exceptional USB disconnect/reconnect fallback remains, with its count
visible as `usb_recoveries` in `uacstats`. It was never invoked in the passing
tests above. It is not the basis for the shutdown test passing.

The temporary hardware watchdog has been removed. `boot` locks interrupts
before invoking the ROM reboot so another thread cannot postpone the ROM's
delayed watchdog reset. Shell-driven BOOTSEL and flashing have been verified.
Saved settings are preserved by the firmware update.

Build: 147,568 bytes flash; 513,784 of 532,480 bytes RAM, leaving 18,696 bytes.

Tested UF2 SHA-256: `fedaa88dfdcc7d4084bdcc4f255d37f9783b9a23c5e67fee823c669d4d283422`.
Local verification summary and final health report:
`build-tests/usb-validation-2026-11-18/` (ignored artifacts). Detailed JSON/log
reports are retained in `investigation-reports.zip`; temporary recordings,
diagnostic firmware images and investigation scripts were removed during cleanup.
