# Audio validation — 2026-10-13

Follow-up: [raw PDM and clock validation](raw-pdm-validation.md) found a
nominal 1.536 MHz / 50% clock, the expected data update phase, and exact
agreement between independent raw-bit reconstruction and firmware PCM.
Acoustic validation remains open.

## Outcome

The PIO transmitter produces the requested signed waveform with correct
I2S framing at the Pico GPIOs. The PDM software filter passes independent
integer and synthetic-signal checks. Microphone capture is stable at a
nominal 16000 frames/s with more processing margin than the first build.

**Acoustic validation is still open.** Onboard-speaker recordings contain
large harmonics, and the external-reference attempts did not produce a
clear steady 1 kHz line. These results do not establish which analogue,
acoustic, assembly, or measurement condition is responsible. They do not
constitute a calibrated speaker THD or microphone sensitivity measurement.

## Hardware and reference documents

- Pico 2 / RP2350; COM13 USB CDC.
- Four IM72D128 microphones, GP6 clock and GP7–GP10 data.
- Yoursee-MAX98357A, GP11 DIN / GP12 BCLK / GP13 LRCLK. The module
  photograph `max98357A.png` shows the advertised label: 9 dB gain, mono
  `(L+R)/2` mix. The vendor datasheet is not included in this repository.
- Passive `XHXDZ-3525-4Ω3W-2P1.25`: 4 ohms ±15%, 2.5 W rated, 3 W maximum,
  resonance 980 Hz ±20%. Its response is specified from resonance upward,
  so 500 Hz is not a suitable standalone quality acceptance frequency.
  The vendor sheet is not included in this repository.
- User confirmed both speaker leads are across SPK+/SPK−, not GND, and
  confirmed four open sound-port holes under the assembled microphones.
- [Infineon microphone datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-im72d128v-datasheet-en.pdf):
  Figure 11 supports late-low sampling for grounded LR; Table 5 includes
  1.536 MHz normal mode; Figure 14 recommends a 0.8 mm PCB sound opening.

The extracted EasyEDA footprint contains documentation-layer port circles
and SMD pads with no drill field. A missing-port hypothesis was therefore
checked with the user and rejected for the assembled board. The project
data alone should not be used to contradict that physical confirmation;
check manufacturing drill exports if revising the PCB.

## Digital output check

`speaker verify` allocates a separate PIO state machine. It waits for
BCLK falling/rising transitions and captures DIN/BCLK/LRCLK at rising
edges through GPIO inputs. Each 32-bit DMA word carries eight 3-bit
samples. The probe captures 16384 bit-clock edges: 256 stereo frames
(16 ms at nominal 16 kHz), during stimulus fade-in.

The host checks:

1. BCLK high at sampled edges.
2. Exactly 32 bit clocks per LRCLK half-period.
3. One-bit I2S delay between LRCLK change and the next MSB.
4. Alternating left/right slots, zero low 16 bits of each 32-bit slot.
5. Decoded signed PCM values against the requested sine and fade.

Observed maximum decoded PCM error, peak 512:

| Requested tone | Maximum error (PCM counts) | Result |
|---|---:|---|
| 500 Hz | 1.34 | Pass |
| 750 Hz | 3.42 | Pass |
| 1000 Hz | 1.01 | Pass |

The difference is consistent with table interpolation and integer rounding.
Peak-64 tests and digital silence also passed after fixing the signed
amplitude-scaling regression introduced during diagnostics. A dedicated
host test now checks negative samples, peak bounds, fades, and frequencies.

The probe verifies GPIO logic and framing. It does **not** measure analogue
voltage levels, edge ringing, absolute clock jitter, the signal after the
PCB interconnect, or amplifier output voltage. The short probe also does
not certify uninterrupted electrical timing over the entire one-second tone.

## Acoustic checks

Initial 2.048 MHz capture, peak-512 I2S stimulus:

| Input tone | Recorded third harmonic relative to fundamental, across 4 channels |
|---|---|
| 500 Hz | +37.5 to +39.7 dB |
| 750 Hz | +11.6 to +14.6 dB |
| 1000 Hz | +4.6 to +6.3 dB |

Reducing the digital peak to 64 did not remove the strong 1.5 kHz response
at 500 Hz. Left-justified framing did not remove that anomaly either.
Digital silence with clocks running recorded near the noise floor (roughly
2–3 PCM counts RMS). These are uncalibrated acoustic observations: speaker
position, room response, and mechanical/electrical coupling were not held
to a laboratory setup, so the figures cannot be interpreted as amplifier THD.

With normal-mode 1.536 MHz PDM / decimation 96 and peak-64 I2S:

| Input tone | Recorded third harmonic relative to fundamental |
|---|---|
| 500 Hz | +38.7 to +40.2 dB |
| 1000 Hz | −7.3 to −5.4 dB |

External attempts were made with MAX98357A output stopped. One recording
had stronger peaks around 554, 830, and 1108 Hz rather than the requested
steady 1 kHz. A subsequently generated 48 kHz WAV containing a known
1 kHz sine also did not yield a clear 1 kHz line at the array (measured
1 kHz amplitudes about 0.03–0.08 PCM counts against 2.6–2.8 counts RMS).
It fails the reference-signal criterion and cannot clear the microphone
hardware or isolate the amplifier as the cause.

