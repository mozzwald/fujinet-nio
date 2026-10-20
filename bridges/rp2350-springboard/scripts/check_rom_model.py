#!/usr/bin/env python3
"""Exercise the C SRAM/word model against a separately verified Palm ROM.

This is a host check, not a bus response or RP2350 timing test. The manifest
must be produced by fujinet-palm/tools/verify-springboard-rom.py first.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def check(rom_path, manifest_path):
    data = rom_path.read_bytes()
    manifest = json.loads(manifest_path.read_text())
    digest = hashlib.sha256(data).hexdigest()
    if manifest.get("sha256") != digest or manifest.get("size") != len(data):
        raise ValueError("ROM differs from independently verified manifest")
    if not 512 <= len(data) <= 65536 or len(data) % 2:
        raise ValueError("ROM size must be even and within 512..65536 bytes")
    if data[8:12] != bytes.fromhex("feedbeef"):
        raise ValueError("Palm card signature missing")
    with tempfile.TemporaryDirectory(prefix="springboard-rom-model-") as temp:
        library = Path(temp) / "model.so"
        subprocess.run([
            os.environ.get("HOST_CC", "cc"), "-std=c11", "-Wall", "-Wextra",
            "-Werror", "-O2", "-shared", "-fPIC", "-I", str(ROOT / "src"),
            str(ROOT / "src/bus_model.c"), "-o", str(library),
        ], check=True)
        model = ctypes.CDLL(str(library))
        byte = ctypes.c_uint8
        pointer = ctypes.POINTER(byte)
        model.sb_load_rom.argtypes = [pointer, ctypes.c_size_t, pointer,
                                     ctypes.c_size_t, ctypes.c_uint32]
        model.sb_load_rom.restype = ctypes.c_bool
        model.sb_rom_word.argtypes = [pointer, ctypes.c_size_t, ctypes.c_uint32]
        model.sb_rom_word.restype = ctypes.c_uint16
        model.sb_address.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        model.sb_address.restype = ctypes.c_uint32
        source = (byte * len(data)).from_buffer_copy(data)
        storage = (byte * (65536 + 2))(*([0xA5] * (65536 + 2)))
        destination = ctypes.cast(ctypes.byref(storage, 1), pointer)
        crc = zlib.crc32(data)
        if not model.sb_load_rom(destination, 65536, source, len(data), crc):
            raise ValueError("C SRAM model rejected verified image")
        if bytes(storage[1:1 + len(data)]) != data:
            raise ValueError("SRAM copy differs")
        if storage[0] != 0xA5 or any(x != 0xA5 for x in storage[1 + len(data):]):
            raise ValueError("SRAM copy overwrote guard/tail bytes")
        for offset in range(0, len(data), 2):
            expected = int.from_bytes(data[offset:offset + 2], "big")
            if model.sb_rom_word(destination, len(data), offset) != expected:
                raise ValueError(f"word mismatch at offset {offset:#x}")
        for offset in (1, len(data) - 1, len(data), 65534, 0xFFFFFFFE, 0xFFFFFFFF):
            if offset < len(data) and not offset & 1:
                continue
            if model.sb_rom_word(destination, len(data), offset) != 0xFFFF:
                raise ValueError(f"invalid offset did not return fill: {offset:#x}")
        before = bytes(storage)
        source[128] ^= 1
        if model.sb_load_rom(destination, 65536, source, len(data), crc):
            raise ValueError("corrupt source accepted")
        if bytes(storage) != before:
            raise ValueError("rejected image changed destination")
        # Exercise sparse upper address lines without allocating a 16 MiB image.
        addresses = [1 << bit for bit in range(1, 24)]
        addresses += [0xFFFE, 0x10000, 0x7FFFE, 0x80000, 0xFFFFFE]
        for address in addresses:
            low = address & 0xFFFE
            high = ((address & 0x70000) << 4) | ((address & 0xF80000) << 5)
            for led in (0, 1 << 23):
                if model.sb_address(low | 1, high | led) != address:
                    raise ValueError(f"address reconstruction failed: {address:#x}")
    return {
        "result": "PASS", "scope": "host SRAM/word/address model only",
        "rom_sha256": digest, "rom_bytes": len(data), "crc32": f"{crc:08x}",
        "words_checked": len(data) // 2,
        "first_words": [f"{int.from_bytes(data[i:i + 2], 'big'):04x}"
                        for i in range(0, 16, 2)],
        "signature_words": ["feed", "beef"],
        "sparse_address_cases": len(addresses) * 2,
        "corrupt_image_rejected_without_writes": True,
        "hardware_rom_response_verified": False,
        "hardware_timing_verified": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = check(args.rom, args.manifest)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ROM model check failed: {error}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
