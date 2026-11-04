#!/usr/bin/env bash
# Host-only fixtures, deliberately separate from flashable firmware.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exp="$root/experiments/bank-msc"
if [[ $# != 1 || $1 != /* || -e $1 ]]; then echo 'Usage: build_models.sh absolute-NEW-output-directory' >&2;exit 2;fi
out=$1
mkdir -p "$out"
common=(-shared -fPIC -O3 -DNDEBUG -std=gnu11 -fshort-enums -Wall -Wextra -Werror
 -DAPIO_EMULATION=1 -DSB_ASYNC_CAPTURE=1 -Dsb_pio_rearm_program_init=sb_async_program_init
 -I "$exp" -I "$root/src" -I "$root/include" -I "$root/.deps/epio/include" -I "$root/.deps/apio/include")
sources=("$exp/program.c" "$root/src/cs1_registers.c" "$root/src/async_stage_a.c"
 "$root/build/phase4-Release/libepio.a" "$root/build/phase4-Release/libsb_model.a")
cc "${common[@]}" -DSB_BANK_FULL_CAPTURE_FIXTURE=1 "$root/tests/async_cosim.c" "${sources[@]}" -o "$out/libfull-fixture.so"
cc "${common[@]}" "$exp/readonly_model.c" "${sources[@]}" -o "$out/libreadonly.so"
cc "${common[@]}" "$exp/observed_model.c" "${sources[@]}" -o "$out/libobserved.so"
