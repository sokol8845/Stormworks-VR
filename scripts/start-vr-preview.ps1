param([Parameter(Mandatory=$true)][int]$GameProcessId)
$ErrorActionPreference = 'Stop'
$targetGame = Get-Process -Id $GameProcessId -ErrorAction Stop
if ($targetGame.ProcessName -ne 'stormworks64') { throw 'Expected the measured Stormworks process.' }
$startEvent = [System.Threading.EventWaitHandle]::OpenExisting("Local\StormworksVR.Start.$GameProcessId")
try {
    if (-not $startEvent.Set()) { throw 'Could not signal VR preview start.' }
    Write-Output "Requested VR start for Stormworks PID $GameProcessId."
} finally { $startEvent.Dispose() }
