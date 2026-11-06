# Continuous four-channel recording

Close other applications using COM13, then run:

```powershell
python tools/stream.py COM13 build-tests/continuous-60s.wav --seconds 60
```

The firmware shell command is `mic stream <seconds>` (1..3600). It owns
the USB shell connection for the requested duration and returns to the
normal shell afterward. Use the host tool: output is binary rather than
terminal text. CDC baud rate is not the USB transfer rate. This is a custom
CDC recording protocol, not a USB Audio Class device.

Audio is synchronized, interleaved PCM16, four channels, nominal 16 kHz.
Each block contains 128 frames (8 ms). A bounded 16-block queue separates
capture from USB; USB backpressure never blocks microphone processing.
Queue overflow, raw capture discontinuity, hardware faults, CRC mismatch,
missing packets, or missing terminal packet invalidate the recording.
USB transmission has a bounded stall timeout. No automatic reconnect or
gap filling is performed.

The host initially writes `.partial.wav`; only a complete validated
recording becomes the requested WAV. A JSON sidecar records frame count,
sequence range, elapsed host time, and per-channel mean, AC RMS, peak and
clipped sample count. These levels describe the captured scene, not calibrated
microphone self-noise or sensitivity. No normalization or gain correction is
applied. Channel numbers follow GPIO7..10; physical placement needs validation.

Protocol v1: 36-byte header, little-endian, followed by `frames * 8` PCM bytes:

| Offset | Field |
|---|---|
| 0 | Eight ASCII bytes `P2PCM001` |
| 8 | Raw capture block sequence, uint32 |
| 12 | Nominal sample rate, uint32 |
| 16 | Frames: 128 for audio, zero for terminal packet |
| 20 | Cumulative raw block drops, uint32 |
| 24 | Cumulative hardware faults, uint32 |
| 28 | Stream error flag, uint32 |
| 32 | IEEE CRC32 of payload, uint32 |

The terminal sequence is the last audio sequence plus one modulo 2^32.
CRC32 covers the payload. Header fields are checked for valid format,
sequence continuity and health. Counters must remain unchanged during the
recording, hardware faults must be zero, and the requested frame count must
match exactly. The first sequence can be any value; capture runs continuously
before and after recording. Shell echo may precede the first magic value and
the prompt may follow the terminal packet.

For channel validation, make a quiet-room recording, then a separate recording
with a fixed centrally placed source. Compare channel RMS and inspect clipping;
do not interpret unequal levels from a nearby off-center source as sensitivity
mismatch. Channel mapping and acoustic delay validation precede direction finding.

## Hardware validation, 2026-10-14

Built and flashed on the attached Pico 2 via COM13. Firmware uses 124760
bytes of flash and 479520 bytes of RAM (90.05%). CDC uses a dedicated
1024-byte USB buffer pool and 4096-byte shell/CDC transmit queues to batch
transfers; the original small-transfer configuration overflowed the stream
queue and was correctly rejected by the host. Final configuration passed a
two-second smoke test followed by a 60-second capture.

The 60-second capture contains exactly 960000 frames per channel, 7500
consecutive blocks (sequences 1873..9372), all payload CRCs valid, and a valid
terminal packet. Host elapsed time was 60.312 seconds including connection
setup. Final firmware health: ready=1, dropped=0, faults=0,
max_process_us=7611. The USB shell responded normally after streaming.

Ambient AC RMS levels were -74.04, -73.89, -73.96 and -74.40 dBFS for
channels 1..4. No clipped samples; peaks were 216, 220, 217 and 210 PCM
counts. This uncontrolled ambient capture is a consistency observation,
not a measurement of microphone self-noise or calibrated sensitivity.

Artifacts: `build-tests/continuous-60s.wav` and its JSON sidecar. All 23 Python
tests passed, including seven new stream tests. Physical channel mapping,
controlled sensitivity comparison and acoustic-delay checks remain the next
validation steps before direction finding.
