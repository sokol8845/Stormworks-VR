# The profiled native loader (2799F0) forces 1280x720/windowed after a
# nonzero graphics write_key. Controlled preview termination must not look
# like an unfinished graphics-settings update on the following launch.
function Set-SwvrPreviewGraphics {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [int]$Width = 3440,
        [int]$Height = 1440,
        [ValidateSet('Fullscreen','Windowed')][string]$Mode = 'Fullscreen',
        [string]$BackupPath,
        [switch]$ClearWriteKeyOnly
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing graphics configuration: $Path" }
    if (-not $ClearWriteKeyOnly -and ($Width -lt 64 -or $Width -gt 16384 -or $Height -lt 64 -or $Height -gt 16384)) {
        throw 'Preview resolution is outside supported bounds.'
    }
    $document = [System.Xml.XmlDocument]::new()
    $document.PreserveWhitespace = $true
    $document.XmlResolver = $null
    $document.Load($Path)
    $root = $document.DocumentElement
    if ($root.Name -ne 'graphics_data') { throw 'Unexpected graphics configuration root.' }
    if ($BackupPath) {
        if (Test-Path -LiteralPath $BackupPath) { throw 'Refusing to overwrite graphics backup.' }
        Copy-Item -LiteralPath $Path -Destination $BackupPath
    }
    if (-not $ClearWriteKeyOnly) {
        $root.SetAttribute('resolution_x',[string]$Width)
        $root.SetAttribute('resolution_y',[string]$Height)
        $root.SetAttribute('window_mode',$(if ($Mode -eq 'Fullscreen') { '1' } else { '0' }))
    }
    $root.SetAttribute('write_key','0')
    $temporary = $Path + '.swvr-' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        $writerOptions = [System.Xml.XmlWriterSettings]::new()
        $writerOptions.Encoding = [System.Text.UTF8Encoding]::new($false)
        $writerOptions.Indent = $false
        $writer = [System.Xml.XmlWriter]::Create($temporary,$writerOptions)
        try { $document.Save($writer) } finally { $writer.Dispose() }
        [System.IO.File]::Replace($temporary,$Path,[System.Management.Automation.Language.NullString]::Value)
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}


function Assert-SwvrPreviewStartup {
    param([string[]]$Lines,[int]$Width=3440,[int]$Height=1440,[ValidateSet('Fullscreen','Windowed')][string]$Mode='Fullscreen')
    $sources = @($Lines -match '^#native_eye_source_config\s+size=')
    if (-not $sources.Count) { throw 'Missing initial native eye-source dimensions.' }
    if ($sources[0] -notmatch '^#native_eye_source_config\s+size=(\d+)x(\d+)\b') { throw "Malformed initial resolution: $($sources[0])" }
    $actualWidth = [int]$Matches[1]; $actualHeight = [int]$Matches[2]
    # Windowed mode renders the client area: the title bar/frame removes up to 120 px of height.
    $ok = if ($Mode -eq 'Windowed') { $actualWidth -eq $Width -and $actualHeight -le $Height -and $actualHeight -ge $Height-120 }
          else { $actualWidth -eq $Width -and $actualHeight -eq $Height }
    if (-not $ok) {
        throw "Incorrect initial resolution: $($sources[0]); expected ${Width}x${Height} ($Mode). A later resize does not satisfy startup validation."
    }
}
