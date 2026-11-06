# Four-channel UAC2 plus CDC

> Current 48 kHz firmware: see [capture-48k.md](capture-48k.md). The 16 kHz
> noise suppressor is unavailable at 48 kHz; results below are historical.

Firmware exposes a full-speed asynchronous UAC2 microphone capture interface
alongside the existing CDC shell. Audio is PCM16, four interleaved channels,
nominal 16 kHz. Acquisition remains a shared 1.536 MHz PDM clock and CIC3
96:1 conversion. No microphone gain, filtering, or physical channel ordering
was changed. Channels remain CH1/U1 top, CH2/U2 left, CH3/U3 bottom, CH4/U4
right in the exposed-port view.

The prototype composite USB identity is VID 2FE3, PID 0005, product
"Pico 2 Mic Array UAC2". Windows assigned CDC COM14 on this machine. Use the
current port number for the shell, flash.py, and CDC recording tools. flash.py
now follows the device serial identity across COM-number changes. The prior
CDC-only firmware is saved as build-tests/pre-uac2.uf2 for recovery.

## Audio capture on Windows

The Windows shared mixer currently presents stereo float at 48 kHz. The legacy
waveform audio listing likewise reports stereo; those paths do not establish
the hardware channel count. WASAPI exclusive successfully opens four-channel
PCM16 at 16 kHz using the built-in Windows USB audio driver.

```powershell
python tools/uac_wasapi.py --list
python tools/uac_wasapi.py build-tests/uac-capture.wav --seconds 60
```

For other recording applications, explicitly select WASAPI exclusive and all
four channels at 16 kHz, with processing/downmixing disabled. Do not assume a
stereo recording from a default Windows input retains the raw array signals.
The host capture script requires desktop microphone access; this session's
sandbox denied audio initialization, so hardware capture was run outside the
sandbox under the approved tool permission. No Windows privacy settings were
changed. No third-party Python audio packages are required.

The descriptor uses four standard quad channel slots FL/FR/BL/BR because the
current Zephyr binding derives channel count from location flags. These slot
labels are USB format metadata, not physical microphone coordinates; preserve
the application CH1..4 mapping stated above. This firmware does not implement
speaker playback, automatic beamforming, or a processed mono endpoint.

## Buffering and clock behavior

The conversion worker feeds a bounded 512-frame ring only while the host enables
capture. Each USB full-speed SOF submits 16 whole four-channel frames, or 15/17
frames when fill crosses a wide deadband. The 128-frame PDM conversion block
creates an 8-ms fill sawtooth. Priming waits for 256 frames; short initial silence
allows startup. Normal PCM is never duplicated or independently resampled by
channel to correct drift: changing packet length preserves every complete frame.

USB packet storage comes from a dedicated aligned slab and is returned only by
the stack's release callback. Ring frames are committed only after successful
USB enqueue. A full endpoint queue retries buffered audio rather than dropping
samples. Restart, source sequence gaps, underrun and overflow reset priming;
health counters expose discontinuities. Faults are not hidden from diagnostics.

Shell commands:

- `uacstats`: cumulative starts/stops, packets, source/output frames, 15/17-frame
  packet counts, priming silence attempts, ring fill range, underruns, overruns,
  source gaps, enqueue failures, allocation failures, and busy-queue retries.
- `uacpattern on` / `uacpattern off`: diagnostic ramp mode, only while the audio
  endpoint is closed. It affects UAC only; CDC still records microphone data.
- Existing `mic stats`, `mic stream`, and `boot` remain available.

## Digital transport validation

The diagnostic pattern uses the raw block sequence and frame offset as a uint16
counter in CH1, and XOR masks 1357/2468/55AA in CH2/3/4. Exact host-side validation
checks channel order, every successive frame including uint16 wrap, and absence
of inserted zeros after startup. This detects dropped/duplicated frames and host
processing; it is separate from acoustic microphone phase validation.

