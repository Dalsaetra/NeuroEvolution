param(
    [string]$RunDir = "runs/latest",
    [switch]$Open,
    [switch]$SkipParetoSolutionViewers
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$RunPath = if ([System.IO.Path]::IsPathRooted($RunDir)) {
    $RunDir
} else {
    Join-Path $RepoRoot $RunDir
}

if (-not (Test-Path $RunPath -PathType Container)) {
    throw "Run directory does not exist: $RunPath"
}

$PlotScript = Join-Path $RepoRoot "tools/plot_run.py"
$RunViewerScript = Join-Path $RepoRoot "tools/view_run.py"
$ParetoViewerScript = Join-Path $RepoRoot "tools/view_pareto_front.py"

uv run python $PlotScript $RunPath
uv run python $RunViewerScript $RunPath

$ViewerPath = Join-Path $RunPath "viewer.html"
Write-Host "Viewer: $ViewerPath"

$ParetoManifestPath = Join-Path $RunPath "pareto_front.csv"
$ParetoViewerPath = Join-Path $RunPath "pareto_front.html"

if (Test-Path $ParetoManifestPath -PathType Leaf) {
    uv run python $ParetoViewerScript $RunPath
    Write-Host "Pareto front dashboard: $ParetoViewerPath"

    if (-not $SkipParetoSolutionViewers) {
        $Solutions = Import-Csv $ParetoManifestPath
        foreach ($Solution in $Solutions) {
            if ([string]::IsNullOrWhiteSpace($Solution.trajectory_dir)) {
                continue
            }

            $SolutionPath = Join-Path $RunPath $Solution.trajectory_dir
            if (-not (Test-Path $SolutionPath -PathType Container)) {
                Write-Warning "Pareto solution directory missing: $SolutionPath"
                continue
            }

            uv run python $RunViewerScript $SolutionPath
        }
        Write-Host "Pareto solution viewers: $(Join-Path $RunPath 'pareto_front/solution_XX/viewer.html')"
    }
} else {
    Write-Host "Pareto front dashboard: skipped (no pareto_front.csv)"
}

if ($Open) {
    Start-Process $ViewerPath
    if (Test-Path $ParetoViewerPath -PathType Leaf) {
        Start-Process $ParetoViewerPath
    }
}
