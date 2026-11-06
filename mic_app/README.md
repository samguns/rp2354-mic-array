# mic_app

Windows desktop direction viewer and interactive validation workspace for the
Pico 2 four-microphone array. Built with Tauri 2, Vue 3, TypeScript and a Rust
serial backend. No Python runtime is required by the desktop app.

## Run

Open `release/mic_app.exe` after a release build. The app selects **Mic Array Telemetry** (currently COM18). Choose **Connect board**, then **Enable estimation** if needed.
Keep other apps off the telemetry port. The diagnostics shell is a separate CDC
port (currently COM20); Audacity/UAC audio operates independently.
**Simulation** exercises the complete interface without hardware and is clearly
labeled; it does not connect to the board. **Auto reconnect** retries a lost
connection every two seconds. It does not silently enable estimation after reboot.

The attached board needs firmware with the dedicated **MIC/2** service
(USB VID:PID `2fe3:0006`). Older shell-only telemetry is not used by this app.
The device handshake confirms protocol, service, sample rate and channel count.
Choosing the shell port produces a clear connection error rather than a session.
No flashing or BOOTSEL action is performed by the desktop app.

## Interpret the display

The compass shows the exposed sound-port face, USB-C upper right. Azimuth is
0 right, 90 top, 180 left and 270 bottom. A mint arrow appears only for a current,
accepted estimate; rejected, disabled, stale and disconnected states have no
usable arrow. Stale means more than 700 ms since result publication, including
time since the last host response. The gold line is the expected source azimuth.

Update rate is the board's completed-estimate rate, not the UI poll rate.
Newest-sample age includes the board's DMA-to-publication delay, result age and
time since receipt; serial transit time is not independently calibrated.
The UAC producer reserve is now 32 ms to absorb connection/control bursts
(approximately 16 ms more buffering than the previous firmware). This does not
change onboard direction-estimation latency.

Levels are raw-window RMS in digital dBFS, not calibrated SPL. Fit residual and
pair coherence are diagnostic metrics, not confidence probabilities.

The board currently reports azimuth only. Front/back is ambiguous; it does not
report elevation, range or multiple speakers. Sources near array normal may be
rejected because their azimuth is poorly defined. No angular accuracy is claimed.

## Validate and replay

1. Use a fixed broadband source at a measured position. Avoid a pure 1 kHz tone
   for GCC-PHAT direction validation. Keep the sound holes clear and board fixed.
2. Enter expected azimuth, distance, placement/elevation and notes. Place sources
   away from directly normal to the face when testing azimuth.
3. Start a trial, hold the setup steady, then finish. One record is retained per
   unique epoch/result sequence, including rejected results. Trials stop at 10,000
   samples. Connection loss or mode changes also finish the active trial.
4. Inspect acceptance, circular mean direction, signed circular bias, mean
   absolute angular error, 95th-percentile absolute error and circular deviation.
   Rejected, stale, disabled and obsolete-epoch results are excluded from angle
   statistics but included in the acceptance denominator. An indeterminate
   circular mean is displayed as a dash.
5. Export JSON for full telemetry and replay, or CSV for per-result analysis.
   Select a completed trial and choose Replay, or import an exported JSON session.
   Replay has play/pause and a position slider. Simulation/device provenance is
   retained in the trial selector and exports.

Sessions are held in memory: export before closing. Exports include unfinished
trial samples too. Imported sessions are limited to 50 MB / 200,000 samples.
No angle averaging is applied to the live arrow; history exposes the variation.

## Development

Use Node 22.12+ and Rust 1.89+ with the Windows MSVC toolchain and WebView2.
See [Tauri Windows prerequisites](https://v2.tauri.app/start/prerequisites/).
The npm Tauri API stays on 2.11 to match the Rust-1.89-compatible crate; commit
both lockfiles and use locked builds for reproducibility.

```powershell
cd mic_app
npm ci
npm run tauri dev
npm run desktop:build
npm test
npm run test:e2e
cargo test --manifest-path src-tauri/Cargo.toml --locked
```

The executable is built at `src-tauri/target/release/mic_app.exe`. The delivered
copy lives at `release/mic_app.exe`. `npm run tauri build` can also create an NSIS
installer; the standalone executable is sufficient on this PC with WebView2.
`npm run dev` provides browser simulation, import/export and replay; serial
connection requires the Tauri desktop executable. Browser UI tests use installed
Microsoft Edge and save a screenshot/session in `../build-tests/mic-app`.

Hardware integration test (owns the serial port for 60 seconds):

```powershell
$env:MIC_APP_TEST_PORT='COM18'
$env:MIC_APP_TEST_OUTPUT='../build-tests/cdc-service/live-transport.json'
cargo test --manifest-path src-tauri/Cargo.toml live_device -- --ignored --nocapture
```

## Protocol and ownership

The Rust backend owns the dedicated telemetry CDC handle on a worker thread.
It performs a MIC/2 subscription handshake, then receives completed estimates
without polling or shell commands. It sends a heartbeat once per second; the
board expires subscriptions after 3.5 seconds without a valid session command,
or after DTR drops. Estimator controls have correlated acknowledgements and a
two-second deadline. Reconnection uses a fresh session identifier to reject
old buffered data. The worker is stopped and joined on disconnect/exit.

The board uses interrupt-driven CDC FIFO access and bounded queues. One pending
telemetry packet is retained; newer data replaces old pending data. An active
packet remains immutable until queued to the driver. Controls take precedence.
No audio buffer is retained for telemetry. Capture/DSP and UAC2 remain independent.
The interactive diagnostics shell is separate and terminal echo is never changed.

Wire protocol v2 is described in [the service specification](../docs/cdc-telemetry-service.md).
To preserve saved-session compatibility, the backend normalizes measurements to
mic_app's existing v1 session measurement schema, adding `protocol_v=2`, session
identity and transport counters. Previously exported sessions still replay.

Validation: [service migration and measurements](../docs/cdc-telemetry-service.md).

Manual onboard steering is available in mic_app: Off / Broadside / Manual, azimuth and assumed elevation, Apply beam and Relearn noise. See [manual beamforming](../docs/manual-beamforming.md).

Noise suppression can be enabled in mic_app. Save & restart retains audio settings and the learned noise profile on the board. See [persistent audio settings](../docs/persistent-audio-settings.md).
