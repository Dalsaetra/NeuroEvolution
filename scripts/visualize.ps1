param(
    [string]$RunDir = "runs/latest",
    [switch]$Open
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$RunPath = Join-Path $RepoRoot $RunDir

if (-not (Test-Path $RunPath -PathType Container)) {
    throw "Run directory does not exist: $RunPath"
}

uv run python (Join-Path $RepoRoot "tools/plot_run.py") $RunPath
uv run python (Join-Path $RepoRoot "tools/view_run.py") $RunPath

$ViewerPath = Join-Path $RunPath "viewer.html"
Write-Host "Viewer: $ViewerPath"

if ($Open) {
    Start-Process $ViewerPath
}
