param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $RepoRoot $BuildDir

cmake -S $RepoRoot -B $BuildPath
cmake --build $BuildPath
ctest --test-dir $BuildPath --output-on-failure
