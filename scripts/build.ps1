param([string]$BuildDir = "build")
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $RepoRoot $BuildDir
& cmake -S $RepoRoot -B $BuildPath -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
& cmake --build $BuildPath --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
& ctest --test-dir $BuildPath -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