A further continuous PC-tone capture was made with the serial terminal
closed and MAX98357A output off (`build-tests/pc-1000-continuous.wav`). All
four channels had their strongest lines near 832 and 1109 Hz, with other
peaks near multiples of 277 Hz. At exactly 1 kHz, amplitudes were only
0.047–0.070 PCM counts against 8.4–9.2 counts RMS. This also fails the
clear-1-kHz reference criterion; harmonic ratios at 1/3 kHz must not be
reported as a valid distortion measurement. Application/output-device
confirmation was requested before repeating the measurement.

The user subsequently confirmed the PC source/output path and repeated
the test with the array raised so its underside openings were clear of
the desk. `build-tests/pc-1000-open-ports.wav` still did not establish a
clear 1 kHz line: amplitudes were 0.062, 0.065, 0.092, and 0.092 PCM counts,
against 9.33, 8.67, 13.63, and 12.74 counts RMS respectively. Underside
obstruction therefore did not explain this capture. A valid acoustic
distortion measurement remains unavailable; investigate the physical PDM
capture path and independently measured clock/data timing before treating
these harmonic ratios as THD or attributing them to the amplifier.

Saved captures and raw results are under `build-tests/`:

- `audio-validation/`: three original I2S traces, tone WAVs, and report.
- `audio-silence/`, `audio-quiet/`, `audio-left/`, `audio-normal-mode/`.
- `external-1000.wav`, `external-1000-near.wav`,
  `external-reference-1000.wav`, with JSON spectral measurements.
- `reference-1000hz.wav`: generated 30-second reference, mono PCM16 / 48 kHz.

## Capture and processing

The default now uses 1.536 MHz PDM and CIC3 decimation 96. With the nominal
150 MHz system clock, divider 3125/256 gives nominal 16000 PCM frames/s.
The original 2.048 MHz / decimation 128 configuration gave about 15998.294
frames/s. Physical oscillator tolerance and fractional-divider jitter remain.

The normal-mode raw block contains 1536 words, producing 128 PCM frames
every 8 ms. The backing ring stays 8 KiB aligned, with metadata outside
the ring, preserving bounded writes on a missed DMA rearm deadline.

A 60.125-second test observed blocks increasing from 31942 to 39455,
approximately 125 blocks/s. Every health sample had `ready=1 dropped=0
faults=0`; a final one-second export returned exactly 16000 frames / four
channels / 128000 payload bytes. The observed worst processing interval
was 5227 us, about 65% of the 8 ms interval. This is a wall-time observation,
not a general CPU utilization measurement or an unlimited-duration guarantee.

The final flashed build's 1 kHz / peak-64 GPIO check passed with maximum
decoded error 1.002 PCM counts. Its post-test health was `rate=16000
channels=4 ready=1 blocks=1427 dropped=0 faults=0 max_process_us=6585`.
That run's uncalibrated third/fundamental acoustic ratios ranged from
−2.5 to +1.1 dB, further underscoring that a clean acoustic result has not
been established. Speaker clocks and data were stopped after the test;
COM13 was released and microphone capture was left running normally.

The link report uses 120548 bytes of flash and 427160 bytes of RAM (80.22%).
Most diagnostic overhead is the 128 kB one-shot speaker waveform and 8 KiB
probe buffer. Streaming playback should use smaller DMA buffers.

## Host verification and reproduction

The CIC3 test checks a long random stream against an independent bitwise
reference, repeated modulo overflow, block continuity, channel isolation,
DC gain, and an independently generated dithered sigma-delta sine. At the
current decimation, recovered amplitudes were 652.879 counts at 500 Hz and
3.732 counts at 1500 Hz. This rules out a large third harmonic introduced
by this software filter for that synthetic input; it does not validate the
physical microphone bitstream.

The tone generator test covers 200, 500, 750, 1000, and 2000 Hz at peaks
0, 1, 64, and 512. Ten Python tests cover framing, wrong format, corrupt
samples, a saved hardware trace, and PCM export validation.
Both native C test executables and all ten Python tests passed after the
final build was flashed and exercised.

```powershell
cmake --build build
python tools/flash.py COM13
python tools/verify_audio.py COM13 --frequencies 500 750 1000
python tools/verify_audio.py COM13 --frequencies 1000 --peak 64 --output build-tests/audio-quiet
python tools/verify_audio.py COM13 --frequencies 1000 --peak 0 --output build-tests/audio-silence
python tools/record.py COM13 external.wav --ms 1000
```

Keep serial terminals closed while these scripts own COM13. No tone runs
automatically after flashing or reboot.

## Remaining acceptance work

Obtain an independently confirmed acoustic reference at the microphone
openings, or measure a captured raw microphone stream and the amplifier
output using bench equipment. At the module input, verify BCLK/LRCLK/DIN
voltage levels and timing; measure the bridged speaker output differentially.
Compare the external reference with onboard playback while avoiding common
mechanical coupling. A clean audio-quality claim remains deferred until
that comparison passes.
