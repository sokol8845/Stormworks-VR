param([Parameter(Mandatory=$true)][int]$GameProcessId)
$ErrorActionPreference = 'Stop'
$targetGame = Get-Process -Id $GameProcessId -ErrorAction Stop
if ($targetGame.ProcessName -ne 'stormworks64') { throw 'Expected the measured Stormworks process.' }
$keypadEvent = [System.Threading.EventWaitHandle]::OpenExisting("Local\StormworksVR.Keypad.$GameProcessId")
try {
    if (-not $keypadEvent.Set()) { throw 'Could not toggle the keypad prototype.' }
    Write-Output 'Requested local keypad prototype toggle. Its OK result is only logged; no game keyboard submission.'
} finally { $keypadEvent.Dispose() }
