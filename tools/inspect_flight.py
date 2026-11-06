"""Read a closed rolling window or frozen incident; optionally make an offline viewer.

These are observed poses/controls at 10 Hz, not deterministic gesture replays.
"""
import argparse
import configparser
import json
import math
from pathlib import Path
import re
import shlex
import struct


def require(value, reason):
    if not value:
        raise ValueError(reason)


def read_segment(path):
    path = Path(path)
    require(not path.is_symlink() and path.stat().st_size <= 16 * 1024 * 1024, "Invalid segment size/path")
    samples, config, pending = [], None, None
    with path.open(encoding="utf-8") as stream:
        def row():
            text = stream.readline(16386)
            require(text.endswith("\n") and len(text) <= 16384, "Truncated or oversized flight row")
            return text.rstrip("\r\n")
        require(row() == "SWVR_FLIGHT 1", "Unsupported flight schema")
        while True:
            text = row()
            if text == "END":
                require(pending is None and not stream.read(1), "Incomplete sample or trailing data")
                return samples
            if text.startswith("C "):
                require(pending is None and text in {"C joystick", "C arm_swing"}, "Invalid configuration order/mode")
                lines, config_bytes = [], 0
                while True:
                    entry = row()
                    if entry == "END_CONFIG":
                        break
                    lines.append(entry)
                    config_bytes += len(entry) + 1
                    require(config_bytes <= 16384, "Oversized configuration")
                parser = configparser.ConfigParser(interpolation=None)
                parser.read_string("\n".join(lines))
                config = {section: dict(parser[section]) for section in parser.sections()}
                require(config and "steering" in config, "Missing flight configuration")
            elif text.startswith("F "):
                require(config is not None and pending is None, "Frame without configuration or diagnostic pair")
                fields = text.split()
                require(len(fields) == 501, "Invalid frame field count")
                elapsed, frame = int(fields[1]), int(fields[4])
                require(elapsed >= 0 and frame > 0, "Invalid frame identity")
                def real(index):
                    require(re.fullmatch(r"[0-9a-fA-F]{1,8}", fields[index]), "Invalid float bits")
                    value = struct.unpack("<f", struct.pack("<I", int(fields[index], 16)))[0]
                    return value if math.isfinite(value) else None
                # Keep the original row as well: it contains eye poses/FOV and
                # all joint orientations, radii and validity flags losslessly.
                hands = []
                for base in (31, 266):
                    active = int(fields[base])
                    require(active in (0, 1), "Invalid hand activity")
                    joints = []
                    for joint in range(26):
                        start = base + 1 + joint * 9
                        flags = int(fields[start + 8])
                        require(0 <= flags <= 15, "Invalid joint flags")
                        joints.append({"position": [real(start + k) for k in (4, 5, 6)], "flags": flags})
                    hands.append({"active": bool(active), "joints": joints})
                pending = {"elapsed_ms": elapsed / 1e6, "frame": frame, "hands": hands, "settings": config, "raw_frame": text}
            elif text.startswith("D "):
                require(pending is not None, "Diagnostic without frame")
                fields = shlex.split(text)
                require(0 <= int(fields[1]) <= 12 and len(fields) == int(fields[1]) + 2, "Invalid diagnostic count")
                require(all(len(value) <= 128 for value in fields[2:]), "Oversized diagnostic")
                pending["diagnostics"] = fields[2:]
                samples.append(pending)
                pending = None
            else:
                raise ValueError("Unknown flight record")


def read_window(directory):
    directory = Path(directory)
    require(directory.is_dir() and not directory.is_symlink(), "Expected flight or incident directory")
    paths = sorted(directory.glob("segment-*.tsv"), key=lambda p: int(p.stem.removeprefix("segment-")))
    require(0 < len(paths) <= 12, "Invalid retained segment count")
    samples = []
    for path in paths:
        require(re.fullmatch(r"segment-[0-9]+\.tsv", path.name), "Invalid segment name")
        samples.extend(read_segment(path))
    require(samples, "No retained samples")
    require(all(a["elapsed_ms"] <= b["elapsed_ms"] for a, b in zip(samples, samples[1:])), "Non-monotonic window")
    return {"schema": 1, "scope": "observations_not_deterministic_replay", "segments": len(paths), "samples": samples}


def build_viewer(data, destination):
    destination = Path(destination)
    require(not destination.exists(), "Output already exists")
    payload = json.dumps(data, ensure_ascii=False, allow_nan=False).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")
    template = Path(__file__).with_name("flight_viewer.html").read_text(encoding="utf-8")
    destination.write_text(template.replace("__FLIGHT_DATA__", payload), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--html", type=Path)
    args = parser.parse_args()
    result = read_window(args.directory)
    print(f"Samples: {len(result['samples'])}; segments: {result['segments']}; window: {result['samples'][0]['elapsed_ms']/1000:.1f}..{result['samples'][-1]['elapsed_ms']/1000:.1f} s")
    if args.html:
        build_viewer(result, args.html)
        print(f"Offline viewer: {args.html}")
