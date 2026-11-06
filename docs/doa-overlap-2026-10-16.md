> Subsequent desktop/telemetry milestone: [mic_app](mic-app-2026-10-16.md).
> Resource and timing numbers below describe the original overlap release.

# Overlapped direction estimation, 2026-10-16

Implemented, flashed and verified with simultaneous 48 kHz four-channel UAC2,
broadside beamforming and full-band noise suppression.

| Measurement | Previous full-band firmware | Current firmware |
|---|---|---|
| Completed-result interval | About 330 ms | About 171 ms (5.8 Hz) |
| Observed maximum interval | 339 ms | 174 ms |
| Newest sample to result publication | About 130 ms | About 44 ms; maximum 47.009 ms |
| Analysis duration | 170.667 ms | 170.667 ms |
| Active core-1 estimation work | About 45 ms | About 32 ms |

Publication occurs about 129 ms after the analysis window midpoint. A polled
result also includes time since its publication. These are measured scheduling
figures, not hard deadlines or calibrated acoustic latency: sample-end timing
comes from the DMA completion ISR. Filter group delay is not subtracted.

## Implementation

Capture on core 0 fills two 2048-frame, four-channel PCM buffers (16 KiB each).
Core 1 transforms each completed window while capture fills the other buffer.
Four consecutive windows accumulate into the same six-pair GCC-PHAT estimator;
the PCM slot is released before the final correlation scan and direction fit.
The previous collection pause is removed. No DMA buffer is retained.

The queue never overwrites a buffer being read. If overloaded, it replaces old
ready work; sequence gaps reset the partial estimate rather than combining
nonconsecutive windows. Configuration epochs discard obsolete results. The
existing 64 KiB diagnostic mirror is filled only for one-shot capture/dump.

The priority-1 controller performs bounded handoffs; bare core 1 performs the
FFT/fit and cooperatively services CIC conversion. Packed real FFTs now fuse
radix stages. Noise suppression hoists FFT twiddle stride division out of its
butterfly loop. Neither optimization changes the intended numerical algorithm.
Full-band suppression and USB audio continue independently of direction results.

`doa stats` adds `DOA_PIPELINE` queue/reset counters and `DOA_LATENCY` sample-end,
midpoint and publication ages. `active_us` excludes CIC service; `wall_us` spans
incremental work and waiting, and is not the completed-result interval. All age
fields use the same stats snapshot; maxima are cumulative since boot.

## Validation and resources

- 61 Python/native tests pass, including queue ownership, overload, sequence
  wrap, configuration epochs, production estimator math and full-band NR.
- `build-tests/overlap/final-pattern.*`: 60 seconds, 576 startup zero frames,
  then 2,879,424 consecutive frames; zero sequence/channel errors. Windows
  reported no flags or position gaps.
- `final-health.txt`: zero capture drops/faults, analysis overwrites/resets,
  USB underruns/overruns/gaps/send/pool errors. Two transient active-busy USB
  returns occurred; the diagnostic frames remained consecutive.
- `final-frame.doa.json`: maximum same-frame host/onboard pair difference
  0.000955 sample; both reject the ambient frame. This verifies numerical
  agreement, not acoustic angle accuracy.
- `processed-audio.*`, `raw-audio.*`, `audio-check.json`: five seconds each of
  actual ambient audio; processed slots match and raw channels differ, with
  nonzero levels and no Windows flags/position gaps. This is a routing check,
  not a speech-quality or calibrated suppression measurement.

Flash: 159652 bytes. RAM: 479968/532480 bytes (90.14%), leaving 52512 bytes
(51.3 KiB). The two-window queue occupies 32828 bytes including metadata.
UF2 SHA256:
`77F57A6E7686A4F6FEE223A59B6B4C66E5DF4AB0DA8EADB5D53CEEAE0AC13A48`.
Recovery image: `build-tests/pre-doa-overlap.uf2`.

Board left with broadside, learned suppression and continuous DOA enabled,
diagnostic pattern off, COM14 free. Reboot defaults remain raw audio and DOA off.
Known-position acoustic validation, calibrated confidence and automatic steering
remain pending. The estimator still uses the nominal exposed-port geometry,
400–3200 Hz analysis band and all six pairs; no accuracy claim is added.
