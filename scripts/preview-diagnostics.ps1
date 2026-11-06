# Pure log-to-message translation; safe to source in offline tests.
function Get-SwvrMeasuredLocomotionMode {
    param([string[]]$Lines,[bool]$ArmSwingRequested)
    $modes = @($Lines | Where-Object { $_ -match '^#locomotion_mode\s+(joystick|arm_swing)$' })
    if ($modes.Count) { return (($modes[-1] -split '\s+',2)[1]) }
    return $(if ($ArmSwingRequested) { 'arm_swing' } else { 'joystick' })
}
function Test-SwvrLocomotionModeEvidence {
    param([string[]]$Lines,[bool]$ArmSwingRequested)
    if (-not $ArmSwingRequested -or ($Lines -match '^#locomotion_mode\s+arm_swing$')) { return $true }
    # A completed switch may leave only the final mode in the supervisor tail.
    return [bool](($Lines -match '^#gesture_reload\s+applied_after_release$') -and ($Lines -match '^#locomotion_mode\s+joystick$'))
}
function Get-SwvrXrDiagnosticMessage {
    param([string]$Line)
    if ($Line -match '^#openxr_readiness\s+reason=(session|focus|tracking|render|ready) waited_ms=([0-9]+)$') {
        $seconds = [math]::Floor([double]$Matches[2] / 1000)
        switch ($Matches[1]) {
            'session' { return "VR czeka na uruchomienie sesji OpenXR ($seconds s). Sprawdź SteamVR i połączenie Steam Link w goglach." }
            'focus' { return "VR czeka na fokus OpenXR ($seconds s). Załóż gogle, wróć do Steam Link i zamknij nakładkę SteamVR/Meta." }
            'tracking' { return "VR czeka na śledzenie pozycji i obrotu głowy ($seconds s). Sprawdź śledzenie gogli i oświetlenie." }
            'render' { return "OpenXR nie udostępnia klatki do renderowania ($seconds s). Sprawdź widoczność aplikacji w SteamVR." }
            'ready' { return 'OpenXR jest ponownie gotowy: sesja, fokus i śledzenie głowy są dostępne.' }
        }
    }
    if ($Line -match '^#(?:stereo_preview_error|openxr_error)\s+.*xrCreateInstance: XrResult -2(?:\D|$)') {
        return 'Nie udało się utworzyć instancji OpenXR (błąd runtime -2). Sprawdź działanie SteamVR, połączenie Steam Link i aktywny runtime OpenXR; sam kod nie wskazuje jednej przyczyny.'
    }
    return $null
}
