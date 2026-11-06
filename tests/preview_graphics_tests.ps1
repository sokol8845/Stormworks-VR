$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts\preview-graphics.ps1')
function Assert-True([bool]$ok,[string]$message) { if (-not $ok) { throw $message } }
$folder = Join-Path ([System.IO.Path]::GetTempPath()) ('swvr-graphics-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder | Out-Null
try {
    $path = Join-Path $folder 'graphics_config.xml'
    $backup = Join-Path $folder 'before.xml'
    $original = '<?xml version="1.0"?><graphics_data flags="54" shadow_quality="3" resolution_x="1280" resolution_y="720" window_mode="0" version="10" write_key="18467"/>'
    [IO.File]::WriteAllText($path,$original)
    Set-SwvrPreviewGraphics -Path $path -BackupPath $backup
    $data = [xml](Get-Content -LiteralPath $path -Raw)
    Assert-True ($data.graphics_data.write_key -eq '0') 'Native crash-recovery branch still armed.'
    Assert-True ($data.graphics_data.resolution_x -eq '3440' -and $data.graphics_data.resolution_y -eq '1440' -and $data.graphics_data.window_mode -eq '1') 'Requested startup mode not saved.'
    Assert-True ($data.graphics_data.flags -eq '54' -and $data.graphics_data.shadow_quality -eq '3' -and $data.graphics_data.version -eq '10') 'Unrelated graphics settings changed.'
    Assert-True ([IO.File]::ReadAllText($backup) -eq $original) 'Backup changed the input bytes.'
    # The native game writes a marker during use; emulate a changed resolution
    # selected by the user before a controlled preview shutdown.
    $data.graphics_data.SetAttribute('resolution_x','2560')
    $data.graphics_data.SetAttribute('resolution_y','1080')
    $data.graphics_data.SetAttribute('write_key','18467')
    $data.Save($path)
    Set-SwvrPreviewGraphics -Path $path -ClearWriteKeyOnly
    $after = [xml](Get-Content -LiteralPath $path -Raw)
    Assert-True ($after.graphics_data.write_key -eq '0' -and $after.graphics_data.resolution_x -eq '2560' -and $after.graphics_data.resolution_y -eq '1080') 'Shutdown overwrote user settings or retained the marker.'
    $stable = [IO.File]::ReadAllText($path)
    $caught=$false;try { Set-SwvrPreviewGraphics -Path $path -Width 1 } catch { $caught=$true }
    Assert-True ($caught -and [IO.File]::ReadAllText($path) -eq $stable) 'Invalid dimensions mutated the file.'
    Assert-SwvrPreviewStartup -Lines @("#native_eye_source_config size=3440x1440 attachments=2")
    $caught=$false
    try { Assert-SwvrPreviewStartup -Lines @("#native_eye_source_config size=1280x720 attachments=2","#native_eye_source_config size=3440x1440 attachments=2") } catch { $caught=$true }
    Assert-True $caught 'A later resize hid incorrect startup dimensions.'
    # Windowed mode renders the client area: same width, height reduced by the title bar/frame.
    Assert-SwvrPreviewStartup -Lines @("#native_eye_source_config size=3440x1419 attachments=2") -Mode Windowed
    foreach ($bad in @(@('3440x1419','Fullscreen'),@('3440x1200','Windowed'),@('3400x1419','Windowed'),@('3440x1441','Windowed'))) {
        $caught=$false
        try { Assert-SwvrPreviewStartup -Lines @("#native_eye_source_config size=$($bad[0]) attachments=2") -Mode $bad[1] } catch { $caught=$true }
        Assert-True $caught "Accepted $($bad[0]) in $($bad[1]) mode."
    }
    Write-Output 'Preview graphics: startup crash marker, requested resolution, backup and controlled-close preservation passed.'
} finally {
    Get-ChildItem -LiteralPath $folder -File | Remove-Item
    Remove-Item -LiteralPath $folder
}
