param(
    [ValidateRange(1, 100000000)][int]$Steps,
    [ValidateRange(1, 100000)][int]$Creatures,
    [ValidateRange(0, 2147483647)][int]$Seed,
    [string]$RunDir = "",
    [string]$Resume = "",
    [string]$StartingGenomes = "",
    [ValidateSet("spiking", "reactive", "random")][string]$Controller,
    [ValidateSet("random", "sparse-ancestor")][string]$FounderBrain,
    [ValidateSet("compact", "standard", "detailed")][string]$Recording,
    [ValidateRange(0, 1000000)][double]$DetailedTailSeconds,
    [switch]$KeepJsonl,
    [switch]$Build,
    [switch]$Open,
    [switch]$OpenTail
)

# Tune simulation defaults in include/neuroevo/config.hpp, then rebuild.
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $RepoRoot "build"
function Resolve-RepoPath([string]$Value) {
    if ([System.IO.Path]::IsPathRooted($Value)) { return [System.IO.Path]::GetFullPath($Value) }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Value))
}
if ([string]::IsNullOrWhiteSpace($RunDir)) { $RunDir = "runs/ecosystem_$(Get-Date -Format 'yyyyMMdd_HHmmssfff')" }
$RunPath = Resolve-RepoPath $RunDir
if ($Resume) {
    foreach ($Parameter in @("Creatures", "Seed", "Controller", "FounderBrain", "StartingGenomes")) {
        if ($PSBoundParameters.ContainsKey($Parameter)) { throw "-$Parameter cannot be combined with -Resume; the checkpoint preserves its configuration." }
    }
}
if ($StartingGenomes -and $PSBoundParameters.ContainsKey("FounderBrain")) {
    throw "-StartingGenomes and -FounderBrain select different genome sources."
}
if ($OpenTail -and (-not $PSBoundParameters.ContainsKey("DetailedTailSeconds") -or $DetailedTailSeconds -le 0)) {
    throw "-OpenTail requires -DetailedTailSeconds greater than zero."
}
if ($Build) {
    & cmake -S $RepoRoot -B $BuildPath -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
    & cmake --build $BuildPath --config Release --target neuroevo_ecosystem
    if ($LASTEXITCODE -ne 0) { throw "Ecosystem build failed" }
}
$Executable = $null
foreach ($Candidate in @("neuroevo_ecosystem.exe", "Release/neuroevo_ecosystem.exe", "Debug/neuroevo_ecosystem.exe", "neuroevo_ecosystem")) {
    $Path = Join-Path $BuildPath $Candidate
    if (Test-Path -LiteralPath $Path -PathType Leaf) { $Executable = $Path; break }
}
if (-not $Executable) { throw "Ecosystem executable not found. Run with -Build first." }
$SimulationArguments = @("--out", $RunPath)
foreach ($Setting in @(@("Steps", "--steps"), @("Creatures", "--creatures"), @("Seed", "--seed"),
    @("Controller", "--controller"), @("FounderBrain", "--founder-brain"), @("DetailedTailSeconds", "--detailed-tail-seconds"))) {
    if ($PSBoundParameters.ContainsKey($Setting[0])) {
        $Value = $PSBoundParameters[$Setting[0]]
        $SimulationArguments += @($Setting[1], [Convert]::ToString($Value, [System.Globalization.CultureInfo]::InvariantCulture))
    }
}
if ($Resume) { $SimulationArguments += @("--resume", (Resolve-RepoPath $Resume)) }
if ($StartingGenomes) { $SimulationArguments += @("--starting-genomes", (Resolve-RepoPath $StartingGenomes)) }
switch ($Recording) {
    "compact"  { $SimulationArguments += @("--record-every", 500, "--record-brains", 0, "--record-observations", 0, "--record-brain-graphs", 0, "--record-routine-events", 0) }
    "standard" { $SimulationArguments += @("--record-every", 10, "--record-brains", 0, "--record-observations", 1, "--record-brain-graphs", 1, "--record-routine-events", 0) }
    "detailed" { $SimulationArguments += @("--record-every", 10, "--record-brains", 1, "--record-observations", 1, "--record-brain-graphs", 1, "--record-routine-events", 1) }
}
& $Executable @SimulationArguments
if ($LASTEXITCODE -ne 0) { throw "Ecosystem simulation failed with exit code $LASTEXITCODE" }

$ViewerScript = Join-Path $RepoRoot "tools/view_ecosystem.py"
$ViewerArguments = @($RunPath)
if (-not $KeepJsonl) { $ViewerArguments += "--gzip-source" }
if (Get-Command uv -ErrorAction SilentlyContinue) {
    & uv run python $ViewerScript @ViewerArguments
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    & python $ViewerScript @ViewerArguments
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $ViewerScript @ViewerArguments
} else {
    throw "Recording saved to $RunPath. Python is required to generate the HTML viewer."
}
if ($LASTEXITCODE -ne 0) { throw "Viewer generation failed with exit code $LASTEXITCODE" }

$TailSource = Join-Path $RunPath "ecosystem_tail.jsonl"
if (Test-Path -LiteralPath $TailSource -PathType Leaf) {
    $TailViewer = Join-Path $RunPath "ecosystem_tail.html"
    $TailArguments = @($TailSource, "--output", $TailViewer)
    if (-not $KeepJsonl) { $TailArguments += "--gzip-source" }
    if (Get-Command uv -ErrorAction SilentlyContinue) { & uv run python $ViewerScript @TailArguments }
    elseif (Get-Command python -ErrorAction SilentlyContinue) { & python $ViewerScript @TailArguments }
    else { & py -3 $ViewerScript @TailArguments }
    if ($LASTEXITCODE -ne 0) { throw "Detailed-tail viewer generation failed with exit code $LASTEXITCODE" }
    Write-Host "Detailed tail: $TailViewer"
}

$ViewerPath = Join-Path $RunPath "ecosystem.html"
Write-Host "Viewer: $ViewerPath"
Write-Host "Run files:"
Get-ChildItem -LiteralPath $RunPath -File | Sort-Object Length -Descending | ForEach-Object {
    Write-Host ("  {0,-30} {1,9:N2} MiB" -f $_.Name, ($_.Length / 1MB))
}
if ($Open) {
    # The user explicitly requested the visible, interactive replay window.
    Start-Process -FilePath $ViewerPath
}

if ($OpenTail) {
    # The user explicitly requested the visible, interactive detailed-tail replay.
    Start-Process -FilePath $TailViewer
}
