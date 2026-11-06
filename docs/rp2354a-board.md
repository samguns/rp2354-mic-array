# RP2354A board, 2026-10-26 schematic

Source: SCH_rp2350-mic-array_1-P1_2026-10-26.png. GPIO assignments match the
previous Pico 2 firmware; MCU package pin numbers differ from Pico header
positions. All pins below are GPIO numbers unless explicitly marked package.

| Net | GPIO | U5 QFN-60 package pin | Destination |
|---|---|---|---|
| SCLK | 6 | 9 | U1-U4 CLOCK |
| DATA1 | 7 | 10 | U1 DATA, PCM CH1 |
| DATA2 | 8 | 12 | U2 DATA, PCM CH2 |
| DATA3 | 9 | 13 | U3 DATA, PCM CH3 |
| DATA4 | 10 | 14 | U4 DATA, PCM CH4 |
| DIN | 11 | 15 | U7 pin 3 |
| BCLK | 12 | 16 | U7 pin 4 |
| LRC | 13 | 17 | U7 pin 5 |

U7 pins 1/2 supply VBUS/GND to the external amplifier. U6 is the ME6217C33M5G
3.3 V regulator, not the previous design's speaker module. Microphone SELECT
pins remain grounded, so existing low-clock sampling and channel order apply.
USB_DM/DP use dedicated package pins 51/52. The crystal remains 12 MHz.

src/board_pins.h is the shared pin map for PDM acquisition, raw/clock probes,
and optional I2S output/probe. Compile-time checks enforce contiguous inputs
and consecutive speaker pins required by these PIO programs. Speaker diagnostics
remain deferred by AUDIO_SPEAKER_DIAGNOSTICS=0.

RP2354A has 2 MiB in-package flash. app.overlay overrides the Pico 2 base
board's 4 MiB declaration. Firmware occupies 0x000000-0x1FDFFF and the two
settings sectors occupy 0x1FE000-0x1FFFFF. The flash driver and linker therefore
use the actual capacity. Saved settings from the old prototype's 0x3FE000
location are not migrated; configure and save on the new board.

The schematic does not establish microphone sound-port coordinates or board
view orientation. Existing nominal beamforming/DOA geometry and mic_app's
prototype orientation illustration still need confirmation against the new PCB
layout; they must not be inferred from schematic symbol placement.

Build target remains rpi_pico2/rp2350a/m33 for the compatible RP235x core and
12 MHz / 150 MHz clock setup. This is an application-specific board override,
not a separate upstream Zephyr board definition. Firmware output is
build/zephyr/zephyr.uf2. No automatic flashing is performed for this revision.

RP2354A flash capacity and pin compatibility: [Raspberry Pi documentation](https://www.raspberrypi.com/documentation/microcontrollers/microcontroller-chips.html).
