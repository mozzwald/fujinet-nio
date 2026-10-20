#!/usr/bin/env python3
"""Analyze eight-channel passive Springboard timing from a sigrok .sr session.

Wiring: D0=/CS0 D1=/OE D2=/WE D3=/RESET D4=A1 D5=A2 D6=A3 D7=/CS1.
Sampled intervals are coarse observations, never an active ROM timing pass.
"""
import argparse
import bisect
import configparser
import csv
from decimal import Decimal
import json
from pathlib import Path
import re
from zipfile import BadZipFile, ZipFile

MAX_BYTES = 64 * 1024 * 1024
CHANNELS = ["CS0", "OE", "WE", "RESET", "A1", "A2", "A3", "CS1"]


def load_session(path):
    with ZipFile(path) as archive:
        if archive.read("version").strip() != b"2":
            raise ValueError("only sigrok session format version 2 is supported")
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(archive.read("metadata").decode())
        devices = [s for s in config.sections() if s.startswith("device ")]
        if devices != ["device 1"]:
            raise ValueError("capture must contain exactly device 1")
        info = config["device 1"]
        if info.getint("unitsize") != 1 or info.getint("total probes") != 8:
            raise ValueError("capture must have all eight channels, one byte per sample")
        if [info.get(f"probe{i}") for i in range(1, 9)] != [f"D{i}" for i in range(8)]:
            raise ValueError("channels must retain names/order D0..D7")
        rate_match = re.fullmatch(r"\s*([0-9]+(?:\.[0-9]+)?)\s*(Hz|kHz|MHz|GHz)\s*",
                                  info["samplerate"])
        if not rate_match:
            raise ValueError("unrecognized sample rate")
        rate = Decimal(rate_match[1]) * {"Hz": 1, "kHz": 1000, "MHz": 1000000,
                                        "GHz": 1000000000}[rate_match[2]]
        if rate <= 0 or rate != int(rate):
            raise ValueError("sample rate must be a positive integer in Hz")
        base = info["capturefile"]
        pattern = re.compile(re.escape(base) + r"-(\d+)$")
        chunks = sorted((int(m[1]), name) for name in archive.namelist()
                        if (m := pattern.fullmatch(name)))
        if not chunks or [n for n, _ in chunks] != list(range(1, len(chunks) + 1)):
            raise ValueError("logic chunks must be contiguous starting at 1")
        if sum(archive.getinfo(name).file_size for _, name in chunks) > MAX_BYTES:
            raise ValueError("capture exceeds 64 MiB analysis limit")
        samples = b"".join(archive.read(name) for _, name in chunks)
        if len(samples) < 2:
            raise ValueError("capture is empty or too short")
        return samples, int(rate)


def low_pulses(samples, bit):
    mask = 1 << bit
    pulses = []
    start = 0 if not samples[0] & mask else None
    previous = bool(samples[0] & mask)
    for index, value in enumerate(samples[1:], 1):
        level = bool(value & mask)
        if previous and not level:
            start = index
        elif not previous and level:
            pulses.append((start, index, start == 0, False))
            start = None
        previous = level
    if start is not None:
        pulses.append((start, len(samples), start == 0, True))
    return pulses


