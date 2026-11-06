param([int]$GameProcessId)
$ErrorActionPreference = 'Stop'
if (-not $GameProcessId) {
    $games = @(Get-Process stormworks64 -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Expected one Stormworks process; provide -GameProcessId explicitly.' }
    $GameProcessId = $games[0].Id
}
$game = Get-Process -Id $GameProcessId
if ($game.ProcessName -ne 'stormworks64') { throw 'Selected process is not Stormworks.' }
$name = "Local\StormworksVR.Capture.$GameProcessId"
try {
    $event = [System.Threading.EventWaitHandle]::OpenExisting($name)
} catch {
    throw 'This game has no capture control. Start it with the updated swvr_trace_launcher first.'
}
try {
    if (-not $event.Set()) { throw 'Capture request failed.' }
    Write-Output "Capture requested for PID $GameProcessId. It starts at the next frame and appends to the existing trace."
} finally { $event.Dispose() }
