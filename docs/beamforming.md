# Broadside beamforming

> Current 48 kHz firmware: see [capture-48k.md](capture-48k.md). The 16 kHz
> noise suppressor is unavailable at 48 kHz; results below are historical.

The first beamformer is a unity-gain average of the four synchronous PCM
channels at 16 kHz. All sound ports are 25 mm from the center in one plane,
so a source on the normal through the array center has equal geometric path
lengths. Broadside therefore needs no relative delays. No calibration gains
or measured delay corrections are applied. This milestone does not implement
steering or automatic direction finding.

Close host microphone capture before configuring the COM14 shell:

```text
nr config off
bf config broadside
bf stats
```

Record from the UAC2 microphone in Audacity. The four USB slots carry the
same beamformed mono signal, so ordinary stereo recording is sufficient.
For comparison with a single microphone, close capture and use:

```text
bf config off
nr config filter 0 1
```

Filter mode selects CH1 and applies the existing high-pass filter; broadside
with NR off has no high-pass. For a comparison with identical filtering,
use `nr config filter 0 1` in both beam modes.

For beamforming followed by noise suppression:

```text
bf config broadside
nr config suppress 0 1
nr learn
```

Keep speech absent for two seconds and check `nr stats` for learned=1.
The channel selector has no effect when beamforming is enabled because NR
receives identical copies of the beam. Changing beam mode resets NR state
and its learned profile. Relearn after changing modes. Defaults on boot are
beamforming off and NR off; settings are not persisted.

CDC streaming and diagnostic microphone recordings remain original four-channel
data. UAC pattern diagnostics override audio output as before.

The arithmetic uses a 32-bit sum, rounded division by four and identical
four-slot output. Coherent input retains its amplitude without clipping.
Uncorrelated equal-power channel noise ideally falls 6 dB; room noise is often
correlated, so this is not a promised acoustic improvement. A planar broadside
beam also accepts sound from the opposite face. Test with speech centered
in front and an interfering source to the side, using fixed source levels,
distance and array orientation. Speech quality and spatial rejection require
that controlled acoustic comparison.

## Validation on 2026-10-15

Native C tests pass for coherent unity gain at both PCM limits, spatial-phase
cancellation, identical output slots, a 6.02 dB orthogonal-noise example, and
compatibility with NR channel selection. Existing NR native tests and all 42
Python tests also pass.

Built and flashed firmware: flash 147500 bytes, RAM 403016 bytes (75.69%).
A five-second real microphone capture contained nonzero broadside audio with
all four USB slots exactly equal. A 30-second diagnostic ramp capture while
beamforming and trained suppression computed passed 479936 consecutive frames
after 64 startup silent frames, with zero channel/sequence errors, Windows
packet flags or device-position gaps. The ramp tests transport under DSP load,
not acoustic suppression quality. Firmware reported no capture drops/faults,
USB underruns/overruns/source gaps or send/pool errors. Four cumulative active
endpoint busy retries occurred across sessions; no lost frames were detected
in the tested ramp recording. Mean inclusive DSP time under USB load was
approximately 1.31 ms per 8 ms block; broadside alone with USB idle was 22 us.
These timings include scheduling effects and are not total CPU utilization.

The active-capture configuration guard was exercised and correctly rejected
changing beam mode. Off/on transitions and suppression learning completed.
The board was left with broadside on, NR off, diagnostic pattern off and COM14
available. Artifacts are `build-tests/beam-broadside.wav`,
`beam-nr-pattern.wav/.json/.pattern.json`, `beam-active-health.txt` and
`beam-final-health.txt`. Controlled acoustic beam-pattern/SNR validation remains.
