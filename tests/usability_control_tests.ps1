# Exercise only test-owned named events; no game process is launched or signalled.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testProcessId = [int]::MaxValue - $PID
function Get-Process {
    param([object]$Id)
    if ($Id -is [string]) { return $null }
    return [pscustomobject]@{ ProcessName = $(if ($Id -eq $testProcessId) { 'stormworks64' } else { 'other' }) }
}
function Assert-True([bool]$ok,[string]$why) { if (-not $ok) { throw $why } }
$scripts = @('run-game-preview.ps1','start-vr-test.ps1','set-vr-locomotion.ps1','reset-avatar-profile.ps1','toggle-vr-keypad.ps1','view-hand-recording.ps1')
foreach ($name in $scripts) {
    $tokens = $null; $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot "scripts\$name"),[ref]$tokens,[ref]$errors)
    Assert-True ($errors.Count -eq 0) "Syntax error: $name"
}
$events = @{}
try {
    foreach ($control in @('GestureReload','ModeJoystick','ModeArmSwing','AvatarProfileClear','Keypad')) {
        $created = $false
        $events[$control] = [System.Threading.EventWaitHandle]::new($false,[System.Threading.EventResetMode]::AutoReset,"Local\StormworksVR.$control.$testProcessId",[ref]$created)
        Assert-True $created 'Test would reuse an event it does not own.'
    }
    foreach ($mode in @('Reload','Joystick','ArmSwing')) {
        & (Join-Path $projectRoot 'scripts\set-vr-locomotion.ps1') -GameProcessId $testProcessId -Mode $mode | Out-Null
        $expected = switch ($mode) { 'Reload' { 'GestureReload' } 'Joystick' { 'ModeJoystick' } 'ArmSwing' { 'ModeArmSwing' } }
        foreach ($control in @('GestureReload','ModeJoystick','ModeArmSwing')) { Assert-True ($events[$control].WaitOne(0) -eq ($control -eq $expected)) 'Wrong mode event signalled.' }
    }
    & (Join-Path $projectRoot 'scripts\toggle-vr-keypad.ps1') -GameProcessId $testProcessId | Out-Null
    Assert-True ($events['Keypad'].WaitOne(0)) 'Keypad toggle was not signalled.'
    & (Join-Path $projectRoot 'scripts\reset-avatar-profile.ps1') -GameProcessId $testProcessId | Out-Null
    Assert-True ($events['AvatarProfileClear'].WaitOne(0)) 'Profile reset was not signalled.'
    foreach ($script in @('set-vr-locomotion.ps1','toggle-vr-keypad.ps1','reset-avatar-profile.ps1')) {
        $rejected = $false
        try { & (Join-Path $projectRoot "scripts\$script") -GameProcessId 1 | Out-Null } catch { $rejected = $true }
        Assert-True $rejected 'Wrong process was accepted.'
    }
} finally { foreach ($event in $events.Values) { $event.Dispose() } }
$profilePath = Join-Path $projectRoot "tmp\profile-reset-test-$PID.ini"
try {
    Set-Content -LiteralPath $profilePath -Value 'test-owned'
    & (Join-Path $projectRoot 'scripts\reset-avatar-profile.ps1') -Profile $profilePath | Out-Null
    Assert-True (-not (Test-Path -LiteralPath $profilePath)) 'Offline profile deletion failed.'
} finally { if (Test-Path -LiteralPath $profilePath) { Remove-Item -LiteralPath $profilePath } }
# Run the real preset in an isolated directory with a launcher that only returns its options.
$presetRoot = Join-Path $projectRoot ('tmp\preset-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $presetRoot 'scripts'),(Join-Path $presetRoot 'config') | Out-Null
$preset = Join-Path $presetRoot 'scripts\start-vr-test.ps1'
$settings = Join-Path $presetRoot 'config\gestures.ini'
Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts\start-vr-test.ps1') -Destination $preset
Set-Content -LiteralPath (Join-Path $presetRoot 'scripts\run-game-preview.ps1') -Value @'
param($Scene,[switch]$ContinueAutosave,[switch]$AvatarPose,[switch]$HandButtons,[switch]$BodyYaw,[switch]$RecordHands,[switch]$VrKeypadPrototype,[switch]$ManualStop,[switch]$QuietTrace,$TimeoutSeconds,$PreviewSeconds,$WindowMode,[switch]$ManualStart,[switch]$DryRun,[switch]$ArmSwingRun,[switch]$HandLocomotion)
return $PSBoundParameters
'@
$options = & $preset -DryRun
Assert-True ($options.ArmSwingRun -and $options.DryRun) 'Missing preferences did not default to fists.'
Set-Content -LiteralPath $settings -Value "[ input ]`nmovement_mode=joystick"
$options = & $preset -DryRun
Assert-True ($options.HandLocomotion -and -not $options.ArmSwingRun) 'Saved joystick mode was ignored.'
$options = & $preset -Movement Fists -DryRun
Assert-True ($options.ArmSwingRun -and -not $options.HandLocomotion) 'Explicit fists did not override saved joystick.'
Set-Content -LiteralPath $settings -Value "[input]`nmovement_mode=fists"
$options = & $preset -DryRun
Assert-True ($options.ArmSwingRun -and -not $options.HandLocomotion) 'Saved fists mode was ignored.'
Write-Output 'PASS usability controls: syntax, owned events, mode mapping, process guards, offline profile reset and saved launch preferences.'
