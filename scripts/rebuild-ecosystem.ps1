param(
    [Parameter(Mandatory = $true, Position = 0)][string]$RunDir,
    [switch]$Open,
    [switch]$OpenTail
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$RunPath = if ([System.IO.Path]::IsPathRooted($RunDir)) {
    [System.IO.Path]::GetFullPath($RunDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $RunDir))
}
if (-not (Test-Path -LiteralPath $RunPath -PathType Container)) {
    throw "Run folder does not exist: $RunPath"
}
$ViewerScript = Join-Path $RepoRoot "tools/view_ecosystem.py"
$LocalPython = Join-Path $RepoRoot ".venv/Scripts/python.exe"
if (Test-Path -LiteralPath $LocalPython -PathType Leaf) {
    $Python = $LocalPython
    $PythonArguments = @()
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $Python = "python"
    $PythonArguments = @()
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    $Python = "py"
    $PythonArguments = @("-3")
} else {
    throw "Python is required to rebuild the viewers. No additional Python packages are needed."
}

$Rebuilt = @()
$Failed = @()
foreach ($Name in @("ecosystem", "ecosystem_tail")) {
    $Source = Join-Path $RunPath "$Name.jsonl"
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { $Source += ".gz" }
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        Write-Warning "No $Name.jsonl or $Name.jsonl.gz found. Its replay cannot be recovered from a checkpoint or summary alone."
        continue
    }
    $Destination = Join-Path $RunPath "$Name.html"
    # Generate beside the destination, then replace it only after successful rendering.
    $Temporary = Join-Path $RunPath "$Name.$([guid]::NewGuid().ToString('N')).tmp.html"
    try {
        Write-Host "Rebuilding $Name.html from $Source"
        & $Python @PythonArguments $ViewerScript $Source --output $Temporary --recover-truncated
        if ($LASTEXITCODE -ne 0) { throw "Viewer generation exited with code $LASTEXITCODE" }
        Move-Item -LiteralPath $Temporary -Destination $Destination -Force
        $Rebuilt += $Name
        Write-Host "Rebuilt: $Destination"
    } catch {
        $Failed += $Name
        Write-Warning "Could not rebuild ${Name}: $_"
    } finally {
        if (Test-Path -LiteralPath $Temporary -PathType Leaf) { Remove-Item -LiteralPath $Temporary }
    }
}
if ($Open -and $Rebuilt -contains "ecosystem") {
    # Explicitly requested interactive viewer.
    Start-Process -FilePath (Join-Path $RunPath "ecosystem.html")
}
if ($OpenTail -and $Rebuilt -contains "ecosystem_tail") {
    # Explicitly requested interactive viewer.
    Start-Process -FilePath (Join-Path $RunPath "ecosystem_tail.html")
}
if ($Failed.Count -gt 0) { throw "Failed to rebuild: $($Failed -join ', '). Original recordings and existing viewers were preserved." }
if ($Rebuilt.Count -eq 0) { throw "No saved replay recordings found in $RunPath" }
