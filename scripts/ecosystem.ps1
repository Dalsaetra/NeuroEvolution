param(
    [switch]$NoPredation,
    [ValidateRange(0.5, 2)][double]$FounderMass = 1,
    [ValidateRange(0, 1)][double]$FounderCarnivory = 0,
    [ValidateRange(0, 1)][double]$MassMutationProbability = 0.2,
    [ValidateRange(0, 10)][double]$MassMutationSigma = 0.12,
    [ValidateRange(0, 1)][double]$CarnivoryMutationProbability = 0.2,
    [ValidateRange(0, 10)][double]$CarnivoryMutationSigma = 0.08,
    [ValidateRange(1, 100000)][int]$Creatures = 24,
    [ValidateRange(1, 100000000)][int]$Steps = 4800,
    [ValidateRange(0, 2147483647)][int]$Seed = 7,
    [string]$RunDir = "",
    [string]$Resume = "",
    [string]$StartingGenomes = "",
    [ValidateSet("spiking", "reactive", "random")][string]$Controller = "spiking",
    [ValidateSet("random", "sparse-ancestor")][string]$FounderBrain = "random",
    [ValidateSet("calibrated", "legacy")][string]$Sensorimotor = "calibrated",
    [ValidateSet("stable", "legacy")][string]$MutationMode = "stable",
    [ValidateRange(0, 32)][int]$ArchiveEvalTrials = 5,
    [ValidateRange(1, 32)][int]$ArchiveEvalWorkers = [Math]::Max(1, [Math]::Min(4, [Environment]::ProcessorCount)),
    [ValidateRange(0.1, 100000)][double]$ArchiveEvalSeconds = 600,
    [ValidateRange(0, 2147483647)][int]$ArchiveEvalSeed = 17071,
    [ValidateRange(0, 10)][double]$ActuatorTau = 0.3,
    [switch]$SoloAncestorTrial,
    [ValidateSet("generated", "nursery-frontier")][string]$Habitat = "generated",
    [ValidateRange(0, 100000)][int]$NurseryExitWidth = 3,
    [ValidateRange(1, 100000)][int]$ShelterSize = 3,
    [ValidateRange(1, 100000)][int]$NurseryFoodPatches = 18,
    [ValidateRange(0.000001, 1000000)][double]$NurseryFoodEnergy = 50,
    [ValidateRange(0, 1000000)][double]$ShelterFoodDecay = 0.005,
    [ValidateRange(0, 1000000)][double]$NurseryFoodDecay = 0.005,
    [ValidateRange(0, 1000000)][double]$GrazeDecay = 0.005,
    [ValidateRange(0, 1000000)][double]$FruitDecay = 0.005,
    [ValidateRange(0.000001, 1000000)][double]$ShelterFoodEnergy = 1,
    [ValidateRange(0.000001, 1000000)][double]$ShelterFoodCapacity = 2,
    [ValidateRange(0, 1000000)][double]$ShelterFoodRegrowth = 0.6,
    [switch]$NoReproduction,
    [switch]$NoStorms,
    [switch]$Establishment,
    [ValidateRange(0, 255)][int]$ImmigrationFloor = 0,
    [ValidateRange(1, 256)][int]$ImmigrationBatch = 2,
    [ValidateRange(0.01, 1000000)][double]$ImmigrationInterval = 5,
    [ValidateRange(4, 256)][int]$ArchiveCapacity = 16,
    [ValidateRange(1, 256)][int]$ArchiveTournamentSize = 3,
    [ValidateRange(0, 1000000)][double]$ArchiveMinAge = 60,
    [ValidateRange(0.000001, 1000000)][double]$ArchiveMinEnergy = 37.5,
    [ValidateRange(1, 1000000)][int]$ArchiveMinFeedingBouts = 3,
    [ValidateRange(0, 1000000)][double]$ArchiveMinEfficiency = 0.6,
    [ValidateRange(0.000001, 1000000)][double]$GrazeEnergy = 2.5,
    [ValidateRange(0.000001, 1000000)][double]$PoorFruitEnergy = 5,
    [ValidateRange(0.000001, 1000000)][double]$RichFruitEnergy = 12.5,
    [ValidateRange(0.000001, 1000000)][double]$PodEnergy = 15,
    [switch]$KeepImmigration,
    [ValidateSet("baseline", "breeding")][string]$EvolutionPreset = "baseline",
    [ValidateSet("compact", "standard", "detailed")][string]$Recording = "compact",
    [ValidateRange(0, 1000000)][double]$DetailedTailSeconds = 300,
    [switch]$KeepJsonl,
    [switch]$Build,
    [switch]$Open,
    [switch]$OpenTail
)

