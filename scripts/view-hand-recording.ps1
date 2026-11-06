param(
    [Parameter(Mandatory=$true)][string]$Recording,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$BuildDirectory = 'build\capture\Release',
    [string]$GestureConfig,
    [ValidateSet('Recorded','Joystick','ArmSwing')][string]$Mode = 'Recorded'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
function Full-ProjectPath([string]$value) { return [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($value)) { $value } else { Join-Path $projectRoot $value })) }
$Recording = Full-ProjectPath $Recording
$Output = Full-ProjectPath $Output
if (Test-Path -LiteralPath $Output) { throw "Output exists: $Output" }
$replay = Join-Path (Full-ProjectPath $BuildDirectory) 'swvr_replay_hands.exe'
if (-not (Test-Path -LiteralPath $replay -PathType Leaf)) { throw "Missing replay executable: $replay" }
$temporary = Join-Path $projectRoot ("tmp\hand-viewer-$PID-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
$json = Join-Path $temporary 'replay.json'
$replayArguments = @($Recording,(Join-Path $temporary 'replay.tsv'),'--json',$json)
if ($GestureConfig) { $replayArguments += @('--config',(Full-ProjectPath $GestureConfig)) }
if ($Mode -ne 'Recorded') { $replayArguments += @('--mode',$(if ($Mode -eq 'ArmSwing') { 'arm_swing' } else { 'joystick' })) }
& $replay @replayArguments
if ($LASTEXITCODE -ne 0) {
    if (-not (Test-Path -LiteralPath $json -PathType Leaf)) { throw "Replay failed; diagnostics retained in $temporary" }
    Write-Warning "Replay contains differences; the viewer will show them. Diagnostics: $temporary"
}
& python (Join-Path $projectRoot 'tools\view_hand_replay.py') $json $Output
if ($LASTEXITCODE -ne 0) { throw "Viewer export failed; diagnostics retained in $temporary" }
Write-Output "Open this local file in your browser: $Output"
$controls = $Recording -replace '\.hands\.tsv$', '.controls.tsv'
if ($controls -ne $Recording -and (Test-Path -LiteralPath $controls -PathType Leaf)) {
    $controlReplay = Join-Path (Full-ProjectPath $BuildDirectory) 'swvr_replay_controls.exe'
    if (-not (Test-Path -LiteralPath $controlReplay -PathType Leaf)) { throw "Missing control replay: $controlReplay" }
    $controlReport = Join-Path $temporary 'controls-replay.tsv'
    & $controlReplay $controls $controlReport
    if ($LASTEXITCODE -eq 2) { throw "Control recording is incomplete or malformed: $controls" }
    if ($LASTEXITCODE -ne 0) { Write-Warning "Control replay differs from the recorded results: $controlReport" }
    Write-Output "Turn/menu replay report: $controlReport"
}
