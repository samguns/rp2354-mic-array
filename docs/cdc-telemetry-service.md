# Dedicated CDC telemetry service

The USB device now exposes three independent functions:

| Function | USB interface | Current Windows endpoint |
|---|---|---|
| Interactive diagnostics shell | CDC communication 0 / data 1 | COM20 |
| MIC/2 telemetry and controls | CDC communication 2 / data 3 | COM18 |
| Four-channel 48 kHz audio | UAC2 | Pico 2 Mic Array UAC2 |

VID:PID is now `2fe3:0006`, with the existing unique device serial number. The
PID change avoids reusing Windows' cached descriptors for the old interface
layout. COM numbers can differ on another PC. mic_app uses VID/PID/interface
metadata to prefer the telemetry port and confirms the service with a handshake.
The app does not open the shell, poll direction or alter terminal echo.

## Wire protocol v2

Commands are bounded ASCII lines terminated by LF (optional CR is ignored):

```text
MIC/2 <session> <request-id> SUBSCRIBE
MIC/2 <session> <request-id> PING
MIC/2 <session> <request-id> DOA 0
MIC/2 <session> <request-id> DOA 1
MIC/2 <session> <request-id> UNSUBSCRIBE
```

Session is a nonzero uint32 selected by the host. Request ID is uint32. No
negative/overflowing values, extra arguments or arbitrary shell commands are
accepted. Lines exceeding 79 bytes are discarded through the next LF; malformed
commands and full command/reply queues increment `rx_errors`. Invalid commands
receive no acknowledgement. Commands for an obsolete session are ignored.

Replies are one JSON object per LF, with `v:2`, `type`, `session` and a correlated
`id`. SUBSCRIBE produces `hello` with `service:"mic_array"`, `rate:48000`,
`channels:4`; PING produces `pong`; DOA and UNSUBSCRIBE produce `ack`.
The DOA acknowledgement includes the applied `enabled` value (0/1). Other
control replies use enabled=-1; they do not describe estimator state.

After subscribing, the board immediately sends a status snapshot and then pushes
a `type:"telemetry"` packet for each completed direction estimate. It also sends
a status heartbeat every second when no result arrives, including while DOA is
off. Every packet has the session identifier. A valid session command renews a
3.5-second lease; expiry or DTR drop disables the subscription. Reconnection
uses a fresh session so old bytes cannot become current data. The host starts
with an LF to resynchronize any incomplete line from an interrupted connection.

Telemetry fields retain the previous measurement meanings: epoch, result `seq`,
device `uptime_ms`, enabled/current flags, result/sample-end age, update interval,
publication delay, azimuth in millidegrees (-1 unusable), rejection mask, four
RMS levels, six pair delays/coherences, fit residual and capture/analysis counters.
Pairs are 12,13,14,23,24,34. Values named `*_milli` divide by 1000.
`tx_skipped` counts replaced pending packets or formatting failures; `rx_errors`
counts input/control-queue errors. Counters are cumulative since boot.

## Scheduling and buffering

The estimator publishes a semaphore notification after writing its existing
result structure. The service thread copies a coherent snapshot and formats it;
no PCM is copied or queued for transport. Its priority is 1, with a 2560-byte
stack, and it waits while idle. USB work/DMA outrank it. UART callbacks perform
only bounded RX parsing and FIFO/queue operations, never formatting or DSP.

The command queue holds four 80-byte entries; the reply queue holds four bounded
192-byte replies. TX has one immutable active packet and one replaceable pending
telemetry packet (1152 bytes each). Replies are selected before pending telemetry.
The driver has a 1024-byte TX FIFO and a 256-byte RX FIFO. Slow hosts cannot
create an unbounded application queue; superseded pending telemetry is counted.
Already queued UART/USB bytes can still arrive after unsubscribe, so the host
also filters session identity. This is a bounded best-effort service, not a hard
latency guarantee.

mic_app validates the handshake, correlates control acknowledgements, sends PING
every second, and times out missing controls after two seconds or a silent link
after four seconds. Closing/cancelling a connection joins its worker. An aborted
handshake cannot leave a hidden serial session when switching to Simulation.
Saved-session measurements remain v1 for compatibility, with `protocol_v:2`
recording the wire version. Old JSON sessions still replay.

