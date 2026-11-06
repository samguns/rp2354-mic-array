#!/usr/bin/env bash
# Ubuntu counterpart of build.ps1.
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
pristine=auto
if [[ "${1:-}" == "--pristine" ]]; then
    pristine=always
elif [[ $# -ne 0 ]]; then
    echo "Usage: $0 [--pristine]" >&2
    exit 1
fi

workspace="$root/zephyrproject"
sdk="$workspace/zephyr-sdk-1.0.1"
udc="$workspace/zephyr/drivers/usb/udc/udc_rpi_pico.c"
if [[ ! -f "$workspace/.west/config" || ! -d "$sdk" ]]; then
    echo "In-tree workspace is missing. Run ./setup.sh" >&2
    exit 1
fi
if ! grep -q sof_rd "$udc"; then
    echo "Zephyr USB patch is not applied. Run ./setup.sh" >&2
    exit 1
fi

export ZEPHYR_BASE="$workspace/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$sdk"
cd "$workspace"
west build -b rpi_pico2/rp2350a/m33 -d "$root/build" -p "$pristine" "$root" -- "-DUSER_CACHE_DIR=$root/build/.cache"
