> Current timing and RAM: [overlapped estimation checkpoint](doa-overlap-2026-10-16.md).
> Measurements below describe their original milestone.

# Onboard noise reduction, 2026-10-14

## Current full-band suppression, 2026-10-16

`nr config suppress` now processes the selected microphone or broadside beam
**directly at 48 kHz**, with no speech-band decimation or interpolation. The
20 Hz high-pass is followed by optional 50/60 Hz hum rejection and learned
stationary-noise suppression over all 129 nonnegative bins of a 256-point real
FFT. Processed mono is duplicated into the four UAC2 slots. Raw mode preserves
four independent microphones; DOA continues to use raw pre-DSP samples.
Filter-only mode retains its 80 Hz high-pass and optional hum notch.

The real FFT uses a packed 128-point complex transform and real-spectrum
reconstruction, reducing the CPU cost sufficiently to run on core 0 with UAC.
The square-root Hann window and 50% overlap reconstruct the unsuppressed signal
with a 128-sample delay (2.667 ms), in addition to capture and USB buffering.
This replaces the old approximately 17 ms speech-branch delay. The 16 kHz
speech resampler, coefficients, generator and obsolete resampling test have
been removed from the active implementation.

Close audio applications, then in COM14:

```text
bf config broadside
nr config suppress 50 1
nr learn
nr stats
```

Keep desired audio absent for the two-second learning period (750 hops), then
record at 48 kHz. Use hum=0 for the widest untouched passband, or 50/60 to reject
known mains hum. `NR_FORMAT` reports the current processing format, low cut and
algorithmic delay. To get four raw channels, close audio apps and run
`nr config off` and `bf config off`. Settings and profiles reset on reboot.

This is stationary-profile spectral suppression, not a neural speech denoiser.
The 187.5 Hz FFT-bin spacing limits frequency selectivity; desired sound sharing
learned noise bins may be attenuated. The minimum spectral gain is approximately
-18 dB, with temporal smoothing. Learning desired audio can suppress that audio.
Full-band means frequencies above the speech band are processed and can survive;
it does not promise transparent treatment of every signal or measured acoustic
bandwidth. New-version speech/listening quality still needs a comparison.

### Validation

- All 60 host/native tests pass. Independent time-domain high-pass comparison
  gives a maximum reconstruction error of one PCM count on random broadband
  data, with selected-channel isolation.
- Native production-C tones: 20 Hz retains 703.5 of 1000 peak counts (expected
  20 Hz low-cut response); 1/8/12/20 kHz retain approximately 999 peak counts.
  A learned 12 kHz interference tone falls from 500 to 62.9 peak counts while a
  separate desired 20 kHz tone retains 998.9 of 1000 counts. Broadband stationary
  noise falls about 12.4 dB in the separate synthetic test. These are digital
  tests, not room/SPL or microphone acoustic-response measurements.
- `build-tests/fullband/final-pattern.wav`: 60 seconds with full-band NR,
  broadside, DOA and UAC active. After 528 startup zeros, 2879472 frames are
  consecutive; zero sequence/channel errors, device drops/faults, or USB
  underruns/overruns/gaps/send/pool errors. Two transient active-busy returns
  were added during this run, with queued samples preserved.
- Actual processed and raw five-second captures have no Windows flags/gaps;
  processed slots are identical and raw channels distinct (`audio-check.json`).
- NR averaged about 562 us/block without UAC at initial measurement and
  680 us/block cumulatively after the endurance test; these include preemption
  and broadside work. DOA intervals were approximately 330 ms, with a maximum
  of 339 ms observed in this run.
- Flash 157280 bytes; RAM 447040/532480 bytes (83.95%), leaving 85440 bytes,
  about 83 KiB. Firmware SHA256:
  `8058FE1B81EE477A462D20582A1B3B294FFD6CC7CEA2EA9394827E7FE0F3F35C`.
  Fallback before this change: `build-tests/pre-fullband-nr.uf2`.

The board is left with full-band suppression, broadside and DOA on, an ambient
noise profile learned, diagnostic pattern off, and COM14 free. Historical
listening feedback and measurements below concern the older speech branch.


## Historical speech-band branch, 2026-10-15

