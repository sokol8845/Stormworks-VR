"""Discover Steam installation and run bounded, read-only capability checks."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import winreg
from inspect_game import inspect


def registry(root, path, name):
    try:
        with winreg.OpenKey(root, path, access=winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            return winreg.QueryValueEx(key, name)[0]
    except OSError:
        return None


def libraries():
    steam = registry(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath')
    if not steam:
        return []
    roots = [Path(steam)]
    config = roots[0] / 'steamapps/libraryfolders.vdf'
    if config.exists():
        roots.extend(Path(p.replace('\\\\', '\\')) for p in re.findall(
            r'"path"\s+"([^"]+)"', config.read_text(encoding='utf-8')))
    return list(dict.fromkeys(roots))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game-dir', type=Path)
    parser.add_argument('--loader', type=Path)
    parser.add_argument('--skip-xr', action='store_true')
    parser.add_argument('--timeout', type=int, default=30)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    output = project / 'artifacts'
    output.mkdir(exist_ok=True)
    roots = libraries()
    runtime = registry(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Khronos\OpenXR\1', 'ActiveRuntime')
    report = {'cpu': registry(winreg.HKEY_LOCAL_MACHINE,
              r'HARDWARE\DESCRIPTION\System\CentralProcessor\0', 'ProcessorNameString'),
              'registered_runtime': runtime, 'runtime_override': os.environ.get('XR_RUNTIME_JSON'),
              'game': None, 'openxr': {'status': 'skipped'}}
    games = [args.game_dir] if args.game_dir else [r / 'steamapps/common/Stormworks' for r in roots]
    game = next((p for p in games if (p / 'stormworks64.exe').exists()), None)
    if game:
        report['game'] = inspect(game)
        (output / 'game-report.json').write_text(json.dumps(report['game'], indent=2), encoding='utf-8')
    loaders = [args.loader] if args.loader else [r / 'steamapps/common/SteamVR/bin/win64/openxr_loader.dll' for r in roots]
    loader = next((p for p in loaders if p.exists()), None)
    xr_report = output / 'openxr-report.json'
    if not args.skip_xr:
        # Avoid showing stale results if this worker times out or crashes.
        xr_report.write_text('{"status":"not_run"}', encoding='utf-8')
        if loader:
            try:
                completed = subprocess.run([sys.executable, str(project / 'tools/openxr_probe.py'),
                    '--loader', str(loader), '--output', str(xr_report)],
                    capture_output=True, text=True, timeout=max(1, args.timeout))
                report['openxr'] = json.loads(xr_report.read_text(encoding='utf-8'))
                report['openxr']['worker_exit_code'] = completed.returncode
                (output / 'openxr-stderr.txt').write_text(completed.stderr, encoding='utf-8')
            except subprocess.TimeoutExpired:
                report['openxr'] = {'status': 'timeout', 'timeout_seconds': args.timeout}
        else:
            report['openxr'] = {'status': 'loader_not_found', 'hint': 'Provide --loader PATH'}
        xr_report.write_text(json.dumps(report['openxr'], indent=2), encoding='utf-8')
    destination = output / 'diagnostics.json'
    destination.write_text(json.dumps(report, indent=2), encoding='utf-8')
    xr = report['openxr']
    print(f'Game: {game or "not found"}')
    print(f'Registered OpenXR runtime: {runtime}')
    print(f'OpenXR result: {xr.get("status")}; hand extension: {xr.get("hand_extension", "unknown")}')
    if xr.get('error'):
        print(xr['error'])
    print(f'Report: {destination}')
    return 1 if not game or xr['status'] in ('error','timeout','loader_not_found') else 0


if __name__ == '__main__':
    sys.exit(main())
