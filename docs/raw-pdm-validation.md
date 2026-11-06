# Raw PDM and clock validation

## Findings

No large clock-frequency error, incorrect capture phase, or raw-to-PCM
conversion mismatch was found. A confirmed external 1 kHz playback attempt
still produced only a weak 1 kHz component in the independently decoded
raw bitstream. The missing reference therefore cannot be attributed to
corruption between the captured PDM snapshot and its PCM export.

This does not establish microphone sensitivity or acoustic quality. It
also does not measure supply voltage, analogue edges, microphone part
identity, assembly condition, or signals at the microphone package pads.

## Firmware diagnostics

`mic raw` performs these bounded measurements while normal capture runs:

1. Count rising edges of GPIO6 with a separate PIO state machine over an
   approximately 20 ms window.
2. Snapshot four consecutive normal DMA blocks: 6144 packed words, or
   49152 PDM ticks / 512 PCM frames / 32 ms at nominal settings.
3. Save the matching 2048 signed PCM16 samples produced by the running
   firmware filter. A discontinuity invalidates the raw request.
4. Independently sample all four data pins in the low and high clock
   half-cycles, approximately 140 ns after detecting each edge.
5. Sample GPIO6 once per system clock for 65536 samples, approximately
   0.437 ms at nominal 150 MHz, to inspect digital high/low run lengths.

The clock counter, phase probe, and clock trace share one state machine.
The phase/clock traces reuse the existing I2S probe buffer; no microphone
or speaker pin directions change. If all speaker and raw diagnostics have
been used, PIO0 uses all four state machines and all 32 instruction slots.
PIO1 and PIO2 are not used by this application.

## Clock results

The final clock trace (`build-tests/raw-clock-timing/clock-trace.json`)
contained:

| Measurement | Result |
|---|---|
| High duration | 48 or 49 system cycles |
| Low duration | 48 or 49 system cycles |
| Full period | 97 or 98 system cycles |
| Mean digital duty cycle | 49.984% |
| Mean frequency relative to nominal 150 MHz | 1,536,004 Hz |
| Approximately 20 ms edge-counter result | 1,535,164 Hz |

The edge counter includes software start/stop timing overhead, explaining
its lower precision. Neither measurement is an absolute calibrated
frequency measurement: both rely on the board's timebase. The clock trace
matches the configured fractional divider and does not support a large
sample-rate error such as moving a 1 kHz input to 832 or 1109 Hz.

This internal GPIO trace cannot certify analogue rise/fall times, ringing,
or clock quality at the microphone pins. An oscilloscope at those pins is
needed for those checks.

## Data phase and activity

During the confirmed external-tone raw capture:

- Each lane had approximately 50% ones and a transition fraction of
  70.1–71.3%, consistent with an active near-zero-density PDM stream.
- Channels 2–4 differed from channel 1 at 49.0%, 52.2%, and 52.2% of ticks.
  They were neither stuck levels nor identical copied channels.
- Low-half and subsequent high-half samples matched at every observed tick.
- Low-half samples differed from the previous high-half samples at roughly
  69.7–72.6% of ticks, showing the new bit arriving in the low half-cycle.

This supports the existing low-half sampling for grounded LR/SELECT, as
shown in Figure 11 of the [Infineon microphone datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-im72d128v-datasheet-en.pdf).
Agreement during the high half does not prove the microphone actively
drives then: the pin may retain the previous level after going high impedance.

## Independent PCM reconstruction

`tools/raw_pdm.py` processes each PDM bit independently with unbounded
Python integer integrators and comb delays. It uses no firmware lookup
tables and does not depend on the firmware's modulo-overflow state.

The first four frames are excluded because the running filter's prior
state is not included in the snapshot. **Every subsequent PCM sample on
all four channels matched exactly: maximum error 0.** This result held
for both ambient and confirmed external-tone captures.

For the confirmed external-tone capture, the independently reconstructed
1 kHz amplitudes were only 0.686, 0.541, 0.796, and 0.554 PCM counts across
the four channels. These short-window numbers are not calibrated SNR or
distortion measurements; leakage from other signals can affect them. They
do not establish a strong reference suitable for THD analysis.

## Files and reproduction

```powershell
python tools/raw_pdm.py COM13 --output build-tests/raw-pdm
python -m unittest discover -s tests -p 'test_*.py' -v
```

The serial terminal must be closed. Each run writes:

- `raw.json`: nominal PDM rate, decimation, packed raw words, matching PCM.
- `phase.json`: low/high phase GPIO samples.
- `clock-trace.json`: single-bit GPIO6 samples at system-clock rate.
- `analysis.json`: reconstruction error, lane activity, phase comparisons,
  clock measurements, and final capture health.

Saved runs include `raw-external-1000`, `raw-clock-timing`, and `raw-repeat`
under `build-tests/`. Raw exports validate lengths and hexadecimal content;
they do not include an additional transport checksum.

All sixteen Python tests and both native C test executables passed. The
new tests cover constant polarity, alternating idle data, a deliberately
corrupted PCM sample, a real hardware snapshot, and malformed/truncated
raw exports. The firmware builds with 122988 bytes of flash and 451736
bytes of RAM (84.84%).

Resource reuse was exercised in both directions: a full `mic raw` run,
then `speaker verify 1000 64 i2s`, then another full `mic raw` run. The
speaker GPIO check passed with maximum PCM error 1.002 counts. The repeat
raw check again matched PCM exactly and measured 49.983% clock duty at
nominal 1,535,993 Hz. Final health was `ready=1 dropped=0 faults=0
max_process_us=5033`. COM13 was released with microphone capture running
and speaker output stopped.

## What remains

The digital acquisition checks have narrowed the problem, but they have
not identified an acoustic or analogue root cause. The next useful bench
checks are microphone VDD/GND, clock/data at the package pads, actual
microphone markings/orientation, and an independent acoustic reference
measurement at the openings. Do not change capture phase or add arbitrary
software gain to hide the failed reference test. The prior onboard-speaker
harmonics remain unvalidated as amplifier or speaker distortion.
