# UAC2 microphone and speaker

For the playback-stop timing correction and repeated Windows duplex validation,
see [USB shutdown fix](usb-shutdown-fix.md). Earlier allocator verification
and the original failure are recorded in [USB buffer allocation fix](usb-buffer-allocation-fix.md).

The composite USB device exposes four-channel 48 kHz PCM16 capture and stereo
48 kHz PCM16 playback, plus independent CDC shell and MIC/2 telemetry ports.
Select Pico 2 Mic Array UAC2 as the Windows output device to play PC audio.
The MAX98357A drives the single connected loudspeaker; its hardware channel
selection determines whether it uses left, right or their mono mix.

DIN is GP11, BCLK GP12 and LRC GP13, through U7 pins 3, 4 and 5. Existing
microphone clock/data pins remain GP6 and GP7-GP10. The speaker uses a separate
PIO0 state machine and two chained DMA channels, with DMA IRQ3 independent of
microphone DMA IRQ2. A 2048-frame stereo ring feeds 128-frame DMA blocks.
PCM16 is expanded into the upper bits of 32-bit standard I2S slots. BCLK is
3.072 MHz and LRCLK 48 kHz; no MCLK is required by the MAX98357A.

USB playback has an asynchronous OUT endpoint and explicit feedback based on
ring occupancy. The Zephyr Windows full-speed workaround emits four-byte
Q16.16 feedback from the application's Q10.14 rate. Feedback adjusts host packet
sizes; playback samples are not periodically duplicated or deleted to match clocks.
The playback reserve is about 21 ms, plus DMA and host buffers. Stop drains the
remaining ring, including a partial block, before sending silence.

The prototype PID changes from 0006 to 0007 so Windows enumerates the new audio
function descriptors freshly. COM numbers may change. mic_app source recognizes
both PIDs; older app builds can select the new telemetry COM port manually.
Both capture and playback must be closed before changing noise mode or saving
settings to flash. Beam steering still changes live. Speaker audio is audible
to the microphones; no acoustic echo cancellation is implemented.

Use spkstats for playback fill, packets, received/played frames, underruns,
overruns, DMA faults and peak PCM level. uacstats remains the microphone USB
counter and mic stats checks acquisition. Initial fill and inactive silence do
not count as underruns. tools/uac_playback.py sends a quiet 1 kHz test through
WASAPI exclusive; --peak 0 sends silence for load testing. tools/uac_wasapi.py
continues to capture four microphone channels. System playback volume may be
handled by the host; the device does not advertise a hardware volume feature unit.

The legacy shell tone diagnostic remains disabled in AUDIO_SPEAKER_DIAGNOSTICS;
normal USB speaker playback is independently enabled in this firmware.

## Hardware validation (2026-11-07)

Windows enumerated both audio endpoints. A quiet 1 kHz playback tone was
recorded acoustically by the array (CH1 tone amplitude 5.35 PCM counts,
versus 0.02/0.03 at 900/1100 Hz). This confirms the physical path, rather
than measuring speaker fidelity.

With manual beamforming at 45 degrees, learned full-band suppression and DOA
enabled, a 65-second four-channel capture overlapped 60 seconds of stereo
playback. The diagnostic ramp verified 3,118,608 frames after 1,392 startup
silence frames, with zero channel or sequence errors. WASAPI reported no
packet flags or device-position gaps. Playback received and drained 2,880,390
frames with zero underruns, overruns or DMA faults; microphone acquisition
reported zero drops or faults. Active-stream microphone USB counters also
showed no underruns or overruns at the intermediate check.

One earlier simultaneous endpoint initialization failed with Windows HRESULT
0x8889000f. After restarting the board, opening capture before playback
completed the load test. After that test Windows left the capture terminal
enabled without polling, causing producer overruns after recording ended.
Following a restart, repeated capture opens, repeated playback opens and
capture after playback all passed, with both terminals inactive and all
overrun counters zero afterward. Treat lifecycle behavior under repeated
full-load sessions as requiring further soak testing.

Diagnostic pattern and temporary DSP settings were cleared by restarting
with the same UF2; saved audio settings were preserved. Shell is COM8 and
telemetry COM15 on this PC. The desktop application source accepts PID 0007;
its packaged executable has not been rebuilt in this change.


If an active microphone stream stops completing USB packets for over one
second, firmware forces a 100-ms USB disconnect/reconnect. `uacstats` reports
`usb_recoveries`; a nonzero count means the host recording was interrupted and
must be reopened. This recovery keeps microphone acquisition running; invoking it fails the
duplex regression test. See the [shutdown timing fix](usb-shutdown-fix.md).
