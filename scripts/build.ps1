param([string]$BuildDir = "build", [switch]$VerboseBuild)
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $RepoRoot $BuildDir
. (Join-Path $PSScriptRoot "build-output.ps1")
Write-Host "Configuring and building Release..."
Invoke-BuildCommand cmake @("-S", $RepoRoot, "-B", $BuildPath, "-DCMAKE_BUILD_TYPE=Release") -Detailed:$VerboseBuild
Invoke-BuildCommand cmake @("--build", $BuildPath, "--config", "Release", "--parallel") -Detailed:$VerboseBuild
Write-Host "Running tests..."
Invoke-BuildCommand ctest @("--test-dir", $BuildPath, "-C", "Release", "--output-on-failure") -Detailed:$VerboseBuild
Write-Host "Build and tests passed."
