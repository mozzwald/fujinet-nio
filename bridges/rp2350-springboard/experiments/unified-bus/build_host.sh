#!/usr/bin/env bash
# Standalone HOST ONLY. No firmware, USB devices, startup, ROM embedding or UF2.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exp="$root/experiments/unified-bus"
if [[ $# != 1 || $1 != /* || -e $1 ]]; then
 echo 'Usage: build_host.sh absolute-NEW-output-directory' >&2; exit 2
fi
out=$1
mkdir -p "$out"
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -nostdlib -Wl,-Ttext=0x20001000 -Wl,--entry=sb_cs1_reply "$exp/workers.S" -o "$out/workers-audit.elf"
cc -shared -fPIC -O3 -DNDEBUG -std=gnu11 -fshort-enums -Wall -Wextra -Werror \
 -DAPIO_EMULATION=1 -DSB_ASYNC_CAPTURE=1 \
 -I "$exp" -I "$root/src" -I "$root/include" \
 -I "$root/.deps/epio/include" -I "$root/.deps/apio/include" \
 "$root/tests/async_cosim.c" "$exp/probe_program.c" \
 "$root/src/cs1_registers.c" "$root/src/async_stage_a.c" \
 "$root/build/phase4-Release/libepio.a" "$root/build/phase4-Release/libsb_model.a" \
 -o "$out/libunified-audit.so"
arm-none-eabi-objdump -d "$out/workers-audit.elf" > "$out/workers.disassembly.txt"
printf '%s\n' 'HOST ONLY — do not flash. Expected matrix timing failures remain.' > "$out/NOT_FIRMWARE.txt"
