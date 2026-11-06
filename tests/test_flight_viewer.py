import sys
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_flight import read_segment, read_window, build_viewer


def sample(frame=1):
    fields = ["F", "100000000", "0", "0", str(frame), "1000000000", "1", "11", "15"]
    fields += ["0"] * 22
    for _ in range(2):
        fields.append("1")
        fields += (["0"] * 8 + ["5"]) * 26
    return " ".join(fields)


def recording(frame=1):
    return 'SWVR_FLIGHT 1\nC joystick\n[steering]\nfull_lock_degrees=90\nEND_CONFIG\n' + sample(frame) + '\nD 1 "KIER 0.50"\nEND\n'


def test_segment_configuration_frames_and_diagnostics(tmp_path):
    source = tmp_path / "segment-1.tsv"
    source.write_text(recording(), encoding="utf-8")
    rows = read_segment(source)
    assert rows[0]["frame"] == 1 and rows[0]["elapsed_ms"] == 100
    assert rows[0]["settings"]["steering"]["full_lock_degrees"] == "90"
    assert rows[0]["hands"][0]["joints"][1]["position"] == [0, 0, 0]
    assert rows[0]["diagnostics"] == ["KIER 0.50"]
    (tmp_path / "segment-12.tsv").write_text(recording(12), encoding="utf-8")
    assert [s["frame"] for s in read_window(tmp_path)["samples"]] == [1, 12]


@pytest.mark.parametrize("mutate", [
    lambda s: s[:-4],
    lambda s: s + "extra\n",
    lambda s: s.replace('D 1 "KIER 0.50"\n', ''),
    lambda s: s.replace('D 1 ', 'D 2 '),
    lambda s: s.replace('C joystick', 'C unknown'),
    lambda s: s.replace(sample(), sample() + ' 0'),
    lambda s: s.replace('[steering]', '[unknown]'),
    lambda s: s.replace('[steering]', '\n' * 16385 + '[steering]'),
])
def test_invalid_or_incomplete_capture_is_rejected(tmp_path, mutate):
    source = tmp_path / "segment-1.tsv"
    source.write_text(mutate(recording()), encoding="utf-8")
    with pytest.raises(ValueError):
        read_segment(source)


def test_offline_viewer_escapes_recorded_text_and_never_overwrites(tmp_path):
    source = tmp_path / "segment-1.tsv"
    source.write_text(recording().replace("KIER 0.50", "</script><script>oops"), encoding="utf-8")
    output = tmp_path / "viewer.html"
    build_viewer(read_window(tmp_path), output)
    html = output.read_text(encoding="utf-8")
    assert "</script><script>oops" not in html and "\\u003c/script" in html
    assert "observations_not_deterministic_replay" in html
    with pytest.raises(ValueError):
        build_viewer(read_window(tmp_path), output)