## Validation, 2026-10-16

- 62 Python/native firmware tests, two Rust framing/schema tests, five TypeScript
  tests and three browser UI tests pass.
- Final production Rust worker: 442 packets / 440 new results in 75 seconds,
  about 5.86 Hz, no link errors and one acknowledged control. Median current
  snapshot interval 171 ms. Median onboard result age when formatted is 0 ms;
  median sample-end-to-publication delay 45.681 ms, sampled maximum 53.308 ms.
  Serial transit and Windows scheduling are not calibrated by these measurements.
- This subscription spans the complete final 60-second UAC2 test with broadside
  and learned full-band suppression. Pattern: 1344 startup zeros, then 2,878,656
  consecutive frames; zero sequence/channel errors, Windows flags or position
  gaps. Acquisition drops/faults, analysis overwrites, telemetry skips and
  protocol input errors remain zero. USB underruns/overruns/gaps/send/pool errors
  remain zero. Transient active-busy returns preserve queued frames.
- The hardware protocol test checks acknowledged off/on, off-state heartbeats,
  lease expiry with DTR high, fresh-session recovery, stale-session isolation and
  unsubscribe. Its final 30-second audio test also passes: 1296 startup zeros,
  1,438,704 consecutive frames, no sample errors or underruns.
- The delivered Tauri executable selects COM18 automatically and passes native
  connection, controls, trial and disconnect checks. Cancelling a wrong-port
  handshake by switching to Simulation also passes without leaving a connection.

An initial control-transition test exposed repeatable audio underruns with the
previous 16-ms producer reserve. Opening the CDC port without commands passed;
connection/control traffic could postpone capture delivery beyond that reserve.
The service now coalesces status bursts with a 20-ms minimum formatting interval;
steady 171-ms estimator updates remain immediate. UAC uses a 1536-frame (32-ms)
prime reserve in its existing 2048-frame ring, with drift thresholds 1280/1792.
This adds approximately 16 ms of USB audio buffering, without increasing audio
ring RAM or direction-estimation latency. The final transition and steady tests
above pass. Earlier failed captures/reports are clearly labeled as intermediate.

Diagnostic packet fields `service_max_us`, `format_max_us`, `irq_max_us` and
`config_max_us` expose boot-cumulative timing maxima, including preemption. In
the final run the reported service maximum was 7242 us, formatting 1082 us and
UART callback 186 us. These are measured maxima, not hard execution budgets.

Final evidence: `build-tests/cdc-service/release-transport.json`,
`release-measurements.json`, `release-pattern.*`, `release-health.txt`,
`protocol-reserve.json`, `reserve-control.*`, `reserve-after.txt`,
`native-smoke.json`, `native-device.png` and `release-manifest.json`.
The folder also retains compact failed/intermediate reports; their recordings
are pruned after the passing release evidence is saved.

Flash 164948 bytes; RAM 489936/532480 bytes (92.01%), leaving 42544 bytes
(41.5 KiB). Recovery image: `build-tests/pre-dedicated-cdc.uf2` retains the older
UAC/shell firmware; the current app requires MIC/2 firmware. Direction estimates
remain provisional; this migration does not establish acoustic angular accuracy
or enable automatic beam steering.

```powershell
python tools/flash.py COM20
python tools/validate_telemetry_service.py --port COM18
python tools/doa_check.py --port COM20 --output build-tests/cdc-service/frame.wav
```

Board left with broadside, learned full-band suppression and continuous DOA on,
pattern off, shell echo unchanged/on, both COM ports free. Reboot defaults remain
raw audio with DOA off. The application explicitly enables estimation when asked.

Release hashes (SHA256):

- Firmware: `C577D27D944764149B3E9920BF1AE474D156E9927D84AE76A43D4D02525A1893`
- Application: `06A632AD1B5B4EB1622E0483AB35E3B1634006C46A9DE227D1B95304FCEB9108`

Manual steering adds BEAM and NR_LEARN controls and output-state telemetry. See [manual beamforming](manual-beamforming.md) for the commands, processing pipeline and reserved estimation cadence. Earlier performance figures above describe the pre-steering milestone.

NR_CONFIG and SETTINGS_SAVE provide app control and board-side persistence. See [persistent audio settings](persistent-audio-settings.md).
