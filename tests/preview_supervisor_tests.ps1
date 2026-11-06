# Exercise the actual pure supervision blocks without executing the launcher.
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts\run-game-preview.ps1'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts\preview-diagnostics.ps1')
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) { throw 'Preview script has syntax errors.' }
function Assert-True([bool]$value,[string]$message) { if (-not $value) { throw $message } }
function Assert-Throws([scriptblock]$body,[string]$message) {
    $caught = $false
    try { . $body } catch { $caught = $true }
    Assert-True $caught $message
}
function Pure-Block([string]$source) {
    if ($source -match '(?i)Start-Process|Stop-Process|Get-Process|Get-CimInstance|SetEnvironmentVariable') {
        throw 'Test extraction would execute a process or mutate the environment.'
    }
    return [scriptblock]::Create($source)
}
$loop = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.WhileStatementAst] -and
    $node.Condition.Extent.Text.Contains('$measurementDeadline')
},$true))
Assert-True ($loop.Count -eq 1) 'Expected one bounded measurement supervisor loop.'
$condition = Pure-Block $loop[0].Condition.Extent.Text
$initialization = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text.Contains('initialized_(gameplay|menu)_experiment_seconds=')
},$true))
$closed = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text.Contains('^#openxr_state\s+closed$')
},$true))
$validation = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -eq '$HandButtons' -and
    $node.Extent.Text.Contains('^#hand_buttons_totals')
},$true))
Assert-True ($initialization.Count -eq 1 -and $closed.Count -eq 1 -and $validation.Count -eq 1) 'Expected exact phase/drain/validation blocks.'
$initializePhase = Pure-Block $initialization[0].Extent.Text
$closedPhase = Pure-Block $closed[0].Extent.Text
$validateHands = Pure-Block $validation[0].Extent.Text

# Get-Date is the only command in the extracted deadline logic. Use one fixed
# clock to prove that late manual start cannot consume the measurement budget.
$epoch = [datetime]'2040-01-01T00:00:00Z'
$script:fakeNow = $epoch
function Get-Date { return $script:fakeNow }
$startupDeadline = $epoch.AddSeconds(600)
$measurementDeadline = $null
$PreviewSeconds = 120
$tail = @()
. $initializePhase
Assert-True ($null -eq $measurementDeadline) 'Empty startup metadata started measurement.'
$script:fakeNow = $epoch.AddSeconds(599)
Assert-True ([bool](. $condition)) 'Startup ended early.'
$tail = @("#openxr_state`tinitialized_gameplay_experiment_seconds=120")
. $initializePhase
$expectedDeadline = $script:fakeNow.AddSeconds(135)
Assert-True ($measurementDeadline -eq $expectedDeadline) 'Late start did not reserve full measurement and cleanup grace.'
$script:fakeNow = $epoch.AddSeconds(610)
Assert-True ([bool](. $condition)) 'Overall startup deadline cut off active measurement.'
. $initializePhase
Assert-True ($measurementDeadline -eq $expectedDeadline) 'Repeated metadata extended the measurement indefinitely.'
$script:fakeNow = $expectedDeadline
Assert-True (-not [bool](. $condition)) 'Measurement timeout is not bounded.'

$capture = '<offline fixture>'
$HandButtons = $true
$HandLocomotion = $false
$ArmSwingRun = $false
$baseClosed = @("#openxr_state`tclosed", "#openxr_totals`tsubmitted=10")
$tail = $baseClosed + @('#hand_buttons_drain' + "`tpending")
$finished = $false
while ($true) { . $closedPhase; break }
Assert-True (-not $finished) 'XR close reported completion while own input remained pending.'
$tail = $baseClosed + @('#hand_buttons_drain' + "`tcomplete")
while ($true) { . $closedPhase; break }
Assert-True $finished 'Completed hand cleanup did not finish supervision.'
$HandButtons = $false
$tail = $baseClosed
$finished = $false
while ($true) { . $closedPhase; break }
Assert-True $finished 'Legacy preview unexpectedly requires hand cleanup.'

$HandLocomotion = $true
$tail = $baseClosed + @("#locomotion_drain`tpending")
$finished = $false
while ($true) { . $closedPhase; break }
Assert-True (-not $finished) 'Locomotion completed with a held movement request.'
$tail = $baseClosed + @("#locomotion_drain`tcomplete")
while ($true) { . $closedPhase; break }
Assert-True $finished 'Completed locomotion did not finish supervision.'
$HandLocomotion = $false

