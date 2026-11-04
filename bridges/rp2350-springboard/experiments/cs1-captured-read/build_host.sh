#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exp="$root/experiments/cs1-captured-read"
[[ $# == 1 && $1 == /* && ! -e $1 ]] || { echo 'Expected NEW absolute host-output directory' >&2;exit 2; }
out=$1
mkdir -p "$out"
flags=(-std=gnu11 -O2 -fshort-enums -Wall -Wextra -Werror -DAPIO_EMULATION=1 -DSB_PIO_REPLY_MARKER=1 -I "$root/src" -I "$root/.deps/apio/include" -I "$root/.deps/epio/include")
cc "${flags[@]}" "$exp/capture.c" "$exp/test_capture.c" "$root/src/pio_rearm_program.c" "$root/build/phase4-Release/libepio.a" -o "$out/test-capture"
for mode in reference captured;do
 extra=();[[ $mode == reference ]] || extra=(-DSB_CAPTURED_READ=1 "$exp/capture.c")
 cc "${flags[@]}" -fPIC -shared "$exp/cosim.c" "$root/src/pio_rearm_program.c" "${extra[@]}" "$root/build/phase4-Release/libepio.a" -o "$out/lib$mode.so"
done
printf '%s\n' 'HOST ONLY input descriptor. No active CS1 response or firmware. Never flash.' > "$out/NOT_FIRMWARE.txt"
"$out/test-capture"