```powershell
# Close all audio clients; use the CDC shell to set uacpattern on.
python tools/uac_wasapi.py build-tests/uac-pattern.wav --seconds 300
python tools/verify_uac_pattern.py build-tests/uac-pattern.wav
# Close audio, then restore uacpattern off before normal microphone use.
```

WASAPI packet flags and firmware counters complement this test. A finite
successful run is not a guarantee against every future host, USB hub, or load.
The existing CDC sequence/CRC recorder remains a diagnostic fallback. UAC does
not resolve the previously measured acoustic-delay outliers.

## Hardware validation, 2026-10-14

Built and flashed successfully: flash 131488 bytes, RAM 488872 bytes (91.81%).
Windows enumerated the audio endpoint and CDC COM14. The first three-second
normal-audio recording opened four-channel PCM16/16 kHz through WASAPI exclusive.
A five-second diagnostic ramp then verified every frame after startup.

The first five-minute test used a small 40-ms host capture buffer. It had no
firmware underruns/overruns/gaps/enqueue failures, and no WASAPI discontinuity
flags, but the independent sample counter detected one loss of 218 source
frames at output frame 2132480 (133.28 seconds). It is saved as
uac-pattern-300s.wav with a FAIL sidecar; this was not accepted as a clean run.
The precise origin of that loss was not instrumented in that first recording.

The recorder was changed to request a 500-ms host buffer (Windows supplied
8192 frames, about 512 ms) and to record device-position discontinuities.
The repeated five-minute test passed:

- Exactly 4800000 four-channel output frames, 300.016 seconds host elapsed.
- 96 initial silent frames, then 4799904 consecutive diagnostic frames.
- Zero channel errors, sequence errors, inserted silence after startup,
  WASAPI packet flags or device-position gaps.
- Firmware counters remained zero for underruns, overruns, source gaps,
  enqueue failures, allocation failures and busy retries while primed.
- The second run added 384 long (17-frame) packets, demonstrating active
  clock accommodation; no independent per-channel rate conversion was used.

This supports the supplied buffered exclusive-mode recorder. A clean repeat
with a larger buffer does not conclusively prove the first gap's root cause,
and does not certify all Windows applications or buffer settings. The first
failure demonstrates why firmware counters and driver flags alone are
insufficient; retain the exact pattern test for future changes.

The CDC shell remained responsive during UAC recording. A simultaneous two-second
CDC microphone export also passed its sequence/CRC checks. After the buffered
run, `uacpattern off` was confirmed. A final ten-second normal microphone UAC
recording contained 160000 frames per channel, no WASAPI flags/position gaps,
and no clipped samples. Channel AC RMS was 5.07, 5.30, 5.37 and 4.96 PCM counts;
this ambient capture is not calibrated audio-quality evidence.

Final health: raw dropped=0, faults=0; UAC underruns=0, overruns=0, gaps=0,
send_errors=0, pool_errors=0, busy_active=0. Endpoint startup probing caused
15 cumulative busy_startup retries across the sessions; these are tracked
separately from steady-stream failures. The board is left in normal microphone
mode with audio capture stopped and the CDC shell available on COM14.

All 42 Python tests pass, including five new UAC pattern checks for counter
wrap/startup, missing/duplicate frames, swapped channels, inserted silence and
missing/truncated pattern data. Build and hardware tests validate the new C
transport and native Windows capture path; the existing DSP was unchanged.

Artifacts under build-tests:

- uac-pattern-buffered-300s.wav / .json / .pattern.json: passing long run.
- uac-pattern-300s.wav / .json / .pattern.json: retained failed small-buffer run.
- uac-microphones.wav / .json / .levels.json: final normal microphone capture.
- uac-buffered-final-health.txt and uac-final-health.txt: counter snapshots.
- cdc-during-uac.wav / .json: concurrent CDC microphone export.
- pre-uac2.uf2: prior CDC-only firmware fallback.
