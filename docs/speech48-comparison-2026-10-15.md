# Guided speech comparison, 2026-10-15

Three 30-second, 4-channel PCM16/48 kHz recordings were captured using phone
speech playback at the user's fixed position and volume. Files are in
`build-tests/speech48-{raw-baseline,beamformed,suppressed}.wav`.
Raw listening uses CH1; broadside and suppression carry identical mono slots.
Suppression used broadside, an 80 Hz high-pass, 50 Hz notch and a two-second
ambient noise profile, with 16 kHz speech processing resampled to 48 kHz.

All three Windows captures report no packet flags or device-position gaps.
Device acquisition remains at zero drops/faults. Beamformed and suppressed
recordings added no USB overruns, underruns, active-busy events or send errors.
The cumulative overrun counter was already 1 before the beamformed recording;
there is no immediately preceding baseline counter snapshot to attribute it.

Listening copies `speech48-listen-*.wav` are mono and have a common
51.345 dB gain, chosen so the largest peak reaches 80% full scale. No separate
normalization or compression was applied. Raw source peaks are only 71 PCM
counts for CH1, beamformed 62, suppressed 47. This low recorded level limits
assessment of subtle artifacts and exposes PCM quantization when amplified.
Preplayback 1?3 second RMS values are 2.466, 3.129, and 1.075 counts respectively.
These sequential room measurements are not a controlled SNR improvement or
speech-retention measurement; timing and background may differ between takes.

Listen for intelligibility, missing consonants, watery/pulsing artifacts,
and background hiss. Listening judgment is pending. The board is left with
broadside and learned suppression enabled; raw mode is restored with
`nr config off` and `bf config off` while audio capture is closed.

Machine-readable measurements: `build-tests/speech48-comparison.json`.
