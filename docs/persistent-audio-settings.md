# Persistent audio settings in mic_app

Connect to the board and use Enable suppression / Disable suppression in the
Manual beamforming panel. Close Audacity or other Pico audio recording first.
Use Relearn noise with desired speech absent for two seconds when learning a
new background profile. Then select Save & restart. The app enables automatic
reconnection and the board restarts. Audio settings saved on board confirms
that the active configuration matches its saved copy.

Saved data includes noise-processing mode, hum filter, input channel, beam mode,
azimuth, assumed elevation and the learned 129-bin noise spectrum. Suppression
and its learned profile are restored before capture starts, without a shell or
a connected desktop application. Direction estimation retains its separate
boot default and can be enabled in the app. Changing a beam invalidates the
noise profile as before; learn and save again when appropriate.

Settings are stored only when Save & restart is selected, avoiding flash wear
from every angle adjustment. The final 8 KiB of the RP2354A board's 2 MiB flash (settings at 0x1FE000-0x1FFFFF) is a
reserved device-tree partition. Two 4 KiB sectors alternate versioned records
with generation numbers, bounds checks and CRC32. A new record is programmed
and read back before restarting; the previous sector remains intact. A failed
or interrupted write is rejected on boot. Normal UF2 firmware updates do not
include the reserved partition; a full flash erase removes saved settings.

Saving requires inactive UAC, completed steering transitions and completed noise
learning. A 500 ms delay allows the correlated MIC/2 acknowledgement to drain.
Capture is stopped and core 1 parks in SRAM before flash programming disables
XIP access. The board then restarts, clearing intentional capture-stop counters.
If audio starts before the delayed save, the save is canceled and reported.

Protocol additions: NR_CONFIG <mode 0..2> <hum 0|50|60> <channel 1..4>,
SETTINGS_SAVE. Existing MIC/2 session/request IDs and acknowledgements apply.
Telemetry adds nr_hum, nr_channel, settings_saved, settings_saving,
settings_error and uac_active. The app gates these controls for older firmware.
The saved flag is maintained by control changes rather than recomputing a
profile checksum during live telemetry. Learning a new profile or changing audio
settings makes it unsaved until explicitly saved again.

## Verified on the board

Four save/restart cases restored suppression On and Off, a manual 135-degree
azimuth / 20-degree elevation beam, Broadside, and learned noise profiles.
The packaged Tauri app passed Enable/Disable, noise learning, Save & restart,
automatic reconnect and saved-profile display. The final release passed a
60-second four-channel pattern capture with 2,878,656 consecutive valid frames
and zero channel/sequence errors alongside 75 seconds of live direction
telemetry (221 unique updates). Recording correctly rejects mode changes and
flash saves. Capture and USB fault counters remained zero.

The release omits debug-only kernel assertions/spinlock ownership validation to
maintain real-time headroom. Explicit DMA sequence checks, capture faults, USB
health checks, configuration bounds and flash CRC validation remain active.
Flash uses 144,924 bytes; RAM uses 502,472 / 532,480 bytes (30,008 bytes free).
Artifact hashes and reports are in build-tests/persistent-audio.
