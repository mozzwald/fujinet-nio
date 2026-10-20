#!/usr/bin/env python3
"""Capture a triggered Springboard read burst through sigrok-cli and analyze it.

Connect the documented passive probes first. Does not touch RP serial, drive bus
pins or generate the Palm read stimulus. Each invocation requires a new directory.
"""
import argparse
from datetime import datetime, timezone
import configparser
import hashlib
import json
from pathlib import Path
import signal
import subprocess
import time
from zipfile import BadZipFile

from analyze_timing import analyze, load_session, write_csv


def utc():
    return datetime.now(timezone.utc).isoformat()


def capture(args):
    if not 1000 <= args.samples <= 32_000_000:
        raise ValueError("samples must be within 1000..32000000")
    if args.rate <= 0 or not 1 <= args.timeout <= 600:
        raise ValueError("positive rate and timeout within 1..600 seconds required")
    command = ["sigrok-cli", "--driver", args.driver,
               "--config", f"samplerate={args.rate}:captureratio=10",
               "--channels", ",".join(f"D{i}" for i in range(8)),
               "--samples", str(args.samples), "--triggers", "D0=f",
               "--loglevel", "3", "--output-format", "srzip", "--output-file", str(args.directory / "capture.sr")]
    args.directory.mkdir(parents=True, exist_ok=False)
    metadata = {"started_utc": utc(), "command": command,
                "rate_requested_hz": args.rate, "samples_requested": args.samples,
                "trigger": "D0 falling", "pretrigger_requested_percent": 10,
                "wiring": {f"D{i}": name for i, name in enumerate(
                    ["CS0", "OE", "WE", "RESET", "A1", "A2", "A3", "CS1"])},
                "timed_out": False, "interrupted": False,
                "transport_completeness_verified": False,
                "hardware_arm_acknowledgement": False}
    metadata_path = args.directory / "acquisition.json"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    # Neither --wait-trigger nor a wall-clock --time limit: keep the pretrigger
    # history and let the user manually start the eight-read burst.
    with (args.directory / "sigrok.stdout.log").open("w") as stdout, \
         (args.directory / "sigrok.stderr.log").open("w") as stderr:
        version = subprocess.run(["sigrok-cli", "--version"], capture_output=True, text=True)
        (args.directory / "sigrok-version.txt").write_text(version.stdout + version.stderr)
        # sigrok-cli treats readable stdin, including inherited EOF, as a stop
        # key. Hold an empty private pipe open until the acquisition finishes.
        process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                   stdout=stdout, stderr=stderr)
        deadline = time.monotonic() + args.timeout
        try:
            time.sleep(1)
            if process.poll() is None:
                print("CAPTURE PROCESS RUNNING: CS0 falling-edge trigger configured. "
                      "No hardware arm acknowledgement is exposed by sigrok-cli.", flush=True)
                print("Run the Palm Read test once when directed; this tool never generates it.", flush=True)
            process.wait(timeout=max(0.01, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            metadata["timed_out"] = True
        except KeyboardInterrupt:
            metadata["interrupted"] = True
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stdin.close()
            metadata.update({"ended_utc": utc(), "sigrok_returncode": process.returncode})
            metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    if metadata["timed_out"] or metadata["interrupted"] or process.returncode:
        print(f"Capture incomplete; preserve {args.directory} including acquisition logs.", flush=True)
        return 1
    session = args.directory / "capture.sr"
    samples, rate = load_session(session)
    metadata.update({"session_sha256": hashlib.sha256(session.read_bytes()).hexdigest(),
                     "samples_saved": len(samples), "rate_saved_hz": rate})
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    if len(samples) != args.samples or rate != args.rate:
        raise ValueError("saved sample count/rate differs from request; inspect acquisition logs")
    report = analyze(samples, rate)
    report["session"] = str(session.resolve())
    (args.directory / "timing.json").write_text(json.dumps(report, indent=2) + "\n")
    write_csv(args.directory / "cycles.csv", report)
    print(f"Saved {len(samples)} samples at {rate} Hz: {report['result']}; "
          f"{report['complete_cs0_pulses']} complete CS0 pulses. Evidence: {args.directory}", flush=True)
    return 0 if report["expected_eight_read_sequence_match"] else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--driver", default="fx2lafw", help="use fx2lafw:conn=BUS.ADDRESS from --scan")
    parser.add_argument("--rate", type=int, default=12_000_000)
    parser.add_argument("--samples", type=int, default=1_000_000)
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    try:
        return capture(args)
    except (OSError, ValueError, KeyError, configparser.Error, BadZipFile) as error:
        parser.exit(1, f"Capture failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
