> Updated 2026-10-16: host analysis accepts raw 48 kHz PCM16 WAVs only.
> Reports include `sample_rate_hz`; legacy/missing-rate reports are rejected by the direction estimator.
> At 48 kHz use at least about 1.2 seconds of audio (three seconds recommended).
> Reference analysis remains at 400?3200 Hz; 48 kHz input does not imply a
> widened analysis band. Historical acoustic measurements below are at 16 kHz.

# Broadband acoustic delay validation

Use the exposed-port underside coordinate frame: +X right, +Y up, +Z toward
the viewer. USB-C is upper right; CH1 top, CH2 left, CH3 bottom, CH4 right.
Ports have radius 25 mm. Sound speed is assumed 343 m/s. Source coordinates
are approximate acoustic source-center positions relative to the array center.

The reference is deterministic periodic band-limited noise (350-3500 Hz),
mono PCM16 at 48 kHz, 30 seconds, peak 8192 (-12 dBFS), with 10 ms boundary
fades. Repeat playback through the external speaker. It is generated with:

```powershell
python tools/delay_check.py --make-reference build-tests/reference-broadband.wav
```

Record three seconds at known positions, keeping source volume fixed and
sound openings clear. Examples (millimeters):

```powershell
python tools/delay_check.py --source-mm 0 0 300 --output build-tests/delay-validation/center.wav
python tools/delay_check.py --source-mm 400 0 100 --output build-tests/delay-validation/right.wav
python tools/delay_check.py --source-mm 0 400 100 --output build-tests/delay-validation/top.wav
```

Set up the stated position before each command. Close audio applications and
set `nr config off` and `bf config off` in the COM14 shell. The default host
capture uses Windows WASAPI exclusive UAC2 at 48 kHz and saves WAV plus
transport and delay JSON reports. Legacy CDC capture and rate/port selection have been removed from these tools. Existing WAVs can be
reanalyzed with --input and --source-mm. No firmware change is required.

Analysis excludes 250 ms at each recording boundary. It averages cross
spectra over up to 16 non-overlapping Hann windows (8192 samples at 48 kHz) spread through
the remaining audio. Regularized GCC-PHAT, weighted by magnitude-squared
coherence, uses 400-3200 Hz. Its fractional-lag search uses 1/32-sample steps
and local parabolic interpolation. That search grid is not a claim of
measurement accuracy. Positive pair delay means the SECOND channel arrives
LATER than the FIRST, compared against the exact point-source distance
difference. Adjacent-pair maximum delay is about 1.65 samples; opposite-pair
maximum is about 7.00 samples at 48 kHz.

Screening requires error <=25 microseconds (1.2 samples at 48 kHz), lag within the physical bound plus
9.375 microseconds (0.45 sample at 48 kHz), mean coherence >=0.5, GCC score >=0.5, and at least 40% coherent
active bins across the band. A boundary peak or clipping also requires review.
These practical thresholds do not establish statistical confidence. Broadband
coverage rejects a single sine masquerading as a delay reference. Room
reflections, source extent, motion and uncertain source positions can bias the
result. A passing result supports channel alignment and delay signs/magnitudes
at these positions; it does not establish angular accuracy across all directions.
No channel phase correction, gain correction or azimuth estimate is applied.

## Hardware results, 2026-10-14

Four three-second four-channel captures completed on COM13. Each contained
375 consecutive packets with valid checksums, a valid terminal packet and
no clipped samples. Final capture health is included in summary.json.
No firmware changes were required.

| Source X/Y/Z, mm | Consistent pairs | Max absolute geometric error | Max triangle-closure residual |
|---|---|---|---|
| center: [0.0, 0.0, 300.0] | 6/6 | 0.306 samples | 0.049 samples |
| right: [400.0, 0.0, 100.0] | 5/6 | 0.410 samples | 0.386 samples |
| top: [0.0, 400.0, 100.0] | 5/6 | 0.239 samples | 0.590 samples |
| diagonal: [250.0, 250.0, 250.0] | 6/6 | 0.363 samples | 0.041 samples |

The centered source had near-zero relative delays (maximum 0.306 sample,
19.1 microseconds). This does not prove zero intrinsic channel phase offset:
the source center, room and placement are approximate.

For the right source, CH2-to-CH4 estimated -2.672 samples versus -2.262
predicted, outside the geometric opposite-pair bound of 2.332 samples.
The other five pairs were within 0.128 sample of predictions. Triangle
closure up to 0.386 sample also identifies inconsistent pair estimates.

For the top source, CH1-to-CH3 estimated +2.488 samples versus +2.262
predicted. It exceeds the physical bound plus the predefined 0.15-sample
allowance; the screening threshold was not relaxed to pass it. The other
five pairs met screening criteria. Maximum triangle closure was 0.590
sample (see saved pair estimates). These are unresolved acoustic/estimator
bias cases; the measurements do not identify their cause.

The diagonal source passed all six pair checks, with maximum geometric
error 0.363 sample and maximum triangle closure 0.041 sample. Pair delay
signs across both axes support the coordinate/channel map. However, the
0.4-sample screening tolerance is not a claim of angular accuracy, and
these four positions do not validate all possible source directions.

Artifacts are under build-tests/delay-validation (WAV, transport-integrity
JSON, per-position delays JSON and combined summary.json). The reference
is build-tests/reference-broadband.wav. All 30 Python tests pass, including
fractional-delay recovery at -2.2, 0 and +1.65 samples, FFT roundtrip, sign
convention, silence rejection and narrowband-coverage rejection.

Next: a direction prototype that fits physically possible delays across
pairs, reports confidence/residuals, and refuses inconsistent solutions.
The flagged near-axis captures must remain regression cases rather than
being silently clamped or used to derive channel correction factors.
