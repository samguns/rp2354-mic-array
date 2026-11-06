# Speaker retest, 2026-10-14

External reference validation: all four channels captured a dominant 1 kHz tone. Third harmonic was -49.9 to -43.7 dBc. This validates the capture at that frequency, not calibrated microphone THD.

Onboard test: five frequencies, PCM16 peaks 128 and 512, 16 kHz I2S. No firmware changes. Each tone ran for one second. Actual GPIO framing/sample checks passed for all ten tones (maximum error below 3.42 PCM counts). Both final health reports show zero dropped blocks and zero faults.

Third harmonic relative to fundamental across four channels:

| Frequency | Peak 128 | Peak 512 |
|---|---|---|
| 500 Hz | 34.0 to 40.8 dBc | 37.9 to 41.8 dBc |
| 750 Hz | 6.2 to 10.6 dBc | 11.4 to 13.9 dBc |
| 1000 Hz | -5.1 to -4.2 dBc | -6.0 to -0.9 dBc |
| 1500 Hz | -34.1 to -24.3 dBc | -32.5 to -16.7 dBc |
| 2000 Hz | -28.6 to -17.0 dBc | -4.7 to 2.0 dBc |

Strong frequency-dependent harmonic output persists. Fourfold digital amplitude did not yield a corresponding acoustic increase; sequential room recordings do not isolate the cause. GPIO sample checks do not validate absolute clocks, electrical timing at the amplifier, power supply, or analogue output. Speaker quality alone is not established.

Next diagnostic: repeat with a known-good 4-8 ohm passive speaker using the same amplifier and fixed geometry. If the anomaly remains, inspect module power and signal timing and measure the bridged output differentially. Never connect an earth-referenced oscilloscope ground to either speaker terminal.

Raw WAVs, GPIO traces and reports are under build-tests/speaker-retest-low and build-tests/speaker-retest-high.