Noise suppression is available again. Raw mode preserves four independent
48 kHz channels. `nr config filter` runs the 80 Hz high-pass and optional
50/60 Hz notch directly at 48 kHz. `nr config suppress` uses a selected
microphone (or the broadside beamformer), anti-alias filtering, decimation to
16 kHz, the existing learned stationary-noise suppressor, and interpolation
back to 48 kHz. All four USB slots carry the same processed mono signal.
This is a speech-band output, **not full-band 48 kHz noise suppression**.
The 63-tap Blackman FIR has a 6.72 kHz cutoff; the transition approaches
8 kHz. Batching, STFT, and resampling add approximately 17 ms of delay,
in addition to capture and USB buffering. Modes and noise profiles reset
on reboot; raw mode remains the boot default.

Close recording applications, then use COM14:

```text
bf config broadside
nr config suppress 50 1
nr learn
nr stats
```

Keep desired speech absent for the two-second learning period, then open
Audacity or another recorder at 48 kHz. The learned ambient profile used in
development is not a calibration for your next recording: relearn it in the
actual noise environment. To restore four independent channels, close the
recorder and run `nr config off` and `bf config off`.

Validation on the attached Pico 2:

- 60 seconds of UAC2 with broadside, 50 Hz notch, and learned suppression:
  2,879,520 consecutive pattern frames after 480 startup zeros; zero sequence
  or channel errors, device drops, capture faults, USB underruns or overruns.
  Artifacts: `build-tests/nr48-ram-pattern.*`, `nr48-ram-after.txt`.
- Portable signal tests verify overlap reconstruction, notch attenuation,
  stationary-noise attenuation, tone retention, speech-branch channel
  selection, out-of-band rejection, and exactly two seconds of learning.
- SRAM hot paths include the FFT, resampler, beamformer, processing wrapper,
  and UAC producer/SOF callbacks. Build RAM is 328,720 bytes, leaving
  203,760 bytes (about 199 KiB). Shell responses can be delayed during capture.
- A first native 48 kHz FFT version, and intermediate slower speech builds,
  failed device continuity tests. Windows WAV completion alone is insufficient:
  the diagnostic pattern and device counters must also pass.

These are functional and synthetic signal checks. Speech quality and acoustic
noise reduction in the room still require a controlled listening/recording
comparison. Historical 16 kHz results below do not validate current acoustic
performance.

## Historical 16 kHz implementation


The Pico 2 can now select raw four-channel UAC capture or a processed mono
signal carried identically in all four existing UAC slots. This keeps the USB
identity and tested Windows format unchanged; it is not a separate one-channel
USB endpoint. Mono uses a selected microphone, not beamforming or an average
of microphones. CDC always exports original four-channel PCM for comparison
and array analysis. Direction finding must use raw mode or CDC, not duplicated
mono channels. The default after boot is raw; settings/noise profile are not
persisted to flash.

With `bf config broadside`, the four-channel broadside average replaces the
selected single microphone before this processing stage. See
[beamforming](beamforming.md). Beamforming and suppression can be enabled
independently; changing beam mode clears the learned noise profile.

## Commands

Close UAC audio applications before changing mode. In the COM14 shell:

```text
nr config filter 0 1
nr config suppress 0 1
nr learn
nr stats
nr config off
```

Choose one config command at a time. Arguments are mode (`off`, `filter`,
`suppress`), optional hum frequency (`0`, `50`, `60`), optional microphone
channel (`1..4`). Filter mode needs no learning. Suppress mode initially
passes the filtered signal until explicitly taught a noise profile with
`nr learn`. Keep desired speech absent during the two-second learning period;
check `learn_left=0 learned=1`. Relearn when the noise environment changes.
Noise learning can run with the UAC endpoint closed because processing runs
continuously once enabled. Opening the host recorder afterward uses that profile.

Use standard UAC capture after configuration:

```powershell
python tools/uac_wasapi.py build-tests/processed.wav --seconds 60
python tools/stream.py COM14 build-tests/raw-comparison.wav --seconds 10
```

The processed UAC channels are identical, so ordinary stereo/mixed consumers
can use the mono content, although host processing/settings still matter.
For raw array measurements, use WASAPI exclusive four-channel PCM16/16 kHz.

