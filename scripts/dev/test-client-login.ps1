param(
    [int]$Seconds = 20,
    [string]$ProofDir = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProofDir)) {
    $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    . (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
    $ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
    $WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
    $ProofDir = Join-Path (Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot) "d_clean_benchmark"
}

& (Join-Path $PSScriptRoot "run-client.ps1") `
    -Mode Benchmark `
    -BenchmarkSeconds $Seconds `
    -MinAverageFps 0 `
    -MinMinimumFps 0 `
    -ProofDir $ProofDir
exit $LASTEXITCODE
