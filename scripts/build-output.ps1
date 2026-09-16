# Keep routine build progress quiet without hiding diagnostics.
function Invoke-BuildCommand {
    param([string]$Command, [string[]]$Arguments, [switch]$Detailed)
    $Output = @(& $Command @Arguments 2>&1)
    $ExitCode = $LASTEXITCODE
    if ($Detailed -or $ExitCode -ne 0 -or ($Output -match "(?i)warning|error")) {
        $Output | ForEach-Object { Write-Host $_ }
    }
    if ($ExitCode -ne 0) { throw "$Command failed (exit $ExitCode)" }
}