## Processing

- 80 Hz one-pole high-pass for DC/rumble reduction.
- Optional 50 or 60 Hz biquad notch, Q=20. Enable only if useful for the noise.
- Spectral suppression: 256-point FFT, 128-sample hop at 16 kHz, matched
  square-root Hann analysis/synthesis windows with overlap-add.
- A two-second stationary-noise power estimate is frozen after learning.
  A conservative spectral gain uses an 18 dB attenuation floor and temporal
  smoothing. No automatic gain boost or automatic speech/noise classifier.
- STFT adds 128 samples (8 ms) of algorithmic delay. Gain/phase artifacts and
  speech degradation remain possible, especially if speech is included in
  learning. This is a classical stationary-noise prototype, not neural noise
  cancellation or competing-talker separation. No speech-quality claim is made.

The portable DSP state uses 6216 bytes. Hardware floating point and FPU context
sharing are enabled. DSP executes in the conversion worker, outside the USB
ring spinlock. `nr stats` reports mean/max inclusive processing wall time, mode,
selected channel and training state; `mic stats` adds PDM_PROFILE mean time.
Neither statistic is a whole-system CPU-idle measurement.

## RAM reclaimed without changing frameworks

DMA previously allocated 16 KiB per buffer to keep metadata outside each 8 KiB
hardware ring. Data now resides in eight separately aligned 8 KiB arrays; small
slab descriptors carry pointers and sequence numbers outside DMA memory. This
preserves wrap containment and buffer ownership while removing 64 KiB of
padding (minus descriptor bytes).

The one-second recording workspace and raw snapshot workspace now share storage.
`mic raw` refuses an active RAM recording and clears a completed RAM recording;
raw diagnostics and recording cannot overwrite one another concurrently. The
speaker sample buffer remains independent because existing speaker tests record
microphones while playing audio. CDC streaming remains independent.

RAM fell from 488872 to 401984 bytes: 86888 bytes (84.85 KiB) reclaimed net,
including new DSP/FPU costs. Current linker utilization is 75.49% of 520 KiB,
leaving about 127.4 KiB for future allocations/static buffers. Flash is 146756
bytes. Diagnostic and speaker buffers still dominate RAM; a future production
profile can omit them without changing the audio algorithm or USB framework.

## Validation

Native C tests passed: raw-channel selection, zero-error untrained STFT
reconstruction against the delayed high-pass reference, mains-notch behavior,
noise learning, stationary-noise attenuation and tone retention. Deterministic
synthetic stationary noise fell 11.466 dB; a 1000-peak 1 kHz tone mixed with
noise retained 979.541 PCM peak. These are synthetic algorithm tests, not
acoustic SNR measurements or proof of intelligibility with real speech.

After the DMA-layout change, a hardware raw PDM/PCM snapshot still matched the
independent host decoder exactly after settling (maximum error zero). Shared
PDM clock remained approximately 1.536 MHz, with no raw drops or faults.

A 60-second UAC pattern capture while suppression was computing passed: 112
startup silent frames followed by 959888 consecutive diagnostic frames, correct
four-channel order, zero sequence/channel errors and zero Windows packet/position
gaps. Firmware reported zero underruns, overruns, source gaps, send/allocation
errors and active busy retries. Diagnostic pattern replaces UAC data only;
therefore this test verifies load/transport continuity, not the sound of DSP.

A separate ten-second real processed-microphone UAC recording verified exact
mono duplication across all four slots, with no Windows gaps. After restoring
the final firmware from the SDK experiment, a further three-second processed
capture passed and immediate `config suppress` followed by `learn` completed.
All 42 Python tests and the new native C DSP test pass. Board is left with
`nr config off`, diagnostic pattern off, CDC COM14 available and raw UAC selected.

Artifacts include nr-pattern-60s.wav/.pattern.json, nr-processed-mono.wav,
nr-final-processed.wav, nr-dma-layout-check diagnostics, nr-final-health.txt,
zephyr-nr-idle-profile.txt, and nr-sdk-comparison.json under build-tests.
See [Pico SDK comparison](sdk-comparison.md) for the measured framework check.
