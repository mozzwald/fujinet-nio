#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mode=${1:-host}
case "$mode" in
  host)
    python3 "$root/scripts/bootstrap.py" --mode host
    for config in Debug Release; do
      dir="$root/build/host-$config"
      cmake -S "$root" -B "$dir" -DBRIDGE_MODE=host -DCMAKE_BUILD_TYPE="$config"
      cmake --build "$dir" --parallel 4
      ctest --test-dir "$dir" --output-on-failure --no-tests=error
    done
    python3 "$root/tests/test_embed_rom.py"
    ;;
  firmware)
    usb_debug=${SPRINGBOARD_USB_DEBUG:-ON}
    case "$usb_debug" in
      ON|OFF) ;;
      *) echo "SPRINGBOARD_USB_DEBUG must be ON or OFF" >&2; exit 2 ;;
    esac
    python3 "$root/scripts/bootstrap.py" --mode firmware
    args=()
    if [[ $# -gt 1 ]]; then args+=("-DSPRINGBOARD_ROM=$2");
    else args+=("-DSPRINGBOARD_ROM="); fi
    cmake -S "$root" -B "$root/build/firmware" -DBRIDGE_MODE=firmware \
      -DPICO_BOARD=waveshare_core2350b -DPICO_PLATFORM=rp2350-arm-s \
      -DCMAKE_BUILD_TYPE=Release -DSPRINGBOARD_USB_DEBUG="$usb_debug" "${args[@]}"
    cmake --build "$root/build/firmware" --parallel 4
    ;;
  *) echo "Usage: $0 host | firmware [absolute-path-to-verified-ROM]" >&2; exit 2 ;;
esac