$HandButtons = $true
$successful = @(
    "#hand_buttons`thooks_created",
    "#button_retirement`thooks_created",
    "#hand_buttons_drain`tcomplete",
    "#hand_buttons_totals`tqueued=1 accepted=1 rejected=0 releases=1 faults=0 pending=0 own_hold=0 blocked=0",
    "#button_retirement_totals`tregistered=1 write=1 localhost=0 retained=0 quarantined=0"
)
$tail = $successful
. $validateHands
$tail = $successful -replace 'write=1 localhost=0','write=0 localhost=1'
. $validateHands
foreach ($replace in @(
    @('accepted=1','accepted=0'),
    @('releases=1','releases=0'),
    @('write=1 localhost=0','write=0 localhost=0'),
    @('drain\tcomplete',"drain`tpending"),
    @('hand_buttons\thooks_created',"hand_buttons`tdisabled")
)) {
    $tail = $successful -replace $replace[0],$replace[1]
    Assert-Throws { . $validateHands } 'Incomplete hand interaction passed measurement validation.'
}
$avatarValidation = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -eq '$AvatarPose' -and
    $node.Extent.Text.Contains('^#avatar_pose_totals')
},$true))
Assert-True ($avatarValidation.Count -eq 1) 'Expected one avatar validation block.'
$validateAvatar = Pure-Block $avatarValidation[0].Extent.Text
$AvatarPose = $true
$tail = @("#avatar_pose`tconfigured_survivor_head_arms", "#avatar_pose_totals`tcandidates=12 draws=10 left=8 right=7 depth_draws=5 rejected=0")
. $validateAvatar
foreach ($field in @('draws=10','left=8','right=7','depth_draws=5')) {
    $goodAvatar = $tail
    $tail = $tail -replace $field,($field.Split('=')[0]+'=0')
    Assert-Throws { . $validateAvatar } 'Avatar success without head and both arm draws.'
    $tail = $goodAvatar
}
Write-Output 'Preview supervision passed: independent deadlines, input cleanup and required avatar head/both hands (no launch).'

# Translate actual log lines without launching a game, loading OpenXR or
# stealing focus. A generic runtime error must not claim a specific cause.
foreach ($reason in @('session','focus','tracking','render','ready')) {
    Assert-True ([bool](Get-SwvrXrDiagnosticMessage "#openxr_readiness`treason=$reason waited_ms=5000")) "Missing XR diagnostic: $reason"
}
Assert-True (-not (Get-SwvrXrDiagnosticMessage '#unrelated reason=focus waited_ms=5000')) 'Unrelated line produced warning.'
Assert-True (-not (Get-SwvrXrDiagnosticMessage "#openxr_readiness`treason=unknown waited_ms=5000")) 'Unknown reason produced warning.'
Assert-True (-not (Get-SwvrXrDiagnosticMessage "#openxr_readiness`treason=focus waited_ms=bad")) 'Invalid clock produced warning.'
$hint = Get-SwvrXrDiagnosticMessage "#stereo_preview_error`txrCreateInstance: XrResult -2"
Assert-True ($hint -match 'SteamVR' -and $hint -match 'nie wskazuje jednej przyczyny') 'Runtime error lacks cautious troubleshooting hint.'
Assert-True (-not (Get-SwvrXrDiagnosticMessage "#openxr_error`txrCreateInstance: XrResult -20")) 'Different runtime error matched -2.'

$diagnostics = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.ForEachStatementAst] -and
    $node.Extent.Text.Contains('Get-SwvrXrDiagnosticMessage -Line $line')
},$true))
Assert-True ($diagnostics.Count -eq 1) 'Expected live diagnostic forwarding block.'
$forwardDiagnostics = Pure-Block $diagnostics[0].Extent.Text
$script:diagnosticWarnings = @()
function Write-Warning { param([string]$Message) $script:diagnosticWarnings += $Message }
$reportedXrDiagnostics = [System.Collections.Generic.HashSet[string]]::new()
$tail = @("#openxr_readiness`treason=focus waited_ms=5000")
. $forwardDiagnostics
. $forwardDiagnostics
Assert-True ($script:diagnosticWarnings.Count -eq 1) 'Polling the same log spammed warnings.'
$tail += "#openxr_readiness`treason=focus waited_ms=35000"
. $forwardDiagnostics
Assert-True ($script:diagnosticWarnings.Count -eq 2) 'New periodic warning was suppressed.'
Write-Output 'XR diagnostics passed: causes, runtime hints and deduplicated console warnings (no launch).'

