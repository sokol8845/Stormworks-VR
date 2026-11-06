param([int]$Seconds = 30, [string]$Loader)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Loader) {
    $steamRoot = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
    $Loader = Join-Path $steamRoot 'steamapps\common\SteamVR\bin\win64\openxr_loader.dll'
}
if (-not (Test-Path -LiteralPath $Loader)) { throw 'Provide -Loader with the installed OpenXR loader DLL path.' }
if ($Seconds -lt 1 -or $Seconds -gt 600) { throw 'Seconds must be between 1 and 600.' }
$demo = Join-Path $projectRoot 'build\Release\swvr_xr_demo.exe'
if (-not (Test-Path -LiteralPath $demo)) { throw 'Run scripts\build.ps1 first.' }
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force artifacts | Out-Null
    & $demo ([System.IO.Path]::GetFullPath($Loader)) $Seconds 2>&1 | Tee-Object -FilePath artifacts\native-demo-test.txt
    if ($LASTEXITCODE -ne 0) { throw 'VR hardware test did not complete. See artifacts\native-demo-test.txt and artifacts\steamvr-logs.' }
} finally { Pop-Location }
