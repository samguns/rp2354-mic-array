# Direction estimator prototype, 2026-10-14

This host-side prototype estimates azimuth from broadband four-channel audio.
No firmware change or additional hardware recording was needed for this milestone.
It fits all six measured pair delays to a shared plane-wave direction, weighted
by coherence. Source coordinates and predicted delays are never used by the fit.
The common sound speed is 343 m/s. Input is raw four-channel PCM16 at
48 kHz only. Delay reports must explicitly carry `sample_rate_hz=48000`;
legacy or missing-rate reports are rejected.

Angles use the exposed-port underside view: 0 degrees right, 90 top, 180 left,
270 bottom (USB-C upper right). The fitted vector is the in-plane projection
of the direction toward the source. A planar array does not distinguish the
two hemispheres. This prototype does not report elevation or distance.

```powershell
# Analyze a raw 48 kHz recording:
python tools/direction.py --wav build-tests/direction/raw-48k.wav

# Use a current 48 kHz delay report:
python tools/direction.py --delays build-tests/direction/raw-48k.delays.json

# Record three seconds through UAC2 and estimate:
python tools/direction.py --capture build-tests/direction/new.wav
```

The default recording command uses Windows WASAPI exclusive UAC2 at 48 kHz.
Close other audio applications first. In the COM14 shell set `nr config off`
and `bf config off` before capture: direction finding requires four independent
raw channels. Identical mono slots are rejected by the WAV analyzer.
Existing WAVs must contain four-channel PCM16 at 48 kHz. WAV replay does not itself certify
how the input file was acquired. An output JSON contains status, angle or null,
rejection reasons, fitted vector, per-pair residuals and fit RMS. Existing delay
validation files are preserved when running from WAV.

## Rejection rules

All six distinct pairs are required. Nonfinite measurements are invalid.
Every pair needs mean coherence >=0.5, GCC score >=0.5, coherent active band
fraction >=0.4, and a peak away from the search boundary. Clipped input is rejected.
A measured delay beyond its geometry bound plus 9.375 microseconds is rejected.
Fit RMS must be <=9.375 microseconds and maximum residual <=18.75 microseconds.
These equal 0.45/0.90 samples at 48 kHz. Fit residuals are reported in
both samples and microseconds. The fitted
in-plane vector magnitude must be in [0.25,1.05]. Near-zero lateral projection
has no stable azimuth; a magnitude above one beyond allowance is unphysical.
No offending pair is silently removed, and no measured delay is clamped.

Accepted results are labeled provisional; rejected results have a null angle.
Confidence is a qualitative screening label, not a calibrated probability.
Low residual means pair consistency; it does not establish angular accuracy.
These thresholds are engineering screening choices and need more recorded
positions and acoustic conditions before product use.

## 48 kHz host validation, 2026-10-16

Synthetic tests exercise positive/negative delays, a four-channel fractional-delay
48 kHz WAV through all six GCC-PHAT pairs into a known 37-degree plane-wave
direction, required rate metadata, rejection of legacy 16 kHz inputs, tone rejection and
processed-mono rejection. These validate host calculations, not physical
48 kHz acoustic accuracy. No firmware was changed or device settings modified.

## Historical 16 kHz hardware recording results

These archived results describe earlier firmware. Current tools reject those
16 kHz recordings and reports; collect new raw 48 kHz audio for analysis.

| Recording | Result | Explanation |
|---|---|---|
| Center | Rejected | Projection too close to array normal for stable azimuth |
| Right | Rejected | CH2-CH4 beyond physical bound; fitted magnitude >1.05 |
| Top | Rejected | CH1-CH3 beyond physical allowance; pair-fit inconsistency |
| Diagonal | Provisional 53.17 degrees | Fit RMS 0.0145 sample; max residual 0.0266 sample |

The diagonal source was placed approximately at (250,250,250) mm, corresponding
to a nominal 45-degree azimuth. The reported angle differs by about 8.2 degrees.
Source placement, reflections, microphone phase and model approximations are
not isolated, so this is one observed discrepancy, not a calibrated error bound.
Historical replay directly from diagonal.wav reproduced the same result without using
source metadata. The center and flagged near-axis recordings remain rejection
cases. Tests cover all eight compass azimuths using ideal delays, source-truth
independence, near-normal ambiguity, impossible/inconsistent delays, poor
broadband evidence, clipping and missing/duplicate pairs.

Next work: evaluate more known azimuths and repeated placements, then add
rolling-window direction tracking with temporal stability and explicit no-result
states. Moving the analysis onto the board should follow that evaluation.
