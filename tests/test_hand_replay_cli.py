import csv
import json
from pathlib import Path
import struct
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build/capture/Release/swvr_replay_hands.exe"
pytestmark = pytest.mark.skipif(not EXE.is_file(), reason="Build the Windows replay executable first")


def bits(value):
    return f"{struct.unpack('<I', struct.pack('<f', value))[0]:x}"


def pose(position):
    return [bits(value) for value in (0, 0, 0, 1, *position)]


def sample(index, gap, forward):
    tokens = ["F", str(index * 16000000), "1", "0", str(index), str(1000000000 + index * 16000000), "7", "11", "15"]
    for x in (-0.032, 0.032):
        tokens += pose((x, 1.6, 0)) + [bits(v) for v in (-0.8, 0.8, 0.8, -0.8)]
    for hand in range(2):
        tokens += ["1" if hand == 0 else "0"]
        for joint in range(26):
            position = {1: (-0.3, 1.1, -0.35 - forward), 5: (-0.3, 1.1, -0.4 - forward),
                        10: (-0.3, 1.1, -0.5 - forward), 15: (-0.3 + gap, 1.1, -0.4 - forward)}.get(joint, (0, 0, 0))
            tokens += pose(position) + [bits(0.01), "5"]
    return " ".join(tokens) + "\n"


def recording(tmp_path):
    path = tmp_path / "hands.tsv"
    path.write_text("SWVR_HANDS 1\nC joystick\n[joystick]\n[arm_swing]\nEND_CONFIG\n" +
                    sample(1, 0.06, 0) + sample(2, 0.01, 0) + sample(3, 0.01, 0.14) + sample(4, 0.06, 0.14) + "END\n", encoding="ascii")
    return path


def run(*args):
    return subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True)


def rows(path):
    with path.open(encoding="utf-8") as stream:
        return list(csv.DictReader((line for line in stream if not line.startswith("#")), delimiter="\t"))


def test_replay_starts_and_releases_sprint_without_game(tmp_path):
    source = recording(tmp_path)
    destination = tmp_path / "replay.tsv"
    result = run(source, destination)
    assert result.returncode == 0, result.stderr
    assert [int(row["native_bits"]) for row in rows(destination)] == [0, 0, 132, 0]
    assert "#complete\tframes=4" in destination.read_text()


def test_override_rechecks_same_motion_with_different_thresholds(tmp_path):
    source = recording(tmp_path)
    settings = tmp_path / "gestures.ini"
    settings.write_text("[joystick]\nactivation=middle_pinch\nsprint_on_m=0.3\nsprint_off_m=0.25\n", encoding="ascii")
    destination = tmp_path / "replay.tsv"
    result = run(source, destination, "--config", settings)
    assert result.returncode == 0, result.stderr
    assert int(rows(destination)[2]["native_bits"]) == 4
    assert "#overrides\tsettings=1" in destination.read_text()


def test_incomplete_recording_has_no_complete_marker(tmp_path):
    source = recording(tmp_path)
    source.write_text(source.read_text()[:-4], encoding="ascii")
    destination = tmp_path / "partial.tsv"
    assert run(source, destination).returncode != 0
    assert "#complete" not in destination.read_text()


def test_existing_output_and_source_cannot_be_overwritten(tmp_path):
    source = recording(tmp_path)
    original = source.read_bytes()
    assert run(source, source).returncode != 0
    assert source.read_bytes() == original
    destination = tmp_path / "existing.tsv"
    destination.write_text("sentinel", encoding="ascii")
    assert run(source, destination).returncode != 0
    assert destination.read_text() == "sentinel"


def test_json_export_contains_tracked_geometry(tmp_path):
    source = recording(tmp_path)
    destination, data = tmp_path / "output.tsv", tmp_path / "output.json"
    result = run(source, destination, "--json", data)
    assert result.returncode == 0, result.stderr
    parsed = json.loads(data.read_text())
    assert parsed["schema"] == 1 and parsed["complete"] and parsed["mismatches"] == 0
    assert len(parsed["samples"]) == 4 and len(parsed["samples"][0]["hands"][0]["joints"]) == 26
    assert parsed["events"][2]["sprint"]


def test_full_journal_replays_native_cancellation(tmp_path):
    source, destination, data = tmp_path / "journal.tsv", tmp_path / "output.tsv", tmp_path / "output.json"
    records = []
    def event(t, text):
        records.append(f"Q {len(records)+1} {t} {t}\n" + text)
    event(0, "C joystick\n[joystick]\nEND_CONFIG\n")
    event(16000000, sample(1, .06, 0)); event(16000001, "I 0\n")
    event(32000000, sample(2, .01, 0)); event(32000001, "I 0\n")
    event(48000000, sample(3, .01, .14)); event(48000001, "I 132\n")
    event(49000000, "N 8 8 0\n"); event(49000001, 'G "physical_keyboard"\n'); event(49000002, 'R "physical_keyboard"\n')
    event(50000000, "I 0\n"); event(60000000, 'G ""\n')
    event(64000000, sample(4, .01, .14)); event(64000001, "I 0\n")
    source.write_text("SWVR_HANDS 2\n" + "".join(records) + "END\n", encoding="ascii")
    result = run(source, destination, "--json", data)
    assert result.returncode == 0, result.stderr
    parsed = json.loads(data.read_text())
    assert parsed["mismatches"] == 0
    assert any(e["type"] == "reset:physical_keyboard" for e in parsed["events"])
    assert any(e["blocks"] == 8 and e["physical"][0] == "8" for e in parsed["events"])
    assert not parsed["events"][-1]["sprint"]
