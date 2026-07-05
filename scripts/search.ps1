param(
    [string]$Config = "experiments/fitness_search.example.json",
    [ValidateSet("grid", "random", "bayes")]
    [string]$Mode = "grid",
    [int]$MaxTrials = 12,
    [int]$Jobs = 1,
    [int]$VisualizeTop = 3,
    [switch]$Build,
    [switch]$PlotOnly,
    [switch]$DryRun,
    [switch]$RerunFailed
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot

if ($Build) {
    & (Join-Path $PSScriptRoot "build.ps1")
}

$ArgsList = @(
    "run",
    "python",
    (Join-Path $RepoRoot "tools/hypersearch.py"),
    (Join-Path $RepoRoot $Config),
    "--mode",
    $Mode,
    "--jobs",
    $Jobs,
    "--visualize-top",
    $VisualizeTop
)

if ($MaxTrials -gt 0) {
    $ArgsList += @("--max-trials", $MaxTrials)
}

if ($PlotOnly) {
    $ArgsList += "--plot-only"
}

if ($DryRun) {
    $ArgsList += "--dry-run"
}

if ($RerunFailed) {
    $ArgsList += "--rerun-failed"
}

Push-Location $RepoRoot
try {
    & uv @ArgsList
} finally {
    Pop-Location
}
