param(
    [string]$GameExe = 'C:\Program Files (x86)\Steam\steamapps\common\Stormworks\stormworks64.exe',
    [string]$Loader,
    [string]$BuildDirectory = 'build\capture\Release',
    [ValidateRange(45,600)][int]$TimeoutSeconds = 180,
    [ValidateRange(10,3600)][int]$PreviewSeconds = 30,
    [switch]$FakeRuntime,
    [switch]$QuietTrace,
    [switch]$NativeEyeResources = $true,
    [ValidateSet('Menu','Gameplay')][string]$Scene = 'Menu',
    [switch]$InteractionProbe,
    [switch]$ManualStart,
    [switch]$ManualStop,
    [switch]$ContinueAutosave,
    [switch]$DryRun,
    [switch]$HeadMask = $true,
    [switch]$HandRay,
    [switch]$InputProbe,
    [switch]$HandButtons,
    [switch]$AvatarPose,
    [switch]$NoHeadLimit,
    [switch]$BodyYaw,
    [switch]$HandLocomotion,
    [switch]$ArmSwingRun,
    [string]$GestureConfig = 'config\gestures.ini',
    [string]$AvatarProfile = 'config\avatar-calibration.ini',
    [switch]$RecordHands,
    [switch]$VrKeypadPrototype,
    [ValidateRange(64,16384)][int]$WindowWidth = 3440,
    [ValidateRange(64,16384)][int]$WindowHeight = 1440,
    [ValidateSet('Fullscreen','Windowed')][string]$WindowMode = 'Fullscreen'
)
$ErrorActionPreference = 'Stop'
if ($Scene -eq 'Gameplay' -and -not $PSBoundParameters.ContainsKey('PreviewSeconds')) { $PreviewSeconds = 300 }
# With -ManualStop the user ends the measurement; the duration is only a safety cap.
if ($ManualStop -and -not $PSBoundParameters.ContainsKey('PreviewSeconds')) { $PreviewSeconds = 1800 }
if ($AvatarPose -and $Scene -ne 'Gameplay') { throw 'AvatarPose requires Scene Gameplay.' }
if (($RecordHands -or $VrKeypadPrototype) -and $Scene -ne 'Gameplay') { throw 'RecordHands and VrKeypadPrototype require Scene Gameplay.' }
if ($ArmSwingRun -and $HandLocomotion) { throw 'Select HandLocomotion (joystick) OR ArmSwingRun for this test.' }
if ($ArmSwingRun) { $HandLocomotion = $true }
if ($BodyYaw -and $Scene -ne 'Gameplay') { throw 'BodyYaw requires Scene Gameplay.' }
if ($HandLocomotion -and $Scene -ne 'Gameplay') { throw 'HandLocomotion requires Scene Gameplay.' }
if ($HandLocomotion) { $BodyYaw = $true }
if ($AvatarPose) { $HandRay = $true; $HeadMask = $true }
if ($HandButtons -and $Scene -ne 'Gameplay') { throw 'HandButtons requires Scene Gameplay.' }
if ($HandButtons -and ($InputProbe -or $InteractionProbe)) { throw 'HandButtons owns the native input hooks; use it without InputProbe or InteractionProbe.' }
if ($HandButtons) { $HandRay = $true }
if ($HandRay -and $Scene -ne 'Gameplay') { throw 'HandRay requires Scene Gameplay.' }
if ($HandRay -and $InteractionProbe) { throw 'HandRay and InteractionProbe use the same native hooks; select one backend.' }
if ($InputProbe -and $Scene -ne 'Gameplay') { throw 'InputProbe requires Scene Gameplay.' }
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'preview-graphics.ps1')
. (Join-Path $PSScriptRoot 'preview-diagnostics.ps1')
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }
$launcher = Join-Path $buildPath 'swvr_trace_launcher.exe'
if ($AvatarPose) { $AvatarProfile = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($AvatarProfile)) { $AvatarProfile } else { Join-Path $projectRoot $AvatarProfile })) }
if ($Scene -eq 'Gameplay') {
    $GestureConfig = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($GestureConfig)) { $GestureConfig } else { Join-Path $projectRoot $GestureConfig }))
    $configChecker = Join-Path $buildPath 'swvr_config_check.exe'
    if (-not (Test-Path -LiteralPath $configChecker -PathType Leaf)) { throw "Missing file: $configChecker" }
    & $configChecker $GestureConfig
    if ($LASTEXITCODE -ne 0) { throw "Invalid gesture configuration: $GestureConfig" }
}
$GameExe = [IO.Path]::GetFullPath($GameExe)
if (-not $DryRun -and (Get-Process stormworks64 -ErrorAction SilentlyContinue)) { throw 'Close the existing Stormworks session before this bounded test.' }
if ($ContinueAutosave) {
    if ($Scene -ne 'Gameplay' -or $FakeRuntime) { throw 'ContinueAutosave requires real Gameplay mode.' }
    $autosaveScene = Join-Path $env:APPDATA 'Stormworks\saves\autosave\scene.xml'
    if (-not (Test-Path -LiteralPath $autosaveScene -PathType Leaf)) { throw "Missing autosave: $autosaveScene" }
}
if ($FakeRuntime) {
    $Loader = Join-Path $buildPath 'swvr_xr_fake.dll'
} elseif (-not $Loader) {
    $steamRoot = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
    $Loader = Join-Path $steamRoot 'steamapps\common\SteamVR\bin\win64\openxr_loader.dll'
}
$Loader = [IO.Path]::GetFullPath($Loader)
foreach ($path in @($GameExe,$launcher,$Loader)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $path" }
    if ($path.Contains('"')) { throw 'A quoted path cannot be passed to the launcher.' }
}
$artifacts = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$kind = if ($FakeRuntime) { 'fake-xr' } else { 'headset-xr' }
$capture = Join-Path $artifacts "$kind-$stamp.tsv"
$stdout = "$capture.launcher.txt"
$stderr = "$capture.launcher.err"
$launcherArguments = @(('"'+$GameExe+'"'),('"'+$capture+'"'))
if ($ContinueAutosave) { $launcherArguments += '--continue' }
if ($DryRun) {
    # Direct native invocation already quotes argv elements; Start-Process
    # above needs literal quoting because it joins ArgumentList into a string.
    $dryArguments = @($GameExe,$capture)
    if ($ContinueAutosave) { $dryArguments += '--continue' }
    & $launcher @dryArguments '--dry-run'
    if ($LASTEXITCODE -ne 0) { throw 'Launcher dry run failed.' }
    Write-Output "Preset: scene=$Scene autosave=$ContinueAutosave manual_start=$ManualStart hands=$HandButtons avatar=$AvatarPose recording=$RecordHands"
    return
}
$variables = @('SWVR_OPENXR_PREVIEW','SWVR_OPENXR_LOADER','SWVR_STEREO_PREVIEW','SWVR_TRACE_EVENTS','SWVR_NATIVE_EYES','SWVR_XR_SCENE','SWVR_XR_SECONDS','SWVR_XR_MANUAL_START','SWVR_INTERACTION_PROBE','SWVR_HEAD_MASK','SWVR_HAND_RAY','SWVR_INPUT_PROBE','SWVR_HAND_BUTTONS','SWVR_AVATAR_POSE','SWVR_HEAD_LIMIT','SWVR_BODY_YAW',
    'SWVR_HAND_LOCOMOTION','SWVR_LOCOMOTION_MODE','SWVR_GESTURE_CONFIG','SWVR_AVATAR_PROFILE','SWVR_PROFILE_STAGING','SWVR_RECORD_HANDS','SWVR_KEYPAD_PROTOTYPE','SWVR_FAKE_GL_IMAGES','SWVR_FAKE_REPORT','VR_LOG_PATH')
