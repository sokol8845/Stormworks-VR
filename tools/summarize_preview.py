"""Offline summary of one bounded VR preview. Never starts or controls a process."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import re


def metadata(path, warnings, *, prefix_limit=None):
    records = defaultdict(list)
    if not path.is_file():
        warnings.append(f"Brak pliku: {path.name}")
        return records
    with path.open(encoding="utf-8-sig", errors="replace") as stream:
        consumed = 0
        for line in stream:
            consumed += len(line)
            if prefix_limit and consumed > prefix_limit:
                break  # Only startup identity from the large GL trace.
            if not line.startswith("#"):
                continue
            if not line.endswith("\n"):
                warnings.append(f"Pominięto niedokończony wpis: {path.name}")
                continue
            key, separator, value = line[1:].rstrip("\r\n").partition("\t")
            if not separator or not re.fullmatch(r"[a-z0-9_]+", key):
                warnings.append(f"Pominięto niepoprawny wpis: {path.name}")
                continue
            records[key].append(value)
    return records


def fields(value):
    result = {}
    for key, raw in re.findall(r"\b([a-z_][a-z0-9_]*)=([^\s]+)", value):
        if re.fullmatch(r"[0-9]+", raw):
            result[key] = int(raw)
        elif re.fullmatch(r"-?[0-9]+\.[0-9]+", raw):
            result[key] = float(raw)
        else:
            result[key] = raw
    return result


def read_performance(path):
    """Read the optional bounded sidecar; v1 is CPU-only, v2 adds GPU intervals."""
    if path.stat().st_size > 131072:
        raise ValueError("raport wydajności przekracza limit rozmiaru")
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schema") not in (1, 2):
        raise ValueError("nieobsługiwana wersja raportu wydajności")
    result = {"schema": data["schema"], "gpu_available": data.get("gpu_available") is True}
    for name in ("frame_interval", "avatar_pose_cpu", "overlay_cpu", "recording_cpu", "render_eye_cpu", "frame_work_cpu", "frame_gpu", "runtime_period"):
        if name not in data:
            continue
        stats = data[name]
        if not isinstance(stats, dict) or type(stats.get("count")) is not int or stats["count"] < 0:
            raise ValueError("niepoprawna liczba próbek wydajności")
        clean = {"count": stats["count"]}
        for field in ("mean_ms", "p95_ms", "p99_ms", "max_ms"):
            value = stats.get(field)
            if type(value) not in (float, int) or not math.isfinite(value) or value < 0:
                raise ValueError("niepoprawny czas w raporcie wydajności")
            clean[field] = value
        result[name] = clean
    for field in ("cpu_budget_samples", "cpu_over_budget", "gpu_budget_samples", "gpu_over_budget", "gpu_skipped", "gpu_pending"):
        value = data.get(field, 0)
        if type(value) is not int or value < 0:
            raise ValueError("niepoprawny licznik budżetu klatki")
        result[field] = value
    for prefix in ("cpu", "gpu"):
        if result[prefix + "_over_budget"] > result[prefix + "_budget_samples"]:
            raise ValueError("liczba przekroczeń większa od liczby próbek")
    return result


def summarize(capture, runtime="unknown"):
    capture = Path(capture)
    if str(capture).endswith(".xr.txt"):
        capture = Path(str(capture)[:-7])
    warnings = []
    performance = None
    performance_path = Path(str(capture) + ".performance.json")
    if performance_path.is_file():
        try:
            performance = read_performance(performance_path)
        except (ValueError, OSError, TypeError) as error:
            warnings.append(f"Raport wydajności pominięty: {error}")
    startup = metadata(capture, warnings, prefix_limit=262144)
    xr = metadata(Path(str(capture) + ".xr.txt"), warnings)
    last = lambda key: xr.get(key, [""])[-1]
    totals = {}
    for key, values in xr.items():
        if key.endswith("_totals") or key in ("openxr_frame_reuse", "openxr_cpu_timing", "gesture_settings"):
            totals[key] = fields(values[-1])
            if len(values) > 1:
                warnings.append(f"{key}: {len(values)} wpisów; użyto ostatniego, bez sumowania.")
    fake = (runtime == "fake" or capture.name.startswith("fake-xr-") or
            Path(str(capture) + ".fake.json").is_file() or "fake" in last("openxr_runtime").lower())
    runtime_kind = "fake" if fake else "headset_requested" if runtime == "headset" or capture.name.startswith("headset-xr-") else "unknown"
    errors = [f"{key}: {value}" for key, values in xr.items() if key.endswith("_error") for value in values]
    submitted = totals.get("openxr_totals", {}).get("submitted")
    closed = last("openxr_state") == "closed"
    missing = []
    if any("niedokończony" in warning for warning in warnings):
        missing.append("końcowy wpis logu")
    for key, values in xr.items():
        if key.endswith("_totals") and any(isinstance(fields(value).get("faults"), int) and fields(value)["faults"] > 0 for value in values):
            errors.append(f"{key}: zarejestrowano błąd w trakcie próby")
    if not closed:
        missing.append("zamknięcie sesji XR")
    if not isinstance(submitted, int) or submitted <= 0:
        missing.append("wysłane klatki XR")
    movement = totals.get("locomotion_totals", {})
    mode = last("locomotion_mode") or "off_or_unreported"
    if mode != "off_or_unreported":
        if last("locomotion") != "hooks_created":
            missing.append("instalacja obsługi ruchu")
        if last("locomotion_drain") != "complete":
            missing.append("zakończenie zwalniania ruchu")
        if not movement:
            missing.append("liczniki ruchu")
        for key in ("faults", "holding"):
            if movement.get(key) != 0:
                (errors if isinstance(movement.get(key), int) else missing).append(f"ruch.{key}={movement.get(key, 'brak')}")
        if mode == "arm_swing":
            swing = totals.get("arm_swing_totals", {})
            if not swing:
                missing.append("liczniki wymachów")
            if not swing.get("starts") or not movement.get("sprint"):
                warnings.append("Brak potwierdzonego rozpoznania startu i wysłania żądania sprintu.")
    hands = totals.get("hand_buttons_totals", {})
    if last("hand_buttons") in {"installed", "hooks_created"} or hands:
        if last("hand_buttons_drain") != "complete" or not hands:
            missing.append("zwolnienie przycisków dłoni")
        for key in ("faults", "pending", "own_hold", "blocked"):
            if hands.get(key) != 0:
                (errors if isinstance(hands.get(key), int) else missing).append(f"przyciski.{key}={hands.get(key, 'brak')}")
        retired = totals.get("button_retirement_totals", {})
        for key in ("retained", "quarantined"):
            if retired.get(key) != 0:
                (errors if isinstance(retired.get(key), int) else missing).append(f"requesty.{key}={retired.get(key, 'brak')}")
    calibration = totals.get("avatar_calibration_totals", {})
    recorded = totals.get("hand_recording_totals", {})
    if recorded.get("failed") == 1:
        errors.append("nagranie.failed=1")
    if recorded.get("limited") == 1:
        warnings.append("Nagranie śledzenia osiągnęło limit rozmiaru; obejmuje początkową część próby.")
    controls = totals.get("control_recording_totals", {})
    if controls.get("failed") == 1:
        errors.append("nagranie_sterowania.failed=1")
    if controls.get("limited") == 1:
        warnings.append("Nagranie sterowania osiągnęło limit rozmiaru; obejmuje początkową część próby.")
    flight = totals.get("flight_recording_totals", {})
    if flight.get("failed") == 1:
        errors.append("bufor_diagnostyczny.failed=1")
    if flight.get("dropped", 0):
        warnings.append(f"Bufor diagnostyczny pominął próbki: {flight['dropped']}.")
    if flight.get("saved", 0) < flight.get("markers", 0):
        missing.append("zapis wszystkich oznaczonych problemów")
    warnings.extend("Zmiana ustawień odrzucona: " + value for value in xr.get("gesture_reload_rejected", []))
    if calibration and calibration.get("accepted") == 0:
        warnings.append("Kalibracja T-pose nie została zaliczona.")
    issues = {key: value for key, value in calibration.items() if key not in {"accepted", "valid_frames", "longest_hold_ms", "holds"} and isinstance(value, int) and value}
    limitations = [
        "Żądania natywnego ruchu nie potwierdzają przemieszczenia postaci ani komfortu w goglach.",
        "Przekazanie zwolnienia ruchu do kolejki nie potwierdza dostarczenia do serwera.",
        "Wysłanie wartości klawiatury i licznik rysowania kierownicy nie potwierdzają wyniku widocznego w grze.",
        "Liczby próbek dłoni i wysłanych obrazów mają różne podstawy; nie wyliczamy z nich procentu śledzenia.",
    ]
    if runtime_kind == "fake":
        limitations.insert(0, "Fałszywy runtime: raport nie jest próbą w goglach.")
    elif runtime_kind == "headset_requested":
        limitations.insert(0, "Wybrano runtime gogli; log nie potwierdza oceny obrazu przez użytkownika.")
    else:
        limitations.insert(0, "Nie ustalono rodzaju runtime.")
    return {
        "schema_version": 1, "capture": str(capture), "runtime_kind": runtime_kind,
        "runtime_name": last("openxr_runtime"), "system_name": last("openxr_system"),
        "exe_sha256": startup.get("exe_sha256", [None])[-1],
        "log_status": "errors" if errors else "incomplete" if missing else "complete",
        "missing_evidence": missing, "errors": errors, "warnings": warnings,
        "closed": closed, "stop_reason": last("openxr_stop_reason"), "locomotion_mode": mode,
        "gesture_config": last("gesture_config"), "totals": totals,
        "performance": performance,
        "locomotion_modes": xr.get("locomotion_mode", []),
        "gesture_changes": xr.get("gesture_reload", []),
        "avatar_profile": xr.get("avatar_profile", []),
        "keypad_prototype": xr.get("keypad_prototype", []),
        "problem_markers": xr.get("problem_marker", []),
        "problem_saved": xr.get("problem_saved", []),
        "readiness": [fields(value) for value in xr.get("openxr_readiness", [])],
        "calibration_events": xr.get("avatar_calibration", []),
        "recenter_events": xr.get("openxr_calibration", []),
        "calibration_blockers": dict(sorted(issues.items(), key=lambda item: item[1], reverse=True)),
        "limitations": limitations,
    }


def markdown(report):
    t = report["totals"]
    status = {"complete": "log zakończony", "errors": "błędy w logu", "incomplete": "niepełne dane"}[report["log_status"]]
    lines = ["# Raport próby VR", "", f"Stan: **{status}**. Runtime: `{report['runtime_kind']}`. Tryb ruchu: `{report['locomotion_mode']}`.", "",
             f"Źródło: `{report['capture']}`", "", "| Obszar | Odczyt z logu |", "|---|---|"]
    for title, key, names in [
        ("Obraz i dłonie", "openxr_totals", ("submitted", "left_hand_samples", "right_hand_samples")),
        ("Ruch", "locomotion_totals", ("packets", "walk", "sprint", "releases", "lifecycle_releases", "faults", "holding", "last_fault")),
        ("Wymachy", "arm_swing_totals", ("frames", "starts", "stops", "invalid_tracking", "fists_open", "outside_zone", "rigid_turns")),
        ("Kalibracja", "avatar_calibration_totals", ("accepted", "holds", "longest_hold_ms", "valid_frames")),
        ("Przyciski dłoni", "hand_buttons_totals", ("accepted", "releases", "faults", "pending", "own_hold")),
        ("Nagranie śledzenia", "hand_recording_totals", ("samples", "limited", "failed")),
        ("Nagranie sterowania", "control_recording_totals", ("limited", "failed")),
        ("Bufor ostatnich minut", "flight_recording_totals", ("samples", "rotations", "dropped", "markers", "saved", "failed")),
        ("Klawiatura pojazdu", "vehicle_keypad_totals", ("opened", "submitted", "cancelled", "rejected", "faults")),
        ("Kierownica", "driver_wheel_totals", ("eyes", "identified", "draws", "patched_shaders", "compile_fallbacks")),
        ("Sterowanie kierownicą", "locomotion_totals", ("steering_packets", "steering_releases", "steering_nonzero_packets", "steering_max_axis")),
    ]:
        values = t.get(key)
        content = "; ".join(f"{name}={values[name]}" for name in names if name in values) if values else "brak danych / opcja wyłączona"
        lines.append(f"| {title} | {content} |")
    if report.get("avatar_profile"):
        lines += ["", "Profil proporcji ciała: " + ", ".join(dict.fromkeys(report["avatar_profile"])) + "."]
    if len(report.get("locomotion_modes", [])) > 1:
        lines += ["", "Tryby ruchu w kolejności: " + " → ".join(report["locomotion_modes"]) + "."]
    if report.get("keypad_prototype") and not t.get("vehicle_keypad_totals"):
        lines += ["", "Klawiatura: prototyp lokalny. Wpisy OK nie potwierdzają wprowadzenia liczby do gry."]
    performance = report.get("performance")
    if performance:
        lines += ["", "| Pomiar | Próbki | Średnia ms | P95 ms | P99 ms | Maks. ms |", "|---|---:|---:|---:|---:|---:|"]
        for label, name in (("Praca klatki CPU", "frame_work_cpu"), ("Odstęp klatek", "frame_interval"), ("Poza avatara CPU", "avatar_pose_cpu"), ("Overlay CPU", "overlay_cpu"), ("Nagrania CPU", "recording_cpu"), ("Render oka CPU", "render_eye_cpu"), ("Zakres klatki GPU", "frame_gpu"), ("Okres runtime", "runtime_period")):
            stats = performance.get(name)
            if stats and stats["count"]:
                values = " | ".join(f"{stats[key]:.3f}" for key in ("mean_ms", "p95_ms", "p99_ms", "max_ms"))
                lines.append(f"| {label} | {stats['count']} | {values} |")
        for name in ("cpu", "gpu"):
            count = performance[name + "_budget_samples"]
            if count:
                over = performance[name + "_over_budget"]
                lines += ["", f"Przekroczenia okresu runtime ({name.upper()}): {over}/{count} ({100 * over / count:.1f}%)."]
        if not performance["gpu_available"]:
            lines += ["", "Brak pomiaru GPU — nie oznacza zerowego kosztu GPU."]
        lines += ["", f"Pomiar GPU: pominięte zakresy {performance['gpu_skipped']}, wyniki oczekujące przy zamknięciu {performance['gpu_pending']}.",
                  "", "Czasy kanałów mogą się nakładać; nie sumować. GPU mierzy odstęp timestampów, nie sam czas zajętości GPU. Okres runtime może różnić się od odświeżania gogli. Percentyle mają rozdzielczość 0,25 ms; dla kubełka od 127,75 ms podawane jest maksimum.",
                  "", "Raport nie obejmuje compositora, reprojekcji SteamVR ani transmisji Steam Link. Powtórzenia obrazu przez mod nie są licznikiem reprojekcji SteamVR. Porównać tę samą próbę z Advanced Frame Timing."]
    if report["calibration_blockers"]:
        lines += ["", "Powody niezaliczenia T-pose: " + ", ".join(f"{k}={v}" for k, v in report["calibration_blockers"].items()) + "."]
    if report["readiness"]:
        lines += ["", "Gotowość XR: " + "; ".join(f"{item.get('reason', '?')} ({item.get('waited_ms', '?')} ms)" for item in report["readiness"]) + "."]
    notes = report["errors"] + report["missing_evidence"] + report["warnings"]
    if notes:
        lines += ["", *[f"- {value}" for value in notes]]
    if t.get("gesture_settings"):
        lines += ["", f"Konfiguracja: `{report['gesture_config']}`", "", "```text", " ".join(f"{k}={v}" for k, v in t["gesture_settings"].items()), "```"]
    lines += ["", *[f"- {value}" for value in report["limitations"]], ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--runtime", choices=("unknown", "fake", "headset"), default="unknown")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    report = summarize(args.capture, args.runtime)
    prefix = report["capture"]
    json_path = args.json or Path(prefix + ".summary.json")
    markdown_path = args.markdown or Path(prefix + ".summary.md")
    sources = {Path(prefix).resolve(), Path(prefix + ".xr.txt").resolve(), Path(prefix + ".performance.json").resolve()}
    if json_path.resolve() in sources or markdown_path.resolve() in sources or json_path.resolve() == markdown_path.resolve():
        parser.error("Report outputs must differ from each other and from source logs")
    for path, content in ((json_path, json.dumps(report, ensure_ascii=False, indent=2) + "\n"), (markdown_path, markdown(report))):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    print(f"Raport VR ({report['log_status']}): {markdown_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
