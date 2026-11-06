"""Exercise native launcher preflight only; every accepted command is dry-run."""
from pathlib import Path
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/capture/Release/swvr_trace_launcher.exe'
GAME = Path('C:/Program Files (x86)/Steam/steamapps/common/Stormworks/stormworks64.exe')
pytestmark = pytest.mark.skipif(not EXE.is_file() or not GAME.is_file(), reason='Windows launcher/game image required; neither game nor XR is started')

def preflight(log, *options):
    return subprocess.run([str(EXE), str(GAME), str(log), *options], capture_output=True, text=True)

@pytest.mark.parametrize('resume', [False, True])
def test_native_command_dry_run_never_creates_capture(tmp_path, resume):
    log = tmp_path / 'uncreated' / 'capture.tsv'
    result = preflight(log, *(['--continue'] if resume else []), '--dry-run')
    assert result.returncode == 0, result.stderr
    assert 'no process created' in result.stdout
    assert result.stdout.strip().endswith('-continue' if resume else 'stormworks64.exe"')
    assert not log.parent.exists()

@pytest.mark.parametrize('options', [('--unknown', '--dry-run'), ('--continue', '--continue')])
def test_unknown_or_duplicate_options_rejected_before_process_creation(tmp_path, options):
    result = preflight(tmp_path / 'never.tsv', *options)
    assert result.returncode != 0 and 'Unknown or duplicate' in result.stderr

def test_existing_capture_preserved_in_dry_run(tmp_path):
    log = tmp_path / 'capture.tsv'
    log.write_text('sentinel', encoding='ascii')
    assert preflight(log, '--continue', '--dry-run').returncode != 0
    assert log.read_text() == 'sentinel'
