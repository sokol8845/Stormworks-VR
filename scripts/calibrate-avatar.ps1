param([Parameter(Mandatory=$true)][int]$GameProcessId)
$ErrorActionPreference = 'Stop'
$targetGame = Get-Process -Id $GameProcessId -ErrorAction Stop
if ($targetGame.ProcessName -ne 'stormworks64') { throw 'Expected the measured Stormworks process.' }
$calibrationEvent = [System.Threading.EventWaitHandle]::OpenExisting("Local\StormworksVR.AvatarCalibrate.$GameProcessId")
try {
    if (-not $calibrationEvent.Set()) { throw 'Could not request avatar calibration.' }
    Write-Output "Requested T-pose calibration for Stormworks PID $GameProcessId. Check avatar_calibration in the capture for acceptance."
} finally { $calibrationEvent.Dispose() }
