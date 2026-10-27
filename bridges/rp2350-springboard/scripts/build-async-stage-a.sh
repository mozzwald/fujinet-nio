#!/usr/bin/env bash
# Build only. Does not inspect, flash, reset or arm a device.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [[ $# != 3 || $1 != /* || $2 != /* || $3 != /* || -e $3 ]]; then
 echo "Usage: $0 absolute-verified-ROM absolute-manifest absolute-NEW-output-directory" >&2;exit 2
fi
cmake -S "$root" -B "$3" -DBRIDGE_MODE=firmware \
 -DPICO_BOARD=waveshare_core2350b -DPICO_PLATFORM=rp2350-arm-s \
 -DCMAKE_BUILD_TYPE=Release -DSPRINGBOARD_USB_DEBUG=ON \
 -DSPRINGBOARD_ASYNC_STAGE_A=ON -DSPRINGBOARD_BOOT_AUTO_ARM=OFF \
 -DSPRINGBOARD_PIO_REARM=OFF -DSPRINGBOARD_PERSISTENT_REPLY=OFF \
 -DSPRINGBOARD_CONTINUOUS=OFF -DSPRINGBOARD_ROM_SMOKE=OFF \
 -DSPRINGBOARD_LATENCY_DIAGNOSTICS=OFF \
 "-DSPRINGBOARD_ROM=$1" "-DSPRINGBOARD_ROM_MANIFEST=$2"
cmake --build "$3" --target springboard_cs1_stage_a --parallel 4
