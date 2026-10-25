#!/usr/bin/env bash
# Separate diagnostic build only; never flashes or arms. Only GPIO39 side-set
# placement changes; data, qualification and ARM source stay the same.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [[ $# != 3 || $1 != /* || $2 != /* || $3 != /* ]]; then
    echo "Usage: $0 absolute-ROM absolute-verifier-manifest absolute-output-directory" >&2
    exit 2
fi
cmake -S "$root" -B "$3" -DBRIDGE_MODE=firmware \
    -DPICO_BOARD=waveshare_core2350b -DPICO_PLATFORM=rp2350-arm-s \
    -DCMAKE_BUILD_TYPE=Release -DSPRINGBOARD_USB_DEBUG=ON \
    -DSPRINGBOARD_CONTINUOUS=OFF -DSPRINGBOARD_PIO_REARM=ON \
    -DSPRINGBOARD_REPLY_MARKER=ON -DSPRINGBOARD_ROM_SMOKE=OFF \
    -DSPRINGBOARD_LATENCY_DIAGNOSTICS=OFF -DSPRINGBOARD_RX_FIRST=OFF \
    "-DSPRINGBOARD_ROM=$1" "-DSPRINGBOARD_ROM_MANIFEST=$2"
cmake --build "$3" --target springboard_rom_pio_rearm --parallel 4
