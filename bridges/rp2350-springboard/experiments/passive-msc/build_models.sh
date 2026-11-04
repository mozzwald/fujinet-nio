#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exp="$root/experiments/passive-msc"
if [[ $# -lt 1 || $# -gt 2 || $1 != /* || -e $1 ]]; then echo 'Usage: build_models.sh absolute-NEW-directory' >&2;exit 2;fi
mkdir -p "$1"
mode=${2:-Release}
opt=-O3
if [[ $mode == Debug ]]; then opt=-O0;fi
common=(-shared -fPIC "$opt" -std=gnu11 -fshort-enums -Wall -Wextra -Werror -DAPIO_EMULATION=1
 -DSB_PIO_REPLY_MARKER=1 -I "$root/src" -I "$root/.deps/apio/include" -I "$root/.deps/epio/include")
cc "${common[@]}" "$root/tests/cs0_idle_cosim.c" "$root/src/pio_rearm_program.c" "$root/build/phase4-$mode/libepio.a" -o "$1/libreference.so"
cc "${common[@]}" "$exp/host_model.c" "$exp/wrapper.c" "$exp/native_alias.c" "$root/src/pio_rearm_program.c" "$root/build/phase4-$mode/libepio.a" -o "$1/libobserver.so"
