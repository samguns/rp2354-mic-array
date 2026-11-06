#!/usr/bin/env bash
# Ubuntu counterpart of setup.ps1. Creates zephyrproject/ inside this repository.
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
workspace="$root/zephyrproject"
zephyr_rev=27150c9dd348690a6b41d3992dd5d3d1458fdc66
patch="$root/patches/zephyr-udc-rpi-pico.patch"
sdk_root="$workspace/zephyr-sdk-1.0.1"
gcc="$sdk_root/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"

case "$(uname -m)" in
    x86_64) sdk_arch=linux-x86_64 ;;
    aarch64 | arm64) sdk_arch=linux-aarch64 ;;
    *)
        echo "Unsupported machine: $(uname -m). This script supports x86_64 and aarch64." >&2
        exit 1
        ;;
esac
sdk_archive="zephyr-sdk-1.0.1_${sdk_arch}_minimal.tar.xz"
sdk_url="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/$sdk_archive"

if ! command -v west >/dev/null 2>&1; then
    echo "west is not on PATH. Install it with: python3 -m pip install --user west" >&2
    echo "Then add ~/.local/bin to PATH." >&2
    exit 1
fi
if ! command -v curl >/dev/null 2>&1 || ! command -v tar >/dev/null 2>&1; then
    echo "curl and tar are required. Install them with apt, including xz-utils." >&2
    exit 1
fi

# west init --mr accepts a branch or tag, not an arbitrary commit.
if [[ ! -f "$workspace/.west/config" ]]; then
    west init -o=--filter=blob:none --mr v4.4.0 "$workspace"
fi

zephyr="$workspace/zephyr"
(
    cd "$zephyr"
    if git apply --check --reverse "$patch" >/dev/null 2>&1; then
        git apply --reverse "$patch"
    fi
    git fetch origin "$zephyr_rev"
    git checkout --detach "$zephyr_rev"
)

(
    cd "$workspace"
    west update
)

(
    cd "$zephyr"
    if git apply --check --reverse "$patch" >/dev/null 2>&1; then
        echo "Zephyr USB patch already applied."
    else
        git apply --check "$patch"
        git apply "$patch"
        echo "Applied Zephyr USB patch."
    fi
)

if [[ ! -x "$gcc" ]]; then
    if [[ ! -f "$sdk_root/setup.sh" ]]; then
        archive="$workspace/$sdk_archive"
        curl -L --fail --retry 3 -o "$archive" "$sdk_url"
        tar -C "$workspace" -xf "$archive"
        rm -f "$archive"
    fi
    (
        cd "$sdk_root"
        ./setup.sh -t arm-zephyr-eabi -h -c
    )
fi
if [[ ! -x "$gcc" ]]; then
    echo "Arm Zephyr toolchain was not installed at $gcc" >&2
    exit 1
fi
echo "Workspace ready: $workspace"
