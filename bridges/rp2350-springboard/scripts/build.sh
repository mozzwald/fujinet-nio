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
    python3 "$root/tests/test_timing.py"
    python3 "$root/tests/test_capture_timing.py"
    python3 "$root/tests/test_hantek_scope.py"
    ;;
  firmware|rom-smoke|rom-marker|rom-latency)
    usb_debug=${SPRINGBOARD_USB_DEBUG:-ON}
    case "$usb_debug" in
      ON|OFF) ;;
      *) echo "SPRINGBOARD_USB_DEBUG must be ON or OFF" >&2; exit 2 ;;
    esac
    python3 "$root/scripts/bootstrap.py" --mode firmware
    dir="$root/build/firmware"
    args=(-DSPRINGBOARD_LATENCY_DIAGNOSTICS=OFF -DSPRINGBOARD_ROM_SMOKE=OFF -DSPRINGBOARD_ROM_MANIFEST= -DSPRINGBOARD_DRIVE_MARKER=OFF)
    if [[ "$mode" == rom-smoke || "$mode" == rom-marker || "$mode" == rom-latency ]]; then
      if [[ $# -ne 3 ]]; then
        echo "$mode requires absolute ROM and verifier manifest paths" >&2; exit 2
      fi
      dir="$root/build/rom-smoke"
      args=(-DSPRINGBOARD_LATENCY_DIAGNOSTICS=OFF -DSPRINGBOARD_ROM_SMOKE=ON "-DSPRINGBOARD_ROM_MANIFEST=$3" -DSPRINGBOARD_DRIVE_MARKER=OFF)
      if [[ "$mode" == rom-marker ]]; then
        dir="$root/build/rom-marker"
        args+=(-DSPRINGBOARD_DRIVE_MARKER=ON)
      fi
      if [[ "$mode" == rom-latency ]]; then
        dir="$root/build/rom-latency"
        args=(-DSPRINGBOARD_LATENCY_DIAGNOSTICS=ON -DSPRINGBOARD_ROM_SMOKE=OFF
              "-DSPRINGBOARD_ROM_MANIFEST=$3" -DSPRINGBOARD_DRIVE_MARKER=OFF)
      fi
    fi
    if [[ $# -gt 1 ]]; then args+=("-DSPRINGBOARD_ROM=$2");
    else args+=("-DSPRINGBOARD_ROM="); fi
    cmake -S "$root" -B "$dir" -DBRIDGE_MODE=firmware \
      -DPICO_BOARD=waveshare_core2350b -DPICO_PLATFORM=rp2350-arm-s \
      -DCMAKE_BUILD_TYPE=Release -DSPRINGBOARD_USB_DEBUG="$usb_debug" "${args[@]}"
    cmake --build "$dir" --parallel 4
    ;;
  *) echo "Usage: $0 host | firmware [absolute-path-to-verified-ROM] | {rom-smoke|rom-marker|rom-latency} absolute-ROM absolute-manifest" >&2; exit 2 ;;
esac
