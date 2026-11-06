# Phone speech-plus-noise comparison, 2026-10-16

Reference: `build-tests/reference-speech-noise.mp3` (59.407 seconds), generated
from the known speech WAV and deterministic band-limited stationary noise.
Playback was sequential through a phone at a fixed requested position/volume.
Both captures used broadside beamforming; baseline NR off, suppressed NR mode 2
with 50 Hz notch and a noise profile learned during the initial noise section.

Baseline capture 80 seconds, suppressed 70 seconds, both PCM16/48 kHz, mono
repeated in four UAC slots. Windows reports no flags or position gaps. Both
added zero capture drops/faults, USB overruns/underruns, send errors, or active
busy events. Cumulative overrun=1 and active-busy=63 predated these tests.

The suppressed noise-only RMS is about 15.4 dB below baseline, measured using
baseline seconds 26?36 and suppressed seconds 10?20. Sequential playback and
room variation limit this comparison: it is not a speech SNR measurement.
Source PCM values are low, so suppression and quantization can both reduce
measured noise. Speech preservation has not yet been judged by listening.

Listening copies use a common gain of about 53 dB. Baseline is trimmed by
approximately 16 seconds to correspond to suppressed playback. Waveform
cross-correlation was only 0.028, so precise sample alignment failed; the
coarse timing comes from observed noise/speech transitions. Do not use these
files for waveform subtraction or a claimed sample-aligned speech metric.
Files `speech-noise-listen-{baseline,suppressed}-speech.wav` contain the first
speech section with adjacent noise; full listening copies are also retained.

The board remains in broadside + learned suppression mode, COM14 free.
Machine-readable levels: `build-tests/speech-noise-comparison.json`.
