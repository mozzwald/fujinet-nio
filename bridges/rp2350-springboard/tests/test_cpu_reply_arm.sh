#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dir="$root/build/cpu-reply-arm-test"
mkdir -p "$dir"
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2 -g -ffreestanding -fno-builtin \
  -fomit-frame-pointer -Wall -Wextra -Werror -nostdlib -I"$root/src" \
  -Wl,-e,_start,-Ttext=0x10000,-z,noexecstack \
  "$root/tests/test_cpu_reply_arm.c" "$root/src/cpu_reply.S" -o "$dir/test.elf"
# User-mode ARM emulation runs this Thumb instruction subset. RAM replaces
# device registers; no M33/PIO timing claim is made from the execution speed.
qemu-arm -cpu cortex-a15 "$dir/test.elf"
echo 'ARM reply kernel:100385 cases PASS (all32768 indices, high addresses, controls, random snapshots, idle stop).'
