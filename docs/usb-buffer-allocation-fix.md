# RP2350 USB endpoint buffer allocation fix

Current shutdown timing correction and validation: [USB shutdown fix](usb-shutdown-fix.md).
The investigation notes below retain the earlier failures and recovery results.

## Cause

The Zephyr revision pinned by `setup.sh`
(`27150c9dd348690a6b41d3992dd5d3d1458fdc66`) calls
`sys_mem_blocks_alloc(pool, blocks, &ep_data->buf)` in the RP2350 UDC driver.
This API writes one pointer per allocated block, but `ep_data->buf` is a
single pointer. A multi-block allocation therefore overwrites adjacent
endpoint state, including the data PID. The matching free call also reads
past that pointer. In addition, the hardware uses one buffer base address,
so the allocation must be contiguous.

This application's 196-byte playback and 392-byte capture packets require
four and seven 64-byte blocks respectively, exposing the error.

## Change

Extend `patches/zephyr-udc-rpi-pico.patch` to use
`sys_mem_blocks_alloc_contiguous(pool, blocks, &ep_data->buf)` and
`sys_mem_blocks_free_contiguous(pool, ep_data->buf, blocks)`.
The existing SOF acknowledgement and logging changes are retained.
Apply the updated patch to a clean checkout of the pinned Zephyr revision
and rebuild using the repository's normal setup/build process.

## Validation

- The patch applies cleanly to the pinned Zephyr source.
- A host model of the endpoint functions with a mock allocator reproduces
  adjacent-state corruption and a free error with the original calls.
  The corrected calls pass 200 duplex enable/disable cycles and a
  fragmented-pool failure check without leaking blocks. This is a model,
  not a hardware test of the allocator.
- Firmware builds for `rpi_pico2/rp2350a/m33`: 147,148 bytes flash and
  513,760 bytes RAM. The UF2 does not overlap the final 8 KiB settings area.
- On Arduino VENTUNO Q, Ubuntu 24.04 / Linux 6.8, the original firmware
  stalled audio `SET_INTERFACE` requests and playback counters stayed zero.
  After backing up the device and flashing the corrected firmware, the
  same USB audio device played a 48 kHz stereo PCM16 WAV through PipeWire.
  Device receive/play counters increased, PCM peak reached 15,487, and
  underrun, overrun and fault counters remained zero. Audible playback
  was confirmed by the user.

Windows and phone playback had worked before the fix. Their USB request
sequences were not captured, so the exact reason those hosts did not
trigger the same failure is not established. No Linux permission or
host-driver changes were needed for the successful post-fix playback.

## Windows hardware verification, 2026-11-18

The merged allocator correction was built and flashed on board
`E66580A7C72D6127`. The compiled driver used the contiguous allocation/free
APIs. All four microphones captured the speaker's 1 kHz tone, and diagnostic
patterns verified channel order and sample sequence.

The allocator correction alone did not resolve repeated Windows duplex
shutdown: capture could stall when playback stopped, while microphone
acquisition continued. The combined patch now also corrects USB DPRAM handoff
timing for the board's 150 MHz system clock. The uninstrumented firmware has
passed 40 separate duplex cycles, 30 playback stops during continuous raw
capture, and 50 playback stops during continuous capture with full DSP load.
All had zero audio errors, host discontinuities, or USB recoveries.

See [USB shutdown timing fix](usb-shutdown-fix.md) for the hardware timing
requirement, complete verification results, reproducible host test, BOOTSEL
correction, and the exceptional USB recovery counter.