$saved = @{}
foreach ($name in $variables) { $saved[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$ownedGame = $null
$launcherProcess = $null
$finished = $false
$graphicsPrepared = $false
$reportedXrDiagnostics = [System.Collections.Generic.HashSet[string]]::new()
$graphicsPath = Join-Path $env:APPDATA 'Stormworks\graphics_config.xml'
try {
    if ([IO.Path]::GetFileName($GameExe) -ieq 'stormworks64.exe') {
        Set-SwvrPreviewGraphics -Path $graphicsPath -Width $WindowWidth -Height $WindowHeight -Mode $WindowMode -BackupPath "$capture.graphics-before.xml"
        $graphicsPrepared = $true
        Write-Output "Prepared startup $WindowWidth x $WindowHeight $WindowMode; graphics recovery marker cleared."
    }
    $env:SWVR_OPENXR_PREVIEW = '1'
    $env:SWVR_OPENXR_LOADER = $Loader
    $env:SWVR_STEREO_PREVIEW = '1'
    $env:SWVR_TRACE_EVENTS = if ($QuietTrace) { '0' } else { '1' }
    $env:SWVR_NATIVE_EYES = if ($NativeEyeResources) { '1' } else { '0' }
    $env:SWVR_XR_SCENE = $Scene.ToLowerInvariant()
    $env:SWVR_XR_SECONDS = [string]$PreviewSeconds
    $env:SWVR_XR_MANUAL_START = if ($ManualStart) { '1' } else { '0' }
    $env:SWVR_HEAD_MASK = if ($HeadMask) { '1' } else { '0' }
    $env:SWVR_HAND_RAY = if ($HandRay) { '1' } else { '0' }
    $env:SWVR_INPUT_PROBE = if ($InputProbe) { '1' } else { '0' }
    $env:SWVR_HAND_BUTTONS = if ($HandButtons) { '1' } else { '0' }
    $env:SWVR_AVATAR_POSE = if ($AvatarPose) { '1' } else { '0' }
    $env:SWVR_HEAD_LIMIT = if ($NoHeadLimit) { '0' } else { '1' }
    $env:SWVR_BODY_YAW = if ($BodyYaw) { '1' } else { '0' }
    $env:SWVR_HAND_LOCOMOTION = if ($HandLocomotion) { '1' } else { '0' }
    $env:SWVR_LOCOMOTION_MODE = if ($ArmSwingRun) { 'arm_swing' } else { 'joystick' }
    $env:SWVR_GESTURE_CONFIG = if ($Scene -eq 'Gameplay') { $GestureConfig } else { '' }
    $env:SWVR_AVATAR_PROFILE = if ($AvatarPose) { $AvatarProfile } else { '' }
    $env:SWVR_PROFILE_STAGING = Join-Path $projectRoot 'tmp'
    $env:SWVR_RECORD_HANDS = if ($RecordHands) { '1' } else { '0' }
    $env:SWVR_KEYPAD_PROTOTYPE = if ($VrKeypadPrototype) { '1' } else { '0' }
    $env:SWVR_INTERACTION_PROBE = if ($InteractionProbe) { '1' } else { '0' }
    $env:VR_LOG_PATH = Join-Path $artifacts 'steamvr-logs'
    if ($FakeRuntime) {
        $env:SWVR_FAKE_GL_IMAGES = '1'
        $env:SWVR_FAKE_REPORT = "$capture.fake.json"
    } else {
        [Environment]::SetEnvironmentVariable('SWVR_FAKE_GL_IMAGES',$null,'Process')
        [Environment]::SetEnvironmentVariable('SWVR_FAKE_REPORT',$null,'Process')
    }
    $testStart = Get-Date
    $startupDeadline = $testStart.AddSeconds($TimeoutSeconds)
    $measurementDeadline = $null
    $launcherProcess = Start-Process -FilePath $launcher -ArgumentList $launcherArguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    Write-Output "Started $kind $Scene experiment. Capture: $capture"
    Write-Output 'The test will close its own Stormworks process after measurements.'
    if ($ManualStart) { Write-Output 'Normal desktop controls remain active. Start VR explicitly after entering the cockpit.' }
    if ($ContinueAutosave) { Write-Output 'Native -continue requests the autosave; VR starts after the first world render unless ManualStart was selected.' }
    if ($ArmSwingRun) { Write-Output 'Two-fist run: with the default settings, close both fists to run and open either hand to stop. No arm swings or preliminary open-hand sequence are required.' }
    if ($ManualStop) { Write-Output "End the measurement with: .\scripts\stop-vr-preview.ps1 -GameProcessId <PID> (safety cap $PreviewSeconds s)." }
    if ($HandRay -and -not $HandButtons) { Write-Output 'Experimental hand pointing uses its own diagnostic backend; it does not emit clicks.' }
    if ($HandButtons) { Write-Output 'Experimental hand buttons are enabled. Completion also waits for native input and owned request cleanup.' }
    if ($HandLocomotion) { Write-Output 'Gesture settings can be reloaded with scripts\set-vr-locomotion.ps1 -GameProcessId <PID> -Mode Reload/Joystick/ArmSwing.' }
    if ($RecordHands) { Write-Output "Hand tracking recording: $capture.hands.tsv (64 MiB cap; optional diagnostic overhead)." }
    if ($VrKeypadPrototype) { Write-Output 'Local keypad prototype enabled. Toggle with scripts\toggle-vr-keypad.ps1; OK records a local result and does not submit it to the game.' }
    if ($AvatarPose) { Write-Output 'Avatar head/upper body and arm IK are enabled for the local first-person survivor. Show both hands during the measurement.' }
    if ($InputProbe) { Write-Output 'The native input probe only records queue, event and UI context; it does not emit actions.' }
    while ((Get-Date) -lt $(if ($measurementDeadline) { $measurementDeadline } else { $startupDeadline })) {
        if (-not $ownedGame -and (Test-Path -LiteralPath $stdout)) {
            $launchText = Get-Content -LiteralPath $stdout -Raw
            if ($launchText -match 'PID=(\d+)') {
                $candidate = Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue
                if ($candidate -and $candidate.Path -eq $GameExe -and $candidate.StartTime -ge $testStart.AddSeconds(-1)) {
                    $parent = Get-CimInstance Win32_Process -Filter "ProcessId=$($candidate.Id)"
                    if ($parent.ParentProcessId -ne $launcherProcess.Id) { throw 'Launcher child ownership mismatch.' }
                    $ownedGame = $candidate
                }
            }
        }
        if (Test-Path -LiteralPath "$capture.xr.txt") {
            $tail = Get-Content -LiteralPath "$capture.xr.txt"
            foreach ($line in $tail) {
                if ($line -notmatch '^#(?:openxr_readiness|stereo_preview_error|openxr_error)\s+') { continue }
                $message = Get-SwvrXrDiagnosticMessage -Line $line
                if ($message -and $reportedXrDiagnostics.Add($line)) {
                    if ($line -match 'reason=ready\b') { Write-Output $message }
                    else { Write-Warning $message }
                }
            }
            if (-not $measurementDeadline -and ($tail -match '^#openxr_state\s+initialized_(gameplay|menu)_experiment_seconds=')) {
                # Navigation/manual start has its own budget. Once XR starts,
                # always leave the requested interval plus shutdown/drain grace.
                $measurementDeadline = (Get-Date).AddSeconds($PreviewSeconds + 15)
            }
            if ($tail -match '^#stereo_preview_error\s+' -or $tail -match '^#openxr_error\s+' -or $tail -match '^#avatar_pose_error\s+') {
                throw "Preview failed; inspect $capture"
            }
            if ($tail -match '^#openxr_state\s+closed$') {
                if (-not ($tail -match '^#openxr_totals\s+submitted=[1-9][0-9]*\b')) {
                    throw "Preview closed without submitting any stereo frame; inspect $capture"
                }
                if ((-not $HandButtons -or ($tail -match '^#hand_buttons_drain\s+complete$')) -and
                    (-not $HandLocomotion -or ($tail -match '^#locomotion_drain\s+complete$'))) {
                    $finished = $true; break
                }
            }
        }
        $launcherProcess.Refresh()
        if ($launcherProcess.HasExited) { throw "Game/launcher exited before completing the preview; inspect $stdout and $stderr" }
        Start-Sleep -Milliseconds 400
    }
    if (-not $finished) { throw "Preview startup or measurement/cleanup timed out; inspect $capture" }
    if (-not ($tail -match '^#openxr_stop_reason\s+(duration_elapsed|user_stop)$')) {
        throw "Preview ended before its full measurement interval; inspect $capture"
    }
    if ($Scene -eq 'Gameplay' -and -not ($tail -match '^#openxr_culling\s+planes_updated=[1-9][0-9]*\b')) {
        throw "Gameplay stereo culling was not observed; inspect $capture"
    }
    if ($HandLocomotion -and (-not ($tail -match '^#locomotion\s+hooks_created$') -or
        -not ($tail -match '^#locomotion_drain\s+complete$') -or
        -not (Test-SwvrLocomotionModeEvidence -Lines $tail -ArmSwingRequested $ArmSwingRun) -or
        -not ($tail -match '^#locomotion_totals\s+.*\bholding=0\b.*\bfaults=0\b'))) {
        throw "Locomotion installation or release validation failed; inspect $capture"
    }
    if ($HandLocomotion -and (Get-SwvrMeasuredLocomotionMode -Lines $tail -ArmSwingRequested $ArmSwingRun) -eq 'arm_swing' -and (-not ($tail -match '^#arm_swing_totals\s+.*\bstarts=[1-9][0-9]*\b') -or
        -not ($tail -match '^#locomotion_totals\s+.*\bsprint=[1-9][0-9]*\b'))) {
        throw "No arm-swing sprint was detected or submitted; inspect arm_swing_totals and locomotion_totals in $capture.xr.txt"
    }
    if ($FakeRuntime) {
        $report = Get-Content -LiteralPath "$capture.fake.json" -Raw | ConvertFrom-Json
        if (-not $report.fake_openxr -or $report.headset_test -or $report.validation_errors -ne 0 -or
            $report.valid_projection_frames -le 0 -or $report.valid_projection_frames -ne $report.projection_frames -or
            $report.invalid_releases -ne 0 -or $report.acquired_images -ne $report.released_images -or
            $report.destroyed_swapchains -ne 2 -or -not $report.first_projection_readback -or
            -not $report.nonblack_rgb[0] -or -not $report.nonblack_rgb[1] -or -not $report.different_eye_pixels) {
            throw "Fake OpenXR integration validation failed; inspect $capture.fake.json"
        }
        if ($report.diagnostic_hand_pixels.Count -ne 2) { throw 'Missing fake hand image evidence.' }
        foreach ($eyeHandPixels in $report.diagnostic_hand_pixels) {
            if ($eyeHandPixels.Count -ne 2 -or $eyeHandPixels[0] -le 0 -or $eyeHandPixels[1] -le 0) {
                throw "Both fake hand colors must appear in each submitted eye; inspect $capture.fake.json"
            }
        }
    }
    if ($NativeEyeResources -and (-not ($tail -match '^#native_eye_resources\s+ready_distinct_resource_and_FBO_names$') -or
        -not ($tail -match '^#native_eye_resources\s+references_released$'))) {
        throw "Independent native resources were not both used and released; inspect $capture"
    }
    if ($graphicsPrepared -and $NativeEyeResources) {
        Assert-SwvrPreviewStartup -Lines $tail -Width $WindowWidth -Height $WindowHeight -Mode $WindowMode
    }
    if ($InputProbe -and (-not ($tail -match '^#input_probe\s+hooks_created$') -or
        -not ($tail -match '^#input_probe_log\s+rearmed_at_vr_start$'))) {
        throw "The requested native input probe was not installed and rearmed; inspect $capture"
    }
    if ($HandButtons) {
        if (-not ($tail -match '^#hand_buttons\s+hooks_created$') -or
            -not ($tail -match '^#button_retirement\s+hooks_created$') -or
            -not ($tail -match '^#hand_buttons_drain\s+complete$')) {
            throw "The hand-button backend was not installed and fully drained; inspect $capture"
        }
        if (-not ($tail -match '^#hand_buttons_totals\s+.*\baccepted=[1-9][0-9]*\b') -or
            -not ($tail -match '^#hand_buttons_totals\s+.*\breleases=[1-9][0-9]*\b') -or
            -not ($tail -match '^#button_retirement_totals\s+.*\b(write|localhost)=[1-9][0-9]*\b')) {
            throw "No complete hand press/release and request retirement was observed; inspect $capture"
        }
    }
    if ($AvatarPose) {
        if (-not ($tail -match '^#avatar_pose\s+configured_survivor_head_arms$') -or
            -not ($tail -match '^#avatar_pose_totals\s+.*\bdraws=[1-9][0-9]*\b') -or
            -not ($tail -match '^#avatar_pose_totals\s+.*\bleft=[1-9][0-9]*\b') -or
            -not ($tail -match '^#avatar_pose_totals\s+.*\bright=[1-9][0-9]*\b')) {
            throw "Avatar head and both tracked arms were not drawn; inspect $capture"
        }
        if (-not ($tail -match '^#avatar_pose_totals\s+.*\bdepth_draws=[1-9][0-9]*\b')) {
            throw "Avatar head and both arms were drawn, but no local depth-only pass was observed; inspect $capture"
        }
    }
    Write-Output "Measurements complete: $capture"
} finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process') }
    if ($ownedGame) {
        $ownedGame.Refresh()
        if (-not $ownedGame.HasExited) {
            # The retained Process object identifies only this launcher's child.
            Stop-Process -InputObject $ownedGame -Force
            $ownedGame.WaitForExit(10000) | Out-Null
            Write-Output "Closed measured Stormworks process $($ownedGame.Id)."
            if ($graphicsPrepared -and $ownedGame.HasExited) {
                Set-SwvrPreviewGraphics -Path $graphicsPath -ClearWriteKeyOnly
                Write-Output 'Cleared graphics recovery marker after the controlled preview close.'
            }
        }
    }
    # Read-only postprocessing also runs after a timeout or failed validation.
    # Report failure must not replace the original exception from the preview.
    try {
        if ((Test-Path -LiteralPath $capture -PathType Leaf) -or (Test-Path -LiteralPath "$capture.xr.txt" -PathType Leaf)) {
            $reportRuntime = if ($FakeRuntime) { 'fake' } else { 'headset' }
            & python (Join-Path $projectRoot 'tools\summarize_preview.py') $capture --runtime $reportRuntime
            if ($LASTEXITCODE -ne 0) { Write-Warning 'Nie udało się utworzyć raportu VR; surowe logi są zachowane.' }
        }
    } catch { Write-Warning "Raport VR: $($_.Exception.Message)" }
}
