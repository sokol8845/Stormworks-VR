param(
    [ValidateSet('Saved','Fists','Joystick','ArmSwing')][string]$Movement = 'Saved',
    [switch]$ManualStart,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
if ($Movement -eq 'Saved') {
    $Movement = 'Fists'
    $settingsFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'config/gestures.ini'
    if (Test-Path -LiteralPath $settingsFile) {
        if ((Get-Item -LiteralPath $settingsFile).Length -gt 16384) { throw 'Gesture configuration too large.' }
        $section = ''
        foreach ($line in Get-Content -LiteralPath $settingsFile) {
            if ($line -match '^\s*\[([^]]+)\]\s*$') { $section = $Matches[1].Trim() }
            elseif ($section -eq 'input' -and $line -match '^\s*movement_mode\s*=\s*(joystick|fists)\s*$') { $Movement = if ($Matches[1] -eq 'joystick') { 'Joystick' } else { 'Fists' } }
        }
    }
}
$options = @{
    Scene = 'Gameplay'; ContinueAutosave = $true; AvatarPose = $true
    HandButtons = $true; BodyYaw = $true; RecordHands = $true
    VrKeypadPrototype = $true; ManualStop = $true; QuietTrace = $true
    TimeoutSeconds = 600; PreviewSeconds = 1800; WindowMode = 'Windowed'
    ManualStart = $ManualStart; DryRun = $DryRun
}
if ($Movement -ne 'Joystick') { $options.ArmSwingRun = $true }
else { $options.HandLocomotion = $true }
& (Join-Path $PSScriptRoot 'run-game-preview.ps1') @options
