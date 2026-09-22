param(
    [ValidateRange(1, 100000000)][int]$Steps,
    [ValidateRange(0, 256)][int]$Threads,
    [ValidateRange(1, 100000)][int]$Creatures,
    [ValidateRange(0, 2147483647)][int]$Seed,
    [string]$RunDir = "",
    [string]$Resume = "",
    [string]$StartingGenomes = "",
    [ValidateSet("spiking", "reactive", "random")][string]$Controller,
    [ValidateSet("random", "sparse-ancestor")][string]$FounderBrain,
    [ValidateSet("fields-and-trees", "scattered")][string]$FoodDistribution,
    [ValidateSet("lif", "izhikevich", "filtered-lif")][string]$NeuronModel,
    [ValidateRange(0.000001, 1)][double]$BrainDt,
    [ValidateRange(0.000001, 10000)][double]$SynapticGain,
    [ValidateSet("compact", "standard", "detailed")][string]$Recording,
    [ValidateRange(0, 1000000)][double]$DetailedTailSeconds,
    [ValidateRange(1, 100000)][int]$TailRecordEvery,
    [switch]$KeepJsonl,
    [switch]$Build,
    [switch]$VerboseBuild,
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
    foreach ($Parameter in @("Creatures", "Seed", "Controller", "FounderBrain", "FoodDistribution", "StartingGenomes", "NeuronModel", "BrainDt", "SynapticGain")) {
        if ($PSBoundParameters.ContainsKey($Parameter)) { throw "-$Parameter cannot be combined with -Resume; the checkpoint preserves its configuration." }
    }
}
if ($StartingGenomes -and $PSBoundParameters.ContainsKey("FounderBrain")) {
    throw "-StartingGenomes and -FounderBrain select different genome sources."
}
if ($OpenTail -and $PSBoundParameters.ContainsKey("DetailedTailSeconds") -and $DetailedTailSeconds -le 0) {
    throw "-OpenTail requires -DetailedTailSeconds greater than zero."
}
if ($Build) {
    . (Join-Path $PSScriptRoot "build-output.ps1")
    Write-Host "Building ecosystem Release..."
    Invoke-BuildCommand cmake @("-S", $RepoRoot, "-B", $BuildPath, "-DCMAKE_BUILD_TYPE=Release") -Detailed:$VerboseBuild
    Invoke-BuildCommand cmake @("--build", $BuildPath, "--config", "Release", "--target", "neuroevo_ecosystem") -Detailed:$VerboseBuild
}
$Executable = $null
# Match the configured generator, even if a previous compiler left binaries here.
$CachePath = Join-Path $BuildPath "CMakeCache.txt"
$MultiConfig = (Test-Path -LiteralPath $CachePath) -and
    (Select-String -LiteralPath $CachePath -Pattern '^CMAKE_CONFIGURATION_TYPES:' -Quiet)
$Candidates = if ($MultiConfig) { @("Release/neuroevo_ecosystem.exe", "Release/neuroevo_ecosystem") }
    else { @("neuroevo_ecosystem.exe", "neuroevo_ecosystem") }
