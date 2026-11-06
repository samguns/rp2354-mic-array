> Current transport: [dedicated CDC service](cdc-telemetry-service.md), telemetry COM18, shell COM20.
> Earlier transport/resource measurements below describe their original checkpoint.

> Latest addition: [mic_app desktop viewer and telemetry](mic-app-2026-10-16.md).
> Current firmware resource/hash details are in that report.

# Current checkpoint: overlapped estimation, 2026-10-16

Implemented, flashed and validated. See [current architecture, measurements,
firmware hash and evidence](doa-overlap-2026-10-16.md).

48 kHz four-channel capture, UAC2, broadside and full-band suppression run while
core 1 incrementally estimates direction from double-buffered raw windows.
Results update about every 171 ms; maximum observed 174 ms. Newest sample to
publication is about 44 ms, maximum observed 47.009 ms. Analysis remains 170.667 ms.

61 tests pass. Final 60-second diagnostic streaming has zero sample/channel
errors, acquisition drops/faults or analysis overwrites/resets. Actual processed
and raw audio checks and same-frame host comparison pass. Evidence:
`build-tests/overlap`. RAM 479968/532480 bytes; 51.3 KiB free.
Recovery: `build-tests/pre-doa-overlap.uf2`.

Board left with DOA on, broadside on, learned full-band suppression on,
pattern off, COM14 free. Reboot defaults to raw audio with DOA off.

Known-position acoustic validation and automatic steering remain pending.
Noise suppression adds 128 samples (2.667 ms). Passive speaker work is deferred.
Host tools support 48 kHz only. Commands: `doa config on|off`, `doa stats`,
`doa capture`, `doa dump`; close audio applications before one-shot export.
