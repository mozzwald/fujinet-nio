#!/usr/bin/env python3
"""Embed a verified Palm ROM, or a non-ROM test pattern for passive bring-up."""
import argparse
from pathlib import Path
import zlib

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("output", type=Path)
p.add_argument("--rom", type=Path)
args = p.parse_args()
data = args.rom.read_bytes() if args.rom else bytes.fromhex("1234a55affff0000")
if not data or len(data) > 65536 or len(data) % 2:
    p.error("image must be nonempty, word-aligned and at most 65536 bytes")
if args.rom and (len(data) < 256 or data[8:12] != bytes.fromhex("feedbeef")):
    p.error("image does not have a Palm card signature; run the Palm ROM verifier first")
args.output.write_text(
    "/* Generated; the default pattern is not a Palm ROM. */\n#pragma once\n"
    f"#define SB_ROM_IMAGE_SIZE {len(data)}u\n"
    f"#define SB_ROM_IMAGE_CRC 0x{zlib.crc32(data):08x}u\n"
    "static const unsigned char sb_rom_image[] = {\n" +
    ",\n".join(",".join(f"0x{x:02x}" for x in data[i:i+16]) for i in range(0, len(data), 16)) +
    "\n};\n")