$ErrorActionPreference = "Stop"
if (-not [string]::IsNullOrWhiteSpace($StartingGenomes)) {
    foreach ($Parameter in @("Resume", "FounderBrain", "SoloAncestorTrial")) {
        if ($PSBoundParameters.ContainsKey($Parameter)) {
            throw "-StartingGenomes cannot be combined with -$Parameter; it supplies founders for a fresh run."
        }
    }
}
if ($OpenTail -and $DetailedTailSeconds -le 0) {
    throw "-OpenTail requires -DetailedTailSeconds greater than zero."
}
if (-not [string]::IsNullOrWhiteSpace($Resume)) {
    foreach ($Parameter in @("NoPredation", "FounderMass", "FounderCarnivory", "MassMutationProbability", "MassMutationSigma", "CarnivoryMutationProbability", "CarnivoryMutationSigma", "Creatures", "Seed", "Controller", "FounderBrain", "SoloAncestorTrial", "NoReproduction", "NoStorms", "Establishment",
        "ImmigrationFloor", "ImmigrationBatch", "ImmigrationInterval", "ArchiveCapacity", "ArchiveTournamentSize", "ArchiveMinAge",
        "ArchiveMinEnergy", "ArchiveMinFeedingBouts", "ArchiveMinEfficiency", "GrazeEnergy", "PoorFruitEnergy",
        "RichFruitEnergy", "PodEnergy", "KeepImmigration",
        "EvolutionPreset", "Sensorimotor", "ArchiveEvalTrials", "ArchiveEvalSeconds", "ArchiveEvalSeed", "ActuatorTau", "Habitat", "NurseryExitWidth", "ShelterSize", "NurseryFoodPatches", "NurseryFoodEnergy", "NurseryFoodDecay", "ShelterFoodDecay", "GrazeDecay", "FruitDecay", "ShelterFoodEnergy", "ShelterFoodCapacity", "ShelterFoodRegrowth")) {
        if ($PSBoundParameters.ContainsKey($Parameter)) {
            throw "-$Parameter cannot be combined with -Resume; the checkpoint preserves its configuration."
        }
    }
}
$RepoRoot = Split-Path -Parent $PSScriptRoot
$StartingGenomesPath = ""
if (-not [string]::IsNullOrWhiteSpace($StartingGenomes)) {
    $StartingGenomesPath = if ([System.IO.Path]::IsPathRooted($StartingGenomes)) {
        [System.IO.Path]::GetFullPath($StartingGenomes)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $StartingGenomes))
    }
    if (-not (Test-Path -LiteralPath (Join-Path $StartingGenomesPath "checkpoint.eco") -PathType Leaf)) {
        throw "Starting genomes require a run folder containing checkpoint.eco: $StartingGenomesPath"
    }
}
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
    & cmake -S $RepoRoot -B $BuildPath -DCMAKE_BUILD_TYPE=Release
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

