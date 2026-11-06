param([Parameter(Mandatory=$true)][int]$GameProcessId)
# Ends a running VR measurement on the user's request (the supervisor then
# validates the logs and closes its own Stormworks process).
$ErrorActionPreference = 'Stop'
$targetGame = Get-Process -Id $GameProcessId -ErrorAction Stop
if ($targetGame.ProcessName -ne 'stormworks64') { throw 'Expected the measured Stormworks process.' }
$stopEvent = [System.Threading.EventWaitHandle]::OpenExisting("Local\StormworksVR.Stop.$GameProcessId")
try {
    if (-not $stopEvent.Set()) { throw 'Could not signal VR preview stop.' }
    Write-Output "Requested VR measurement stop for Stormworks PID $GameProcessId."
} finally { $stopEvent.Dispose() }
