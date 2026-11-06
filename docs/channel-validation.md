# Acoustic channel mapping

All position labels use the exposed-port underside view, with the same top edge up:
USB-C upper right and MAX98357A lower right. Top=CH1/U1, left=CH2/U2,
bottom=CH3/U3, right=CH4/U4. See array-geometry.svg.

Use the verified build-tests/reference-1000hz.wav on repeat through an external
speaker. For each position, bring that sound opening 1-2 cm from the source,
without touching or covering it. Move the board near a fixed source if easier.
Keep the source level fixed and hold the board steady during each three-second
capture. Close other applications using COM13.

```powershell
python tools/check_channels.py top
python tools/check_channels.py left
python tools/check_channels.py bottom
python tools/check_channels.py right
```

Run each command only after physically placing the source at the labeled port.
The tools save WAV, stream-integrity JSON, and mapping JSON under
build-tests/channel-mapping. An existing capture can be analyzed with
`python tools/check_channels.py top --input path/to/top.wav`.

Analysis excludes 250 ms at each boundary, uses a Hann window, and compares
1 kHz amplitudes. A provisional clear ranking requires at least 3 dB lead over
the next channel, at least 10 dB over nearby spectral bins in every channel,
and no clipped samples. These are practical screening criteria, not statistical
confidence or a manufacturer specification. Only a clear expected winner is
labeled consistent; weak separation is inconclusive. A mismatch calls for
checking placement and repeatability before changing the electrical channel map.
Large speakers, reflections and port directivity can prevent a clean ranking.
Do not normalize channels from these near-field captures.

After all four positions have been measured, compare the complete response
matrix for a repeatable location/channel association. Then perform an equidistant
source sensitivity check and broadband delay tests. Physical results are recorded below; preparing the tool alone is not validation.

## Hardware mapping results, 2026-10-14

All four three-second captures passed stream integrity checks (375 consecutive
blocks each), with no clipped samples. Each near-port position selected the
expected channel. This supports the physical mapping in the exposed-port frame.

| Source near | Expected/strongest channel | Lead over runner-up |
|---|---|---|
| top | CH1 | 5.21 dB |
| left | CH2 | 6.40 dB |
| bottom | CH3 | 5.15 dB |
| right | CH4 | 4.44 dB |

Recordings and per-position results are under build-tests/channel-mapping.
These near-field levels must not be used as sensitivity correction factors.

## Centered source comparison

Source positioned approximately 30 cm in front of the exposed-port center.
The three-second capture passed integrity checks and had no clipped samples.
All channels had a clear 1 kHz line, 48.8-50.3 dB above the selected nearby
spectral bins. This is a spectral-bin comparison, not a broadband SNR claim.

| Channel | 1 kHz peak amplitude, PCM | Relative to mean tone level |
|---|---|---|
| CH1 | 12.14 | +0.39 dB |
| CH2 | 12.58 | +0.71 dB |
| CH3 | 11.69 | +0.07 dB |
| CH4 | 10.14 | -1.17 dB |

Total channel spread: 1.88 dB. These levels include source placement, room
reflections and microphone response; they do not establish intrinsic
sensitivity mismatch. No gain correction was applied. A repeated centered
comparison after rotating the board can help distinguish spatial effects
from a persistent channel offset if calibration becomes necessary.

All five captures (four port positions plus center) passed integrity checks.
Next milestone: broadband acoustic-delay validation using the mirrored geometry
and confirmed channel map before implementing an azimuth estimator.
