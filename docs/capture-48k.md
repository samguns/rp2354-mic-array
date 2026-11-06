> Update: 48 kHz capture now also supports a 16 kHz speech-suppression branch,
> resampled to 48 kHz USB. See [noise-reduction.md](noise-reduction.md).
> The implementation and measurements below describe the earlier raw milestone.

# Four-channel 48 kHz acquisition

Current target: IM72D128V01 x4, nominal 3.072 MHz PDM, total decimation 64,
48 kHz synchronized signed PCM16, four-channel raw UAC2 plus CDC shell.
This milestone does not add steered beamforming or onboard GCC-PHAT.

## Acquisition and conversion

GP6 drives the common clock and GP7..GP10 are sampled together. The 150 MHz
system clock, fractional PIO divider 1250/256 and ten-cycle PDM loop give
nominal 3.072 MHz, with five cycles per half-period. Sampling is late in the
low half, before the microphones release the bus. Physical accuracy still
depends on the oscillator. The independent phase probe samples approximately
100 ns after detected edges. GPIO synchronizers remain enabled.

Two hardware-chained DMA channels and 24 independently owned, aligned
4 KiB destination buffers preserve the existing fault/ownership checks.
Each block contains 1024 packed PDM words and produces 128 PCM frames,
or 2.667 ms at 48 kHz. The core 0 acquisition thread runs above the shell
at priority 2. The raw
pool covers 64 ms at 48 kHz; the former eight-buffer pool covered too little
time after the sample-rate increase. Any continuity loss stops acquisition
and increments faults instead of repeatedly concealing gaps with warmup
silence. The stopped worker leaves the shell responsive for BOOTSEL recovery.

The filter stages are:

1. Four-integrator/four-comb CIC, decimation 16: 3.072 MHz -> 192 kHz.
2. 31-tap halfband FIR, decimation 2: 192 kHz -> 96 kHz.
3. 63-tap halfband FIR, decimation 2: 96 kHz -> 48 kHz.
4. Three-tap CIC compensation FIR at 48 kHz.

The CIC is evaluated as its exact 61-tap finite convolution using eight
byte-position lookup tables, rather than integrating every PDM sample. Missing
startup history contributes zero, preserving the bitwise CIC model exactly.
Lookup tables are initialized once, not regenerated on every stream reset.

Halfband filters use separate even/odd phase histories, omit zero taps, and
perform paired signed 16-bit DSP multiply-accumulates. Q14 coefficients sum to
unity and each stage rounds and saturates to PCM16. The coefficient generator
checks the absolute coefficient sum against the 32-bit accumulator limit.
Large near-full-scale waveforms can clip at an intermediate FIR stage because
of overshoot. The nominal common linear filter group delay is approximately
0.432 ms; no per-channel delay correction is applied. Hot conversion code and
coefficient copies are placed in RAM.

Core 1 runs the CIC stage for the next block while core 0 finishes the FIR
stages for the preceding block. Core 1 is an isolated bare worker launched
with the RP2350 ROM FIFO handshake; it does not run Zephyr, service interrupts,
allocate memory or access DMA/USB. Core 0 owns scheduling, USB, DMA interrupts,
block ownership and final output. Release/acquire publication protects the job
pointer and intermediate output. The completed CIC buffer is copied before
publishing the next job. The previous raw DMA block remains owned through FIR
completion and diagnostic copying; the incoming raw block stays owned while
core 1 reads it. A core fault or bounded timeout stops acquisition without
recycling a possibly in-use input buffer. This is application-level dual-core
work, not Zephyr SMP support. The pipeline adds one block (2.667 ms) of latency.

The FIR stage processes four input samples per output, with compact polyphase
histories and explicitly unrolled paired DSP MACs. Integer rounding remains
bit-exact against direct convolution. Native tests compare monolithic,
channel-partitioned and independently separated CIC/FIR processing, including
resets. These transformations were needed to sustain USB load at 150 MHz.

`tools/design_pdm48.py` regenerates checked-in coefficients using Python's
standard library. `tools/pdm48_reference.py` independently integrates each
PDM bit and uses direct integer FIR convolution for raw-snapshot comparisons.

The quantized linear filter model has approximately 0 to +0.126 dB passband
gain on a 100 Hz grid from DC through 20 kHz. Its transition is 20..28 kHz;
24 kHz is approximately -6.24 dB. A 1 kHz grid of input image frequencies folding
into 0..20 kHz found worst attenuation approximately 64.7 dB. This transition
choice protects the requested 20 kHz band while reducing processing cost.
These are numerical filter checks, not calibrated acoustic bandwidth, SNR or
THD measurements. The microphone and PCB ports also shape acoustic response.

## USB and commands

The UAC2 descriptor advertises only 48 kHz, four channels, 16-bit samples.
Full-speed payload is nominally 384 bytes/ms, with 47/48/49 complete-frame
packets and a maximum software packet allocation of 392 bytes. The existing
2048-frame reservoir primes at 768 frames; no channel is independently resampled.
PID remains 0005 and the product name remains Pico 2 Mic Array UAC2.

