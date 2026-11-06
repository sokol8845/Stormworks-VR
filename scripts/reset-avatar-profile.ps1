param(
    [int]$GameProcessId,
    [string]$Profile = 'config\avatar-calibration.ini'
)
$ErrorActionPreference = 'Stop'
if ($PSBoundParameters.ContainsKey('GameProcessId') -and $GameProcessId -le 0) { throw 'GameProcessId must be positive; omit it only for offline profile removal.' }
if ($GameProcessId) {
    $targetGame = Get-Process -Id $GameProcessId -ErrorAction Stop
    if ($targetGame.ProcessName -ne 'stormworks64') { throw 'Expected the measured Stormworks process.' }
    $clearEvent = [System.Threading.EventWaitHandle]::OpenExisting("Local\StormworksVR.AvatarProfileClear.$GameProcessId")
    try {
        if (-not $clearEvent.Set()) { throw 'Could not request avatar profile reset.' }
        Write-Output 'Requested removal of the active saved profile; check avatar_profile cleared in the VR log.'
    } finally { $clearEvent.Dispose() }
} else {
    if (Get-Process stormworks64 -ErrorAction SilentlyContinue) { throw 'Use -GameProcessId to reset the loaded profile while Stormworks is running.' }
    $projectRoot = Split-Path $PSScriptRoot -Parent
    $profilePath = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Profile)) { $Profile } else { Join-Path $projectRoot $Profile }))
    if (Test-Path -LiteralPath $profilePath -PathType Leaf) { Remove-Item -LiteralPath $profilePath }
    Write-Output "Saved avatar profile cleared: $profilePath"
}
