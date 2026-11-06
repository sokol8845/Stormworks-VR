import json
from pathlib import Path
import subprocess
import sys
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from summarize_preview import read_performance, summarize, markdown


def test_actual_cpp_performance_report(tmp_path):
    executable = Path(__file__).resolve().parents[1] / "build/capture/Release/swvr_vr_comfort_tests.exe"
    if not executable.is_file():
        pytest.skip("Build the C++ comfort test first")
    path = tmp_path / "fixture.tsv.performance.json"
    subprocess.run([str(executable), str(tmp_path), str(path)], check=True, capture_output=True)
    data = read_performance(path)
    assert data["schema"] == 2
    assert data["cpu_over_budget"] == data["gpu_over_budget"] == 1
    assert data["gpu_budget_samples"] == 2
    assert data["frame_gpu"]["p95_ms"] == 11
    report = summarize(tmp_path / "fixture.tsv", "fake")
    text = markdown(report)
    assert "1/2 (50.0%)" in text
    assert "Zakres klatki GPU" in text
    assert "nie są licznikiem reprojekcji SteamVR" in text


def test_legacy_missing_and_bad_sidecar(tmp_path):
    source = tmp_path / "old.tsv"
    assert summarize(source)["performance"] is None
    path = Path(str(source) + ".performance.json")
    path.write_text('{"schema":1}', encoding="utf-8")
    assert read_performance(path)["gpu_available"] is False
    assert "Brak pomiaru GPU" in markdown(summarize(source))
    path.write_text('{"schema":2,"gpu_over_budget":3,"gpu_budget_samples":2}', encoding="utf-8")
    assert summarize(source)["performance"] is None
    assert any("wydajności pominięty" in item for item in summarize(source)["warnings"])


@pytest.mark.parametrize("value", [float("nan"), -1, "<script>", True])
def test_non_numeric_or_invalid_timings_rejected(tmp_path, value):
    path = tmp_path / "bad.json"
    path.write_text(json.dumps({"schema": 2, "frame_gpu": {"count": 1, "mean_ms": value, "p95_ms": 1, "p99_ms": 1, "max_ms": 1}}), encoding="utf-8")
    with pytest.raises(ValueError):
        read_performance(path)
