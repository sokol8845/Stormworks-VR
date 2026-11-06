"""Build a standalone, offline hand replay viewer from swvr_replay_hands --json."""
import argparse
import json
from pathlib import Path


def build(source, destination):
    source, destination = Path(source), Path(destination)
    if destination.exists() or source.resolve() == destination.resolve():
        raise ValueError("Output exists or aliases source")
    if source.stat().st_size > 160 * 1024 * 1024:
        raise ValueError("Replay JSON too large")
    data = json.loads(source.read_text(encoding="utf-8"))
    if data.get("schema") != 1 or not data.get("complete") or not isinstance(data.get("events"), list) or not isinstance(data.get("samples"), list):
        raise ValueError("Expected complete replay JSON schema 1")
    payload = json.dumps(data, ensure_ascii=False, allow_nan=False, separators=(",", ":")).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")
    template = Path(__file__).with_name("hand_replay_viewer.html").read_text(encoding="utf-8")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(template.replace("__HAND_REPLAY_DATA__", payload), encoding="utf-8")
    return len(data["samples"]), len(data["events"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    samples, events = build(args.source, args.output)
    print(f"Offline viewer: {args.output} ({samples} frames, {events} events)")