foreach ($Candidate in $Candidates) {
    $Path = Join-Path $BuildPath $Candidate
    if (Test-Path -LiteralPath $Path -PathType Leaf) { $Executable = $Path; break }
}
if (-not $Executable) { throw "Ecosystem executable not found. Run with -Build first." }
if ($Resume -or $StartingGenomes) {
    $CheckpointPath = if ($Resume) { Resolve-RepoPath $Resume }
        else { Join-Path (Resolve-RepoPath $StartingGenomes) "checkpoint.eco" }
    $Reader = [System.IO.File]::OpenText($CheckpointPath)
    $SavedWords = 0
    try {
        # Supported historical formats put the first RNG line in this header.
        for ($LineIndex = 0; $LineIndex -lt 32 -and -not $Reader.EndOfStream; ++$LineIndex) {
            $Line = $Reader.ReadLine().Trim()
            if ($Line -match '^\d+(\s+\d+){311,312}$') {
                $SavedWords = ($Line -split '\s+').Count
                break
            }
        }
    } finally { $Reader.Dispose() }
    if ($SavedWords) {
        $NativeWords = & $Executable --rng-state-words
        if ($LASTEXITCODE -ne 0) { throw "Rebuild the executable with -Build before loading a checkpoint." }
        if ([int]$NativeWords -ne $SavedWords) {
            if ($SavedWords -ne 313) {
                throw "This checkpoint needs the MSVC build. Configure a Visual Studio Release build to load it."
            }
            $GnuCompiler = Get-Command g++ -ErrorAction SilentlyContinue
            if (-not $GnuCompiler -or -not (Get-Command ninja -ErrorAction SilentlyContinue)) {
                throw "This checkpoint was saved by the GCC build. GCC (g++) and Ninja are required to load it with a compatible runtime."
            }
            $CompatibleBuild = Join-Path $BuildPath "resume-gnu"
            Write-Host "Checkpoint requires the GCC runtime; building a compatible executable."
            . (Join-Path $PSScriptRoot "build-output.ps1")
            Invoke-BuildCommand cmake @("-S", $RepoRoot, "-B", $CompatibleBuild, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_COMPILER=$($GnuCompiler.Source)") -Detailed:$VerboseBuild
            Invoke-BuildCommand cmake @("--build", $CompatibleBuild, "--target", "neuroevo_ecosystem") -Detailed:$VerboseBuild
            $Executable = Join-Path $CompatibleBuild "neuroevo_ecosystem.exe"
            $CompatibleWords = & $Executable --rng-state-words
            if ($LASTEXITCODE -ne 0 -or [int]$CompatibleWords -ne $SavedWords) {
                throw "The compatible build does not support this checkpoint's RNG format."
            }
        }
    }
}
Write-Host "Executable: $Executable"
$SimulationArguments = @("--out", $RunPath)
foreach ($Setting in @(@("Steps", "--steps"), @("Threads", "--threads"), @("Creatures", "--creatures"), @("Seed", "--seed"),
    @("FoodDistribution", "--food-distribution"),
    @("Controller", "--controller"), @("FounderBrain", "--founder-brain"), @("NeuronModel", "--neuron-model"), @("BrainDt", "--brain-dt"), @("SynapticGain", "--synaptic-gain"), @("DetailedTailSeconds", "--detailed-tail-seconds"), @("TailRecordEvery", "--tail-record-every"))) {
    if ($PSBoundParameters.ContainsKey($Setting[0])) {
        $Value = $PSBoundParameters[$Setting[0]]
        $SimulationArguments += @($Setting[1], [Convert]::ToString($Value, [System.Globalization.CultureInfo]::InvariantCulture))
    }
}
if ($Resume) { $SimulationArguments += @("--resume", (Resolve-RepoPath $Resume)) }
if ($StartingGenomes) { $SimulationArguments += @("--starting-genomes", (Resolve-RepoPath $StartingGenomes)) }
switch ($Recording) {
    "compact"  { $SimulationArguments += @("--record-every", 500, "--record-brains", 0, "--record-observations", 0, "--record-brain-graphs", 0, "--record-routine-events", 0) }
    "standard" { $SimulationArguments += @("--record-every", 10, "--record-brains", 0, "--record-observations", 0, "--record-brain-graphs", 0, "--record-routine-events", 0) }
    "detailed" {
        $SimulationArguments += @("--record-every", 500, "--record-brains", 0, "--record-observations", 0, "--record-brain-graphs", 0, "--record-routine-events", 0)
        if (-not $PSBoundParameters.ContainsKey("DetailedTailSeconds")) { $SimulationArguments += @("--detailed-tail-seconds", 600) }
    }
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
