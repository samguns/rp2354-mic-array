> Current timing and RAM: [overlapped estimation checkpoint](doa-overlap-2026-10-16.md).
> Measurements below describe their original milestone.

# Onboard direction latency optimization, 2026-10-16

Final image SHA256:
`472B0800361496BBB2E8917349FD02FAA029AF4FFD536E45CC35EBB76EBA76BB`.
Flash 157488 bytes; RAM 449312/532480 bytes (84.38%), leaving 83168 bytes
(about 81 KiB). The pre-optimization image is `build-tests/pre-doa-optimization.uf2`.

## Change

Previously the estimator was a low-priority core-0 job competing with USB and
suppression. Its elapsed processing reached 3719 ms under full load. Now a
core-0 controller submits one frozen raw snapshot to bare core 1. The scalar
FPU is enabled on core 1. During estimation, cooperative hooks service pending
CIC work throughout energy accumulation, FFT butterflies and correlation scans.
The bare worker never calls Zephyr services, allocates memory or touches USB/DMA.
Release/acquire atomics protect job publication, completion and snapshot lifetime.
Core 0 remains responsible for FIR conversion, suppression, USB and shell work.

Pairs of real microphone channels share a complex FFT and their separate
spectra are recovered algebraically. This halves the FFT count from 16 to eight.
The correlation scan uses a quarter-sample grid with local parabolic refinement,
and precomputed initial phases/rotation steps replace runtime grid trigonometry.
Raw 48 kHz capture, 8192-frame snapshots, four 2048-point Hann windows, the
400-3200 Hz analysis band and all rejection thresholds remain the same.

## Measured behavior

| Metric | Optimized result |
|---|---|
| Typical completed-update interval with UAC + broadside + suppression | 344-360 ms |
| Maximum completed-update interval observed | 576 ms |
| Maximum processing elapsed observed | 168 ms |
| Core-1 work excluding CIC service | approximately 45-47 ms per estimate |
| Largest CIC publication-to-service wait observed | 594 microseconds |
| Same-frame pair-delay difference from independent host, final comparison | 0.000827 sample maximum |

The update interval includes the 170.7 ms sample window and collection pause;
it must not be confused with processing elapsed. The 576 ms maximum includes
startup/scheduling effects; no hard 500 ms deadline or sustained 5 Hz is claimed.
`doa stats` now reports `DOA_TIMING core=1 active_us=... interval_ms=...` and
cumulative maxima. Active time is core-1 elapsed work minus cooperative CIC
service, including memory/bus stalls; it is not a cycle-perfect instruction count.

## Validation

- All 59 host/native tests pass, including production C against independent host
  DSP on synthetic directions and rejection cases. Added CDC protocol tests
  ensure an old prompt cannot terminate a result read early.
- `build-tests/doa/optimized-full-load.wav`: 60 seconds with DOA, broadside,
  learned suppression and UAC2 enabled. After 576 startup zeros, 2879424 frames
  are consecutive with zero sequence/channel errors. Windows flags/gaps are empty.
- `optimized-load-after.txt`: capture drops/faults zero; USB underruns, overruns,
  sequence gaps, send errors and pool errors zero. Two transient active-busy
  returns were observed, with all queued samples preserved by the pattern test.
- `optimized-live-timing.json` contains live telemetry during that run. Some early
  reads are incomplete due to a pre-existing prompt-framing issue; only complete
  timing records are usable. The command reader was subsequently fixed and tested.
  The cumulative maxima in `optimized-load-after.txt` are the authoritative bounds.
- `optimized-final-frame.doa.json` compares the exact real snapshot against the
  matching-window host estimator. Both reject that frame for inadequate broadband
  evidence/near-normal ambiguity; numerical agreement does not prove an angle.
- `optimized-processed-audio.wav`: five seconds of actual microphone audio, four
  identical processed mono slots; Windows flags/gaps empty and PCM nonzero.

The board remains DOA on, broadside on and suppression on with the ambient
profile learned during validation; diagnostic pattern off, COM14 free. Boot
defaults remain raw capture and DOA off. Physical angle accuracy, calibrated
confidence and automatic beam steering are still pending.