```powershell
python tools/uac_wasapi.py build-tests/raw-48k.wav --seconds 60
python tools/record.py COM14 build-tests/raw-cdc-snapshot.wav --ms 250
python tools/raw_pdm.py COM14 --output build-tests/raw-48k-check
```

The WASAPI recorder now defaults to 48 kHz. Use `--rate 16000` only with older
firmware. Windows shared mode may downmix the four inputs; use the provided
exclusive recorder for raw array validation. COM14 is the last assigned shell
port, not a guarantee against Windows reassignment.

On boot both beamforming and NR are off, so UAC carries the four separate raw
microphone channels. `bf config broadside` still averages the microphones and
duplicates the combined mono in four slots. Close UAC capture before changing
mode. The existing 16 kHz NR filter and suppress modes return ENOTSUP at 48 kHz
rather than silently using incorrect filter frequencies, learning duration
or FFT settings. `nr config off` remains valid. Retuning NR is a later milestone.
CDC diagnostic snapshots remain raw regardless of beam mode. Continuous
48 kHz CDC bulk recording is explicitly unavailable: its queue overflowed
under hardware testing while acquisition remained healthy. Use UAC2 for
continuous recording; `mic stream` returns ENOTSUP with a clear redirect.
The old 16 kHz CDC streaming implementation and host parser remain available
for the historical profile.

The fixed-size RAM diagnostic recording is now limited to 333 ms; longer
recordings should use USB streaming. `mic raw` exports 512 frames (10.67 ms)
and discards the first 128 frames for independent-model settling. The deferred onboard speaker diagnostic is compiled out with
`AUDIO_SPEAKER_DIAGNOSTICS=0`, freeing its 128000-byte stimulus buffer. Set the
flag only when deliberately rebuilding that diagnostic profile; do not assume
its old 16 kHz tests apply to the new microphone pipeline. Historical host delay and
direction tools require 16 kHz recordings and must be adapted before 48 kHz use.

## Validation

All 49 Python tests pass. Native C tests compare the optimized filter against
independent bitwise CIC integration and direct integer FIR convolution exactly
through repeated wraparound, plus DC gain, isolation and block continuity.
Dithered synthetic PDM tones at 1/10/20 kHz retain approximately 9828/9939/9832
PCM peak for a nominal 9830-peak input. Selected 28/44/76/172 kHz input tones
produce output alias components of approximately 6.07/3.70/2.48/5.50 PCM peak.
Synthetic modulation includes its own quantization noise; these are algorithm
checks, not microphone distortion measurements.

The single-core attempts exceeded their deadline. A temporary invalid priority
also prevented startup and required BOOTSEL recovery. Channel partitioning
worked at idle but failed under USB load. The final design pipelines the CIC
and FIR stages, restores temporal buffering and reduces FIR control/MAC cost.
Failed development captures remain under build-tests and must not be treated
as passing validation.

Final image: 147108 bytes flash, 322328 bytes RAM (60.53%), approximately
205 KiB remaining. Savings include excluding deferred speaker diagnostics
and the unsupported high-rate CDC queue; this is not an OS-only RAM comparison.
The previous working 16 kHz image is `build-tests/pre-48k.uf2`.

A 60-second diagnostic recording of the final conversion/USB pipeline passed
2879424 consecutive frames after 576 startup silent frames, with no sequence
or channel errors. Windows reported no packet/position gaps; firmware reported
zero capture drops/faults and zero USB underruns, overruns, source gaps,
send errors, allocation errors or active busy retries. The release-image repeat also passed 2879424 frames after 576 startup silent
frames, with the same zero-error counters. Startup endpoint busy retries are counted
separately and did not lose tested audio.

A hardware raw snapshot independently reconstructed all four PCM streams
exactly after settling (maximum error zero). PDM streams were distinct, with
approximately 50% one bits and 71% transitions. Internal clock tracing estimated
3.072 MHz; this references the same board clock, not a calibrated external
frequency standard. GPIO low/high probe comparisons were consistent. Controlled
acoustic channel-phase/direction validation remains outstanding.

Release-image recordings verify four distinct raw UAC channels, exact mono
replication in broadside mode and a valid 250 ms four-channel CDC snapshot,
all at 48 kHz. Capture drops/faults remained zero. Idle core 0 FIR processing
was approximately 1.1 ms per 2.667 ms block and core 1 CIC approximately 1.77 ms.
Timers include scheduling and memory-contention effects; these are not
whole-system CPU-idle measurements or a guarantee of spare cycles for NR.

Artifacts: `48k-release-pattern-60s.wav/.json/.pattern.json`,
`48k-release-raw-check/analysis.json`, `48k-release-raw.wav`,
`48k-release-broadside.wav`, `48k-release-cdc-snapshot.wav`,
`48k-release-audio-check.json`, and health logs under build-tests.
The board is left on COM14 with raw UAC selected, BF/NR/pattern off.
Release UF2 SHA256: `C280550CF9609BE6225AEEAB9C96303EB396902B32318C4EA2EC97DC388D35D4`.

Microphone clock/operating modes: [Infineon IM72D128V01 datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-im72d128-datasheet-en.pdf).