def analyze(samples, rate):
    if rate <= 0 or len(samples) < 2:
        raise ValueError("positive rate and at least two samples required")
    ns = 1e9 / rate
    cs = low_pulses(samples, 0)
    oe = low_pulses(samples, 1)
    oe_ends = [pulse[1] for pulse in oe]
    addr_edges = [i for i in range(1, len(samples))
                  if (samples[i] ^ samples[i - 1]) & 0x70]
    cycles = []
    warnings = []
    previous_end = None
    for start, end, clipped_start, clipped_end in cs:
        if clipped_start or clipped_end:
            warnings.append(f"CS0 pulse at sample {start} is capture-boundary clipped")
            continue
        pos = bisect.bisect_right(oe_ends, start)
        overlaps = []
        while pos < len(oe) and oe[pos][0] < end:
            overlaps.append(oe[pos])
            pos += 1
        segment = samples[start:end]
        forbidden = [name for bit, name in ((2, "WE asserted"), (3, "RESET asserted"),
                                             (7, "CS1 asserted"))
                     if any(not value & (1 << bit) for value in segment)]
        addresses = sorted({((value >> 4) & 7) * 2 for value in segment})
        last = bisect.bisect_right(addr_edges, start) - 1
        setup_samples = start - (addr_edges[last] if last >= 0 else 0)
        cycle = {
            "index": len(cycles), "start_sample": start, "end_sample": end,
            "cs_width_ns": (end - start) * ns,
            "gap_from_previous_cs_ns": None if previous_end is None else (start - previous_end) * ns,
            "low_address_bits": addresses[0] if len(addresses) == 1 else None,
            "low_address_stable_during_cs": len(addresses) == 1,
            "low_address_setup_ns": setup_samples * ns,
            "low_address_setup_is_lower_bound": last < 0,
            "oe_overlap_count": len(overlaps), "invalid_control_states": forbidden,
            "oe_width_ns": None, "oe_minus_cs_fall_ns": None,
            "oe_minus_cs_rise_ns": None, "read_overlap_ns": None,
            "read_observed": False,
        }
        if len(overlaps) == 1:
            os, oe_end, os_clip, oe_clip = overlaps[0]
            cycle.update({
                "oe_width_ns": None if os_clip or oe_clip else (oe_end - os) * ns,
                "oe_minus_cs_fall_ns": None if os_clip else (os - start) * ns,
                "oe_minus_cs_rise_ns": None if oe_clip else (oe_end - end) * ns,
                "read_overlap_ns": (min(end, oe_end) - max(start, os)) * ns,
                "read_observed": not forbidden and not os_clip and not oe_clip,
            })
            if os_clip or oe_clip:
                warnings.append(f"OE pulse overlapping CS0 cycle {len(cycles)} is clipped")
        cycles.append(cycle)
        previous_end = end
    offsets = [cycle["low_address_bits"] for cycle in cycles]
    expected = list(range(0, 16, 2))
    match = (offsets == expected and all(c["read_observed"] for c in cycles)
             and not warnings)
    widths = [c["cs_width_ns"] for c in cycles]
    return {
        "scope": "coarse passive CS0 observation; low address bits A1..A3 only",
        "result": "MATCH" if match else "INCONCLUSIVE",
        "expected_eight_read_sequence_match": match,
        "samplerate_hz": rate, "sample_interval_ns": ns,
        "interval_quantization_uncertainty_ns": ns,
        "uncertainty_note": "+/- one sample for an edge difference, excluding clock, threshold and probe errors",
        "sample_count": len(samples), "capture_duration_ms": len(samples) / rate * 1000,
        "complete_cs0_pulses": len(cycles), "clipped_cs0_pulses": len(cs) - len(cycles),
        "read_observations": sum(c["read_observed"] for c in cycles),
        "observed_low_address_sequence": offsets, "expected_low_address_sequence": expected,
        "cs_width_min_ns": min(widths) if widths else None,
        "cs_width_max_ns": max(widths) if widths else None,
        "cycles": cycles, "warnings": warnings,
        "timing_qualified": False, "hardware_rom_response_verified": False,
        "limitations": [
            "No data channels or upper addresses are observed.",
            "Missing short pulses or USB sample loss cannot be excluded by this file alone; retain acquisition logs.",
            "Sampling cannot establish 5/15 ns relationships at 24 MHz or a 20 ns active-response margin.",
            "OE/CS overlap is not the CPU data-sampling deadline or RP response latency.",
        ],
    }


def write_csv(path, report):
    fields = ["index", "start_sample", "end_sample", "cs_width_ns", "oe_width_ns",
              "oe_minus_cs_fall_ns", "oe_minus_cs_rise_ns", "read_overlap_ns",
              "gap_from_previous_cs_ns", "low_address_bits", "low_address_stable_during_cs",
              "low_address_setup_ns", "low_address_setup_is_lower_bound", "read_observed"]
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(report["cycles"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()
    try:
        samples, rate = load_session(args.session)
        report = analyze(samples, rate)
        report["session"] = str(args.session.resolve())
        output = json.dumps(report, indent=2) + "\n"
        if args.json:
            args.json.write_text(output)
        if args.csv:
            write_csv(args.csv, report)
        print(output, end="")
    except (OSError, ValueError, KeyError, configparser.Error, BadZipFile) as error:
        parser.exit(1, f"Timing analysis failed: {error}\n")
    return 0 if report["expected_eight_read_sequence_match"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
