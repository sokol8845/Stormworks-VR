import sys
from pathlib import Path
import json
import subprocess

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from summarize_preview import summarize, markdown


BASE = """#openxr_runtime\tSteamVR
#openxr_state\tinitialized_gameplay_experiment_seconds=30
#locomotion_mode\tarm_swing
#locomotion\thooks_created
#gesture_config\tconfig/gestures.ini
#gesture_settings\tstroke_m=0.06 start_half_strokes=3
#openxr_readiness\treason=tracking waited_ms=5000
#openxr_readiness\treason=ready waited_ms=6100
#avatar_calibration_totals\tvalid_frames=5 longest_hold_ms=1400 holds=1 accepted=0 hand_not_tracked=10
#openxr_totals\tsubmitted=100 left_hand_samples=140 right_hand_samples=130
#openxr_state\tclosed
#locomotion_totals\tpackets=6 walk=0 sprint=5 releases=1 lifecycle_releases=1 holding=0 faults=0 last_fault=none
#arm_swing_totals\tframes=180 invalid_tracking=40 fists_open=10 outside_zone=5 starts=1 stops=1
#locomotion_drain\tcomplete
"""


def fixture(tmp_path, text=BASE, name="headset-xr-fixture.tsv"):
    capture = tmp_path / name
    capture.write_text("frame\tthread\n#exe_sha256\t" + "a" * 64 + "\n", encoding="utf-8")
    Path(str(capture) + ".xr.txt").write_text(text, encoding="utf-8")
    return capture


def test_complete_report_preserves_counts_and_limits_claims(tmp_path):
    capture = fixture(tmp_path)
    report = summarize(capture)
    assert report["log_status"] == "complete"
    assert report["runtime_kind"] == "headset_requested"
    assert report["totals"]["openxr_totals"]["left_hand_samples"] == 140
    assert report["calibration_blockers"] == {"hand_not_tracked": 10}
    assert report["readiness"][-1] == {"reason": "ready", "waited_ms": 6100}
    assert "Kalibracja T-pose nie została zaliczona." in report["warnings"]
    assert "przemieszczenia" in " ".join(report["limitations"])
    assert "140%" not in markdown(report)
    assert report["exe_sha256"] == "a" * 64


def test_truncated_latest_entry_cannot_replace_good_totals(tmp_path):
    report = summarize(fixture(tmp_path, BASE + "#locomotion_totals\tfaults=99"))
    assert report["totals"]["locomotion_totals"]["faults"] == 0
    assert any("niedokończony" in item for item in report["warnings"])


def test_repeated_totals_are_not_added_or_old_errors_hidden(tmp_path):
    report = summarize(fixture(tmp_path, BASE + "#locomotion_totals\tpackets=9 holding=1 faults=1\n#locomotion_drain\tpending\n#openxr_error\tnative transition failed\n"))
    assert report["log_status"] == "errors"
    assert report["totals"]["locomotion_totals"]["packets"] == 9
    assert any("bez sumowania" in item for item in report["warnings"])
    assert any("zwalniania ruchu" in item for item in report["missing_evidence"])


def test_no_sidecar_is_incomplete(tmp_path):
    report = summarize(tmp_path / "missing.tsv")
    assert report["log_status"] == "incomplete"
    assert report["runtime_kind"] == "unknown"
    assert len(report["warnings"]) == 2


def test_fake_runtime_never_becomes_headset_evidence(tmp_path):
    capture = fixture(tmp_path, name="fake-xr-fixture.tsv")
    report = summarize(capture, "headset")
    assert report["runtime_kind"] == "fake"
    assert "nie jest próbą w goglach" in report["limitations"][0]
    assert summarize(Path(str(capture) + ".xr.txt"))["capture"] == str(capture)


def test_missing_native_counts_do_not_look_successful(tmp_path):
    report = summarize(fixture(tmp_path, BASE.replace(" holding=0 faults=0", "")))
    assert report["log_status"] == "incomplete"
    assert "ruch.holding=brak" in report["missing_evidence"]


