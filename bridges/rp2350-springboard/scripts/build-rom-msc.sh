#!/usr/bin/env bash
# Offline only: no programmer/USB/Prism access.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [[ $# != 3 || $1 != /* || $2 != /* || $3 != /* || -e $3 ]]; then
 echo "Usage: $0 absolute-recovery-ROM absolute-manifest absolute-NEW-output-directory" >&2;exit 2
fi
cmake -S "$root" -B "$3" -DBRIDGE_MODE=firmware -DPICO_BOARD=waveshare_core2350b \
 -DPICO_PLATFORM=rp2350-arm-s -DCMAKE_BUILD_TYPE=Release -DSPRINGBOARD_USB_MSC_ROM=ON \
 -DSPRINGBOARD_PIO_REARM=OFF -DSPRINGBOARD_ASYNC_STAGE_A=OFF \
 "-DSPRINGBOARD_ROM=$1" "-DSPRINGBOARD_ROM_MANIFEST=$2"
cmake --build "$3" --target springboard_rom_msc --parallel 4
