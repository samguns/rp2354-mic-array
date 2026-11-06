> Current transport: [dedicated CDC service](cdc-telemetry-service.md), telemetry COM18, shell COM20.
> Earlier transport/resource measurements below describe their original checkpoint.

> Current implementation: [overlapped estimation and validation](doa-overlap-2026-10-16.md).
> About 171 ms between results under full DSP/USB load; observed maximum 174 ms.

# Optional onboard direction estimator

The Pico estimates provisional azimuth from the four **raw** 48 kHz microphone
channels, before beamforming and speech suppression. The feature is off at boot
and does not steer the beamformer. Processed USB output can remain enabled while
estimation reads its independent raw input.

In the COM14 shell:

```text
doa config on
doa stats
doa config off
```

`doa stats` reports cumulative updates and accepted estimates, result age,
elapsed processing time, six pair delays and quality metrics. `angle_mdeg` is
degrees times 1000, or -1 when no direction is accepted. Rejected results never
publish a usable angle. Status `provisional` means the engineering checks passed,
not calibrated acoustic accuracy. Angles use the exposed-port view: 0 right,
90 top, 180 left, 270 bottom, with USB-C at upper right.

The rejection mask combines:

| Bit | Meaning |
|---|---|
| 1 | Raw frame RMS below 4 PCM counts |
| 2 | Clipped PCM |
| 4 | Insufficient broadband coherence/coverage, low GCC score or boundary peak |
| 8 | A measured delay exceeds its physical bound plus 0.45 sample |
| 16 | Pair delays do not fit one direction, or fit is degenerate |
| 32 | Source projection too close to array normal for stable azimuth |
| 64 | Fitted direction exceeds the unit-vector allowance |

The energy threshold is an uncalibrated gate, not a sound-pressure threshold.
The array cannot identify which side of its plane a source occupies. No distance
or elevation is reported. Known-position acoustic validation remains pending.

## Processing and ownership

Capture fills two 2048-frame (16 KiB each) PCM buffers while core 1 incrementally
accumulates four consecutive windows. PCM slots are released before final fitting.
The controller uses priority 1 for bounded handoffs; bare core 1 performs DSP and
cooperatively services CIC conversion. A separate 64 KiB mirror supports one-shot
capture/dump only. No DMA buffer is retained. Overload replaces old ready windows;
sequence gaps reset partial estimates and epochs invalidate obsolete results.
Core 1 uses no Zephyr services, allocation, USB or DMA access.

Each snapshot averages four non-overlapping 2048-point Hann FFT windows over
400–3200 Hz (bins 18–136). All six channel pairs use regularized GCC-PHAT weighted
by magnitude-squared coherence. Pairs of real channels share one complex FFT,
reducing the FFT count from 16 to eight per snapshot. A 1/4-sample grid plus local parabolic refinement
feeds the same weighted plane-wave fit and screening thresholds as the host.
Coordinates are the nominal 25 mm-radius exposed-port geometry. No pair is
discarded or clamped. The fine grid does not establish timing accuracy.

Continuous collection has no inter-frame pause. Completed results arrive about
every 171 ms. `DOA_TIMING active_us` excludes CIC service; `interval_ms` measures
completed updates. `DOA_LATENCY publish_end_us` measures newest DMA sample to
publication (about 44 ms). `end_age_us` also includes time since publication;
`midpoint_age_us` adds half the 170.667 ms analysis duration. Ages share one stats
snapshot. `DOA_PIPELINE` reports discarded windows and continuity resets. Maxima
are cumulative since boot, not guaranteed deadlines. See the current validation
report for resource usage and measured limits. Settings are not persisted.

## Same-frame comparison with the host

Close UAC audio applications, leave COM14 free, and run:

```powershell
python tools/doa_check.py --output build-tests/doa/host-comparison.wav
```

This performs `doa capture`, waits for one completed estimate, leaves estimation
off, and uses `doa dump` to export that exact raw frame. The tool saves the WAV,
shell output, and a `.doa.json` report comparing all six onboard delays with the
independent host FFT using matching 2048-point windows. The normal host direction
tool uses longer 8192-point windows, so this comparison invokes its matched-window
API directly. A pair difference under 0.05 sample is the numerical check; it is
not an acoustic angular-accuracy guarantee. The onboard low-level gate is an
additional check beyond the host's fit.

The portable production C estimator is tested against the host using synthetic
fractional-delay broadband audio, cardinal directions and a 37-degree source.
Tests also exercise silence, tones, clipping, impossible delays, near-normal
ambiguity and independent noise. Device measurements are recorded below after
firmware validation.

## Historical optimization checkpoint, 2026-10-16

See [optimization measurements](doa-optimization-2026-10-16.md). Typical update
interval under simultaneous UAC2, beamforming and suppression is 344-360 ms,
with a 576 ms maximum observed during the test. Processing elapsed was at most
168 ms, with about 46 ms of core-1 work excluding CIC service. This improves
responsiveness but does not establish a hard real-time deadline or acoustic
accuracy. Raw UAC remains 48 kHz; internal FFT windows and the analysis band are
unchanged. RAM at that checkpoint was 449312/532480 bytes (84.38%), about 81 KiB free.

## Historical first implementation validation, 2026-10-16

Final UF2 SHA256:
`1C36446AB1B2438DCBDAA0EF6073AE91ACDF47C4B135050915F717DA7E8DA224`.
Flash 156528 bytes; RAM 447360/532480 bytes (84.01%), leaving 85120 bytes
(about 83 KiB). Estimator buffer/workspace/stack total is 114084 bytes; linker
padding, thread metadata and SRAM hot code also contribute to overall usage.

- 57 Python/native tests pass. Portable production C is compiled and compared
  against the independent host FFT on synthetic audio.
- `build-tests/doa/final-frame.doa.json`: maximum pair difference
  0.000857 sample on the same real raw frame, with beamforming and suppression
  enabled independently. Both estimators rejected the frame as near normal.
- `final-full-load-pattern.wav` and `.pattern.json`: 60 seconds with DOA,
  broadside, learned suppression and UAC2 enabled; 528 startup zeros then
  2879472 consecutive frames, zero sequence/channel errors. Device acquisition
  had zero drops/faults, and USB had zero underruns/overruns, sequence gaps,
  send errors or pool errors. A small number of transient active-busy returns
  occurred (two added in this 60-second run); queued frames remained continuous.
- `processed-audio.wav`: 15 seconds of actual microphone audio with all DSP
  enabled, identical mono slots. `raw-audio.wav`: five seconds, distinct raw
  slots with DOA still enabled. Both Windows reports have no flags/position gaps.
- Typical elapsed estimation was about 150 ms with raw capture and no UAC,
  245?250 ms with suppression and no UAC. Under suppression plus UAC one job
  took 3719 ms. This is opportunistic estimation, **not sustained 5 Hz** under
  full load. Always inspect `age_ms`; an old angle must not drive steering.

Early builds failed acquisition continuity even with DOA disabled. Moving the
remaining main capture loop and multicore handoff/worker hot paths into SRAM
restored continuity; the final firmware passed the tests above. Intermediate
failed WAVs were removed after retaining their transport/health reports. Windows
WAV completion alone does not establish device continuity.

Known-source physical position validation, calibrated confidence, and automatic
beam steering are still pending. At that checkpoint the board was left with DOA on,
broadside on, suppression on (ambient profile learned), diagnostic pattern off,
and COM14 free. Boot defaults remain raw capture with DOA off.
