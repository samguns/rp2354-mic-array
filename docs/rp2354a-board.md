# RP2354A board hardware and wiring

Source: [EasyEDA Pro project](https://pro.lceda.cn/editor#id=b3406129141d4149bf3fd8f035bb6d27). Pins below are GPIO numbers unless marked as U5 QFN-60 package pins.

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
3.3 V regulator. Microphone SELECT pins are grounded, so capture samples in
the low half of CLOCK. Channel order is DATA1, DATA2, DATA3, DATA4.
USB D−/D+ use package pins 51/52. The crystal is 12 MHz.

src/board_pins.h is the shared pin map for PDM acquisition, raw/clock probes,
and optional I2S output/probe. Compile-time checks enforce contiguous inputs
and consecutive speaker pins required by these PIO programs. Speaker diagnostics
remain deferred by AUDIO_SPEAKER_DIAGNOSTICS=0.

RP2354A has 2 MiB in-package flash. `app.overlay` sets that size and reserves
the final 8 KiB for settings. Firmware occupies `0x000000–0x1FDFFF`. The two
settings sectors occupy `0x1FE000–0x1FFFFF`.

Beamforming and direction estimation use the 25 mm cross in
[array geometry](array-geometry.md). Schematic symbol placement is not that
coordinate frame.

The Zephyr build target is `rpi_pico2/rp2350a/m33`, with this board's 12 MHz
crystal and 150 MHz system clock. Firmware output is `build/zephyr/zephyr.uf2`.

RP2354A flash capacity and pin compatibility: [Raspberry Pi documentation](https://www.raspberrypi.com/documentation/microcontrollers/microcontroller-chips.html).
