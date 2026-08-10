param(
    [int]$Port = 8765,
    [switch]$NoOpen
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Arguments = @("run", "python", (Join-Path $RepoRoot "tools/dashboard.py"), "--port", $Port)
if ($NoOpen) {
    $Arguments += "--no-open"
}

Push-Location $RepoRoot
try {
    & uv @Arguments
} finally {
    Pop-Location
}
