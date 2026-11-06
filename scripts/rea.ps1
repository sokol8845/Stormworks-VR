# Local, pinned REA toolchain. Environment changes last only for this invocation.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$node = Join-Path $projectRoot 'tools\runtime\node_modules\node\bin\node.exe'
$entry = Join-Path $projectRoot 'tools\rea\node_modules\rea-agents\scripts\rea.mjs'
$jdk = Join-Path $projectRoot 'tools\jdk\jdk-21.0.12.1+1'
$ghidra = Join-Path $projectRoot 'tools\ghidra\ghidra_12.1.4_PUBLIC'
foreach ($required in @($node, $entry, "$jdk\bin\javac.exe", "$ghidra\support\analyzeHeadless.bat")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing local tool: $required" }
}
$previousJava = $env:JAVA_HOME
$previousGhidra = $env:GHIDRA_INSTALL_DIR
$previousPath = $env:PATH
try {
    $env:JAVA_HOME = $jdk
    $env:GHIDRA_INSTALL_DIR = $ghidra
    $env:PATH = "$(Split-Path $node);$jdk\bin;$previousPath"
    & $node $entry @args
    $reaExitCode = $LASTEXITCODE
} finally {
    $env:JAVA_HOME = $previousJava
    $env:GHIDRA_INSTALL_DIR = $previousGhidra
    $env:PATH = $previousPath
}
exit $reaExitCode
