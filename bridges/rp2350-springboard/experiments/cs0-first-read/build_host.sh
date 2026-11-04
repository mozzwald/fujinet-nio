#!/usr/bin/env bash
# Compile analysis ELF and unchanged shared PIO fixture. No firmware or devices.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exp="$root/experiments/cs0-first-read"
[[ $# == 1 && $1 == /* && ! -e $1 ]] || { echo 'Expected NEW absolute output directory' >&2;exit 2; }
bash "$root/experiments/unified-bus/build_host.sh" "$1"
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -nostdlib -Wl,-Ttext=0x20001000 -Wl,--entry=sb_cs1_reply "$exp/workers.S" -o "$1/cs0-first-HOST-ONLY.elf"
arm-none-eabi-objdump -d "$1/cs0-first-HOST-ONLY.elf" > "$1/cs0-first.disassembly.txt"
printf '%s\n' 'HOST ONLY. No startup/vector table, USB, ROM, or UF2. DO NOT FLASH.' > "$1/NOT_FIRMWARE.txt"
