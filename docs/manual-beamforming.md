# Manual beamforming

Open mic_app/release/mic_app.exe, connect to the telemetry port, select Manual,
enter azimuth and assumed elevation, then Apply beam. A purple compass marker
shows the applied target separately from the green direction estimate.
Use current estimate copies the accepted azimuth into the form; Apply is still
required. This is fixed steering, with no automatic tracking.

In the exposed-port view (USB-C upper right), azimuth 0 degrees is right,
90 top, 180 left and 270 bottom. Elevation 0 lies in the PCB plane;
90 points normal to its face. Elevation is assumed, not measured. Broadside
selects that normal. A planar array cannot distinguish the mirrored rear source.

Audio processing runs on the RP2354A: four synchronized 48 kHz raw microphones feed
both the independent GCC-PHAT estimator and the delay-and-sum beamformer.
The beam filter runs on core 1, pipelined with CIC conversion and direction
estimation; core 0 runs FIR conversion, noise suppression and USB handling.
Manual steering reserves core 1 time by analyzing alternate complete direction
frames: approximately 2.9 Hz rather than 5.9 Hz in Off/Broadside. Each analyzed
frame still contains contiguous raw samples. Face-normal steering uses the faster
broadside path and retains normal estimation cadence.
A double output buffer adds one 128-sample block (2.667 ms) of audio latency.
Beamformed mono feeds noise suppression and is duplicated into all four UAC
slots. Off disables beamforming; existing noise processing can remain enabled.
Audacity can record the UAC device while mic_app uses its separate CDC interface.

The nominal microphone coordinates in mm are CH1 (0,25), CH2 (-25,0),
CH3 (0,-25), CH4 (25,0). Compensation delay is 14 + dot(position,direction)
* 48000 / 343 samples, with positions in metres. The microphone nearest the
source receives the greatest compensation delay. A 21-tap Hamming-windowed
sinc filter uses Q14 coefficients, normalized for exact DC gain, and 64-bit
accumulation using ARM packed dual multiply-accumulate instructions. Common delay is 14 samples (0.292 ms). Steering changes crossfade
for 20 ms. Changes apply at capture block boundaries and preserve filter history.

A changed beam invalidates the learned stationary noise profile while preserving
STFT overlap state. Click Relearn noise with desired speech absent for two seconds.
Enable suppression in mic_app before learning. Save & restart retains the audio
configuration and learned profile; see [persistent audio settings](persistent-audio-settings.md). Profile status and
applied beam settings are included in exported session telemetry. Simulation
controls do not process audio; replay cannot control the board.

Shell examples: `bf config manual 45 0`, `bf config broadside`, `bf config off`,
`bf stats`. Changes are permitted during UAC recording.

MIC/2 additions: `BEAM <mode> <azimuth_tdeg> <elevation_tdeg>` and `NR_LEARN`.
Modes 0/1/2 mean off/broadside/manual; angles use tenths of a degree.
Beam acknowledgments carry a revision. Telemetry reports bf_mode,
bf_azimuth_tdeg, bf_elevation_tdeg, bf_revision, bf_pending, bf_fade_left,
bf_delays_milli, nr_mode, nr_learned and nr_learn_left. Pending/fade state,
rather than acknowledgment alone, identifies completion. Older firmware remains
viewable with manual controls disabled.

Synthetic production-C tests cover coherent sources at six azimuths, four
elevations and seven frequencies from 100 Hz to 20 kHz, steering sign,
opposite-direction rejection at 4 kHz, exact off passthrough, DC gain and fades.
These verify the algorithm, not physical rejection or calibrated pointing accuracy.
The small array has broad beams at low frequencies; room reflections, microphone
mismatch and geometry errors limit practical rejection. Controlled-source acoustic
validation is still needed before quoting beamwidth or attenuation performance.

## Hardware verification, 2026-10-16

75 seconds of dedicated CDC telemetry delivered 221 new direction results.
A concurrent 60-second UAC diagnostic capture validated 2,878,608 consecutive
four-channel frames after startup silence, with zero channel or sequence errors.
Nine live beam changes and noise relearning passed during a second 15-second
recording (718,656 validated frames). Capture drops/faults and USB underruns,
overruns, gaps and send/pool errors remained zero. Real microphone recording
confirmed four identical slots in Manual and independent channels in Off.
The packaged Tauri application passed steering, learning, estimation toggles,
trial recording, disconnect and canceled wrong-port connection tests.

Flash: 174,516 bytes. RAM: 498,664 / 532,480 bytes (33,816 bytes free).
Reports and artifact hashes are in build-tests/manual-beam/release-manifest.json.
