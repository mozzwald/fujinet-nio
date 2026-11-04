#!/usr/bin/env bash
# Host analysis only; never opens a USB, serial, programmer or Palm device.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exp="$root/experiments/unified-bank"
fixture="$root/experiments/unified-bus"
if [[ $# != 1 || $1 != /* || -e $1 ]]; then
 echo 'Usage: build_host.sh absolute-NEW-output-directory' >&2; exit 2
fi
out=$1
mkdir -p "$out"
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -nostdlib -Wl,-Ttext=0x20001000 -Wl,--entry=sb_cs1_reply "$exp/workers.S" -o "$out/bank-a-HOST-ONLY.elf"
compile_model() {
 cc -shared -fPIC -O3 -DNDEBUG -std=gnu11 -fshort-enums -Wall -Wextra -Werror \
  -DAPIO_EMULATION=1 -DSB_ASYNC_CAPTURE=1 \
  -I "$fixture" -I "$root/src" -I "$root/include" \
  -I "$root/.deps/epio/include" -I "$root/.deps/apio/include" \
  "$1" "$fixture/probe_program.c" "$root/src/cs1_registers.c" "$root/src/async_stage_a.c" \
  "$root/build/phase4-Release/libepio.a" "$root/build/phase4-Release/libsb_model.a" -o "$2"
}
compile_model "$root/tests/async_cosim.c" "$out/libbank-fixture.so"
compile_model "$exp/publication_observer.c" "$out/libbank-observer.so"
cc -std=gnu11 -O2 -fshort-enums -Wall -Wextra -Werror -DAPIO_EMULATION=1 \
 -I "$root/.deps/apio/include" -I "$root/.deps/epio/include" \
 "$exp/token_capture.c" "$root/build/phase4-Release/libepio.a" -o "$out/token-capture-HOST-ONLY"
arm-none-eabi-objdump -d "$out/bank-a-HOST-ONLY.elf" > "$out/workers.disassembly.txt"
printf '%s\n' 'HOST ONLY. No startup/ROM/UF2. Never flash this ELF.' > "$out/NOT_FIRMWARE.txt"
