param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ build tools are required.' }
& cmake -S $projectRoot -B "$projectRoot\build" -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_GENERATOR_INSTANCE=$vs"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& cmake --build "$projectRoot\build" --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& ctest --test-dir "$projectRoot\build" -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