$SimulationArguments = @("--steps", $Steps, "--out", $RunPath, "--archive-eval-workers", $ArchiveEvalWorkers)
if ([string]::IsNullOrWhiteSpace($Resume) -or $PSBoundParameters.ContainsKey("MutationMode")) {
    $SimulationArguments += @("--stable-mutations", [int]($MutationMode -eq "stable"))
}
switch ($Recording) {
    "compact"  { $SimulationArguments += @("--record-every", 500, "--record-brains", 0, "--record-observations", 0, "--record-brain-graphs", 0, "--record-routine-events", 0) }
    "standard" { $SimulationArguments += @("--record-every", 10, "--record-brains", 0, "--record-observations", 1, "--record-brain-graphs", 1, "--record-routine-events", 0) }
    "detailed" { $SimulationArguments += @("--record-every", 10, "--record-brains", 1, "--record-observations", 1, "--record-brain-graphs", 1, "--record-routine-events", 1) }
}
$EffectiveTailSeconds = $DetailedTailSeconds
if ($Recording -eq "detailed" -and -not $OpenTail -and -not $PSBoundParameters.ContainsKey("DetailedTailSeconds")) {
    $EffectiveTailSeconds = 0
}
if ($EffectiveTailSeconds -gt 0) {
    $SimulationArguments += @("--detailed-tail-seconds", $EffectiveTailSeconds.ToString([System.Globalization.CultureInfo]::InvariantCulture), "--tail-record-every", 10)
}
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
    $EffectiveCreatures = if ($SoloAncestorTrial) { 1 } else { $Creatures }
    if ($SoloAncestorTrial -and $Habitat -ne "generated") { throw "-SoloAncestorTrial cannot be combined with -Habitat nursery-frontier" }
    $SimulationArguments += @("--habitat", $Habitat)
    if ($NoPredation) { $SimulationArguments += @("--predation", 0) }
    foreach ($TraitSetting in @(@("FounderMass", "--founder-mass", $FounderMass),
        @("FounderCarnivory", "--founder-carnivory", $FounderCarnivory),
        @("MassMutationProbability", "--mass-mutation-probability", $MassMutationProbability),
        @("MassMutationSigma", "--mass-mutation-sigma", $MassMutationSigma),
        @("CarnivoryMutationProbability", "--carnivory-mutation-probability", $CarnivoryMutationProbability),
        @("CarnivoryMutationSigma", "--carnivory-mutation-sigma", $CarnivoryMutationSigma))) {
        if ($PSBoundParameters.ContainsKey($TraitSetting[0])) {
            $SimulationArguments += @($TraitSetting[1], $TraitSetting[2].ToString([System.Globalization.CultureInfo]::InvariantCulture))
        }
    }
    if ($PSBoundParameters.ContainsKey("NurseryFoodPatches")) { $SimulationArguments += @("--nursery-food-patches", $NurseryFoodPatches) }
    if ($PSBoundParameters.ContainsKey("NurseryFoodEnergy")) { $SimulationArguments += @("--nursery-food-energy", $NurseryFoodEnergy.ToString([System.Globalization.CultureInfo]::InvariantCulture)) }
    if ($PSBoundParameters.ContainsKey("ShelterSize")) {
        $SimulationArguments += @("--shelter-size", $ShelterSize)
    }
    if ($PSBoundParameters.ContainsKey("NurseryExitWidth")) {
        $SimulationArguments += @("--nursery-exit-width", $NurseryExitWidth)
    }
    $EffectiveController = if ($SoloAncestorTrial) { "spiking" } else { $Controller }
    $EffectiveFounderBrain = if ($SoloAncestorTrial) { "sparse-ancestor" } else { $FounderBrain }
    if ($Habitat -eq "nursery-frontier" -and -not $PSBoundParameters.ContainsKey("FounderBrain")) { $EffectiveFounderBrain = "sparse-ancestor" }
    $SimulationArguments += @("--creatures", $EffectiveCreatures, "--seed", $Seed, "--controller", $EffectiveController,
        "--sensorimotor", $Sensorimotor,
        "--archive-eval-trials", $ArchiveEvalTrials, "--archive-eval-seed", $ArchiveEvalSeed,
        "--archive-eval-seconds", $ArchiveEvalSeconds.ToString([System.Globalization.CultureInfo]::InvariantCulture))
    if ($StartingGenomesPath) {
        $SimulationArguments += @("--starting-genomes", $StartingGenomesPath)
    } else {
        $SimulationArguments += @("--founder-brain", $EffectiveFounderBrain)
    }
    if ($PSBoundParameters.ContainsKey("ActuatorTau")) {
        $SimulationArguments += @("--actuator-tau", $ActuatorTau.ToString([System.Globalization.CultureInfo]::InvariantCulture))
    }
    if ($SoloAncestorTrial) {
        $SimulationArguments += @("--habitat", "ancestor-nursery",
            "--mutate-weight-prob", 0, "--mutate-neuron-prob", 0,
            "--mutate-add-synapse-prob", 0, "--mutate-add-neuron-prob", 0, "--mutate-reciprocal-motif-prob", 0,
            "--mutate-remove-synapse-prob", 0)
    }
    if ($NoReproduction) { $SimulationArguments += "--no-reproduction" }
    if ($NoStorms) { $SimulationArguments += "--no-storms" }
    if ($Establishment) { $SimulationArguments += @("--establishment", "1") }
    $SimulationArguments += @("--immigration-floor", $ImmigrationFloor, "--immigration-batch", $ImmigrationBatch,
        "--immigration-interval", $ImmigrationInterval.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        "--archive-capacity", $ArchiveCapacity,
        "--archive-tournament-size", $ArchiveTournamentSize,
        "--archive-min-age", $ArchiveMinAge.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        "--archive-min-energy", $ArchiveMinEnergy.ToString([System.Globalization.CultureInfo]::InvariantCulture),
        "--archive-min-feeding-bouts", $ArchiveMinFeedingBouts,
        "--archive-min-efficiency", $ArchiveMinEfficiency.ToString([System.Globalization.CultureInfo]::InvariantCulture))
    foreach ($FoodSetting in @(@("GrazeEnergy", "--graze-energy", $GrazeEnergy),
        @("PoorFruitEnergy", "--poor-fruit-energy", $PoorFruitEnergy),
        @("RichFruitEnergy", "--rich-fruit-energy", $RichFruitEnergy), @("PodEnergy", "--pod-energy", $PodEnergy))) {
        if ($Habitat -ne "nursery-frontier" -or $PSBoundParameters.ContainsKey($FoodSetting[0])) {
            $SimulationArguments += @($FoodSetting[1], $FoodSetting[2].ToString([System.Globalization.CultureInfo]::InvariantCulture))
        }
    }
    foreach ($FoodSetting in @(@("ShelterFoodDecay", "--shelter-food-decay", $ShelterFoodDecay),
        @("NurseryFoodDecay", "--nursery-food-decay", $NurseryFoodDecay),
        @("GrazeDecay", "--graze-decay", $GrazeDecay),
        @("FruitDecay", "--fruit-decay", $FruitDecay), @("ShelterFoodEnergy", "--shelter-food-energy", $ShelterFoodEnergy),
        @("ShelterFoodCapacity", "--shelter-food-capacity", $ShelterFoodCapacity),
        @("ShelterFoodRegrowth", "--shelter-food-regrowth", $ShelterFoodRegrowth))) {
        if ($PSBoundParameters.ContainsKey($FoodSetting[0])) {
            $SimulationArguments += @($FoodSetting[1], $FoodSetting[2].ToString([System.Globalization.CultureInfo]::InvariantCulture))
        }
    }
    if ($KeepImmigration) { $SimulationArguments += @("--immigration-auto-stop", "0") }
    if ($EvolutionPreset -eq "breeding") {
        # Makes lineage continuation less brittle while retaining meaningful food and weather pressure.
        $SimulationArguments += @(
            "--maturity-age", 60, "--reproduction-threshold", 130,
            "--reproduction-cost", 65, "--offspring-energy", 60,
            "--reproduction-cooldown", 90
        )
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
