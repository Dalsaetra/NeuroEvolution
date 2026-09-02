param(
    [ValidateRange(1, 100000)][int]$Creatures = 24,
    [ValidateRange(1, 100000000)][int]$Steps = 4800,
    [ValidateRange(0, 2147483647)][int]$Seed = 7,
    [string]$RunDir = "",
    [string]$Resume = "",
    [ValidateSet("spiking", "reactive", "random")][string]$Controller = "spiking",
    [switch]$NoReproduction,
    [switch]$Build,
    [switch]$Open
)

$ErrorActionPreference = "Stop"
if (-not [string]::IsNullOrWhiteSpace($Resume)) {
    foreach ($Parameter in @("Creatures", "Seed", "Controller", "NoReproduction")) {
        if ($PSBoundParameters.ContainsKey($Parameter)) {
            throw "-$Parameter cannot be combined with -Resume; the checkpoint preserves its configuration."
        }
    }
}
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $RepoRoot "build"
if ([string]::IsNullOrWhiteSpace($RunDir)) {
    $RunDir = "runs/ecosystem_$(Get-Date -Format 'yyyyMMdd_HHmmssfff')"
}
$RunPath = if ([System.IO.Path]::IsPathRooted($RunDir)) {
    [System.IO.Path]::GetFullPath($RunDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $RunDir))
}

if ($Build) {
    & cmake -S $RepoRoot -B $BuildPath
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }
    & cmake --build $BuildPath --config Release --target neuroevo_ecosystem
    if ($LASTEXITCODE -ne 0) { throw "Ecosystem build failed with exit code $LASTEXITCODE" }
}

$Executable = $null
foreach ($Candidate in @(
    (Join-Path $BuildPath "neuroevo_ecosystem.exe"),
    (Join-Path $BuildPath "Release/neuroevo_ecosystem.exe"),
    (Join-Path $BuildPath "Debug/neuroevo_ecosystem.exe"),
    (Join-Path $BuildPath "neuroevo_ecosystem")
)) {
    if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
        $Executable = $Candidate
        break
    }
}
if (-not $Executable) {
    throw "Ecosystem executable not found under $BuildPath. Run this script with -Build first."
}

$SimulationArguments = @("--steps", $Steps, "--out", $RunPath)
if (-not [string]::IsNullOrWhiteSpace($Resume)) {
    $ResumePath = if ([System.IO.Path]::IsPathRooted($Resume)) {
        [System.IO.Path]::GetFullPath($Resume)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Resume))
    }
    if (-not (Test-Path -LiteralPath $ResumePath -PathType Leaf)) {
        throw "Checkpoint does not exist: $ResumePath"
    }
    $SimulationArguments += @("--resume", $ResumePath)
} else {
    $SimulationArguments += @("--creatures", $Creatures, "--seed", $Seed, "--controller", $Controller)
    if ($NoReproduction) { $SimulationArguments += "--no-reproduction" }
}
& $Executable @SimulationArguments
if ($LASTEXITCODE -ne 0) { throw "Ecosystem simulation failed with exit code $LASTEXITCODE" }

$ViewerScript = Join-Path $RepoRoot "tools/view_ecosystem.py"
if (Get-Command uv -ErrorAction SilentlyContinue) {
    & uv run python $ViewerScript $RunPath
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    & python $ViewerScript $RunPath
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $ViewerScript $RunPath
} else {
    throw "Recording saved to $RunPath. Python is required to generate the HTML viewer."
}
if ($LASTEXITCODE -ne 0) { throw "Viewer generation failed with exit code $LASTEXITCODE" }

$ViewerPath = Join-Path $RunPath "ecosystem.html"
Write-Host "Viewer: $ViewerPath"
if ($Open) {
    # The user explicitly requested the visible, interactive replay window.
    Start-Process -FilePath $ViewerPath
}
