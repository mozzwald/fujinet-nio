#!/usr/bin/env bash
# Build only. No hardware access, filesystem mounting, flash or reset.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [[ $# != 3 || $1 != /* || $2 != /* || $3 != /* || -e $3 ]]; then
 echo 'Usage: build_candidate.sh absolute-recovery-ROM absolute-manifest absolute-NEW-build-directory' >&2;exit 2
fi
cmake -S "$root" -B "$3" -DBRIDGE_MODE=firmware -DPICO_BOARD=waveshare_core2350b \
 -DPICO_PLATFORM=rp2350-arm-s -DCMAKE_BUILD_TYPE=Release -DSPRINGBOARD_BANK_MSC=ON \
 -DSPRINGBOARD_CS1_MSC=OFF -DSPRINGBOARD_USB_MSC_ROM=OFF -DSPRINGBOARD_USB_ROM_STORAGE=OFF \
 -DSPRINGBOARD_ASYNC_STAGE_A=OFF -DSPRINGBOARD_PIO_REARM=OFF \
 "-DSPRINGBOARD_ROM=$1" "-DSPRINGBOARD_ROM_MANIFEST=$2"
cmake --build "$3" --target springboard_bank_msc --parallel 4
