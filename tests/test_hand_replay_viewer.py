import json
from pathlib import Path
import sys
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from view_hand_replay import build


def test_viewer_is_standalone_and_escapes_untrusted_text(tmp_path):
    source, output = tmp_path / "input.json", tmp_path / "viewer.html"
    source.write_text(json.dumps({"schema": 1, "complete": True, "events": [{"type": "</script><script>alert(1)</script>"}], "samples": []}), encoding="utf-8")
    assert build(source, output) == (0, 1)
    text = output.read_text(encoding="utf-8")
    assert "\u005cu003c/script\u005cu003e" in text
    assert "<script>alert(1)" not in text
    assert "https://" not in text and "fetch(" not in text
    assert "__HAND_REPLAY_DATA__" not in text


def test_incomplete_or_existing_output_is_rejected(tmp_path):
    source, output = tmp_path / "input.json", tmp_path / "viewer.html"
    source.write_text(json.dumps({"schema": 1, "complete": False, "events": [], "samples": []}), encoding="utf-8")
    with pytest.raises(ValueError):
        build(source, output)
    assert not output.exists()
    source.write_text(json.dumps({"schema": 1, "complete": True, "events": [], "samples": []}), encoding="utf-8")
    output.write_text("sentinel", encoding="utf-8")
    with pytest.raises(ValueError):
        build(source, output)
    assert output.read_text() == "sentinel"