def test_unresolved_button_ownership_is_reported(tmp_path):
    text = BASE + "#hand_buttons\tinstalled\n#hand_buttons_totals\tfaults=0 pending=1 own_hold=0 blocked=0\n#button_retirement_totals\tretained=1 quarantined=0\n"
    report = summarize(fixture(tmp_path, text))
    assert report["log_status"] == "errors"
    assert "przyciski.pending=1" in report["errors"]
    assert "requesty.retained=1" in report["errors"]


def test_current_hook_state_checks_button_release(tmp_path):
    text = BASE + "#hand_buttons\thooks_created\n#hand_buttons_totals\tfaults=0 pending=1 own_hold=1 blocked=0\n#button_retirement_totals\tretained=1 quarantined=0\n"
    report = summarize(fixture(tmp_path, text))
    assert "przyciski.pending=1" in report["errors"]
    assert "przyciski.own_hold=1" in report["errors"]
    assert "zwolnienie przycisków dłoni" in report["missing_evidence"]


def test_native_keypad_and_wheel_are_not_reported_as_local_prototype(tmp_path):
    text = BASE + "#keypad_prototype\tready\n#vehicle_keypad_totals\topened=3 submitted=3 cancelled=0 rejected=0 faults=0\n#driver_wheel_totals\teyes=100 identified=20 draws=40 compile_fallbacks=0\n#control_recording_totals\tlimited=1 failed=0\n"
    report = summarize(fixture(tmp_path, text))
    output = markdown(report)
    assert "prototyp lokalny" not in output
    assert "Klawiatura pojazdu" in output and "submitted=3" in output
    assert "Kierownica" in output and "identified=20" in output
    assert any("sterowania" in warning and "limit" in warning for warning in report["warnings"])


def test_cli_writes_both_reports_without_modifying_sources(tmp_path):
    capture = fixture(tmp_path)
    script = Path(__file__).resolve().parents[1] / "tools/summarize_preview.py"
    original = capture.read_bytes()
    subprocess.run([sys.executable, str(script), str(capture)], check=True, capture_output=True)
    assert json.loads(Path(str(capture) + ".summary.json").read_text(encoding="utf-8"))["closed"]
    assert "Raport próby VR" in Path(str(capture) + ".summary.md").read_text(encoding="utf-8")
    failed = subprocess.run([sys.executable, str(script), str(capture), "--json", str(capture)], capture_output=True)
    assert failed.returncode != 0 and capture.read_bytes() == original


def test_rolling_capture_reports_lost_samples_and_unfinished_marker(tmp_path):
    report = summarize(fixture(tmp_path, BASE + "#flight_recording_totals\tsamples=10 dropped=2 rotations=3 markers=2 saved=1 failed=1\n"))
    assert "bufor_diagnostyczny.failed=1" in report["errors"]
    assert "zapis wszystkich oznaczonych problemów" in report["missing_evidence"]
    assert any("pominął próbki: 2" in value for value in report["warnings"])


def test_usability_diagnostics_preserve_modes_and_local_keypad_scope(tmp_path):
    text = BASE + "#locomotion_mode\tjoystick\n#gesture_reload\tapplied_after_release\n#gesture_reload_rejected\tInvalid threshold\n#avatar_profile\tloaded\n#avatar_profile\tbound_to_live_local_actor\n#hand_recording_totals\tsamples=10 limited=1 failed=0\n#keypad_prototype\taccepted_local value=12.3\n"
    report = summarize(fixture(tmp_path, text))
    assert report["locomotion_modes"] == ["arm_swing", "joystick"]
    assert report["gesture_changes"] == ["applied_after_release"]
    assert report["avatar_profile"][0] == "loaded"
    assert any("odrzucona" in note for note in report["warnings"])
    assert any("limit rozmiaru" in note for note in report["warnings"])
    assert "nie potwierdzają wprowadzenia liczby do gry" in markdown(report)