$locomotionValidation = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Extent.Text.Contains('Locomotion installation or release validation failed')
},$true))
Assert-True ($locomotionValidation.Count -eq 1) 'Expected locomotion validation block.'
$validateLocomotion = Pure-Block $locomotionValidation[0].Extent.Text
$HandLocomotion = $true
$tail = @("#locomotion`thooks_created", "#locomotion_drain`tcomplete", "#locomotion_totals`tpackets=12 walk=4 sprint=7 releases=1 holding=0 faults=0")
. $validateLocomotion
foreach ($change in @(@('holding=0','holding=1'),@('faults=0','faults=1'),@('drain\tcomplete',"drain`tpending"))) {
    $good = $tail
    $tail = $tail -replace $change[0],$change[1]
    Assert-Throws { . $validateLocomotion } 'Incomplete locomotion shutdown passed.'
    $tail = $good
}
$ArmSwingRun = $true
$tail += "#locomotion_mode`tarm_swing"
. $validateLocomotion
$tail = $tail -replace 'arm_swing','joystick'
Assert-Throws { . $validateLocomotion } 'Arm-swing test accepted the joystick backend.'

$selection = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Extent.Text.Contains('Select HandLocomotion (joystick) OR ArmSwingRun')
},$true))
$normalize = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -eq '$ArmSwingRun' -and
    $node.Extent.Text.Contains('$HandLocomotion = $true')
},$true))
$followBody = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -eq '$HandLocomotion' -and
    $node.Extent.Text.Contains('$BodyYaw = $true')
},$true))
Assert-True ($selection.Count -eq 1 -and $normalize.Count -eq 1 -and $followBody.Count -eq 1) 'Missing arm-swing selection or backend normalization.'
$selectMode = Pure-Block $selection[0].Extent.Text
$normalizeMode = Pure-Block $normalize[0].Extent.Text
$normalizeBody = Pure-Block $followBody[0].Extent.Text
$HandLocomotion = $false; $ArmSwingRun = $true; $BodyYaw = $false
. $selectMode
. $normalizeMode
. $normalizeBody
Assert-True ($HandLocomotion -and $BodyYaw) 'Arm-swing did not enable the movement backend and BodyYaw.'
Assert-Throws { . $selectMode } 'Both alternative movement modes were allowed together.'
Write-Output 'Arm-swing mode selection and supervisor validation passed (no launch).'
$observedSwing = @($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Extent.Text.Contains('No arm-swing sprint was detected')
},$true))
Assert-True ($observedSwing.Count -eq 1) 'Expected observed arm-swing validation.'
$validateObservedSwing = Pure-Block $observedSwing[0].Extent.Text
$ArmSwingRun = $true
$tail = @("#arm_swing_totals`tframes=100 half_strokes=4 starts=1 stops=1", "#locomotion_totals`tpackets=4 sprint=3 releases=1 holding=0 faults=0")
. $validateObservedSwing
$good = $tail
$tail = $tail -replace 'starts=1','starts=0'
Assert-Throws { . $validateObservedSwing } 'A trial without an observed sprint passed.'
$tail = $good -replace 'sprint=3','sprint=0'
Assert-Throws { . $validateObservedSwing } 'A recognized gesture without a native sprint request passed.'
$HandLocomotion = $true; $ArmSwingRun = $true
$tail = @("#locomotion`thooks_created", "#locomotion_mode`tjoystick", "#gesture_reload`tapplied_after_release", "#locomotion_drain`tcomplete", "#locomotion_totals`tpackets=4 walk=3 sprint=0 releases=1 holding=0 faults=0")
. $validateLocomotion
. $validateObservedSwing
$ArmSwingRun = $false
$tail = @("#locomotion_mode`tarm_swing", "#gesture_reload`tapplied_after_release", "#arm_swing_totals`tframes=100 starts=0", "#locomotion_totals`tsprint=0 holding=0 faults=0")
Assert-Throws { . $validateObservedSwing } 'A runtime switch to arm-swing bypassed observed sprint validation.'
$tail = $tail -replace 'starts=0','starts=1' -replace 'sprint=0','sprint=3'
. $validateObservedSwing
