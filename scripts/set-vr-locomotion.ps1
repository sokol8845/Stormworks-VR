param(
    [Parameter(Mandatory=$true)][int]$GameProcessId,
    [ValidateSet('Reload','Joystick','ArmSwing')][string]$Mode = 'Reload'
)
$ErrorActionPreference = 'Stop'
$targetGame = Get-Process -Id $GameProcessId -ErrorAction Stop
if ($targetGame.ProcessName -ne 'stormworks64') { throw 'Expected the measured Stormworks process.' }
$control = switch ($Mode) { 'Reload' { 'GestureReload' } 'Joystick' { 'ModeJoystick' } 'ArmSwing' { 'ModeArmSwing' } }
$changeEvent = [System.Threading.EventWaitHandle]::OpenExisting("Local\StormworksVR.$control.$GameProcessId")
try {
    if (-not $changeEvent.Set()) { throw 'Could not request gesture change.' }
    Write-Output "Requested $Mode for PID $GameProcessId. Wait for gesture_reload applied_after_release in the log, then open your hands."
} finally { $changeEvent.Dispose() }
