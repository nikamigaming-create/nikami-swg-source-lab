param(
    [ValidateSet("Live", "Benchmark")]
    [string]$Mode = "Live",
    [string]$Configuration = "Release",
    [string]$ClientDataRoot = "",
    [string]$ServerHost = "127.0.0.1",
    [int]$Port = 44453,
    [string]$User = "2074824509",
    [string]$Password = "test",
    [int]$BenchmarkSeconds = 20,
    [double]$MinAverageFps = 0.0,
    [double]$MinMinimumFps = 0.0,
    [string]$ProofDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
$ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if ([string]::IsNullOrWhiteSpace($ClientDataRoot)) {
    $ClientDataRoot = Get-NikamiRuntimeClientRoot -WorkspaceRoot $WorkspaceRoot
}
if ([string]::IsNullOrWhiteSpace($ProofDir)) {
    $ProofDir = Join-Path (Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot) "manual_run"
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Exe = Join-Path $Root "modern-client\build\$Configuration\swg_modern_client.exe"
if (!(Test-Path -LiteralPath $Exe)) {
    & (Join-Path $PSScriptRoot "build-client.ps1") -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

New-Item -ItemType Directory -Force -Path $ProofDir | Out-Null
$LogPath = Join-Path $ProofDir (($Mode.ToLowerInvariant()) + ".log")
$env:SWG_LOG_FILE = $LogPath

$args = @($ClientDataRoot, $ServerHost, "$Port", $User, $Password)

if ($Mode -eq "Benchmark") {
    $JsonPath = Join-Path $ProofDir "benchmark_login.json"
    $args += @(
        "--benchmark-login",
        "--benchmark-output=$JsonPath",
        "--benchmark-login-timeout=90",
        "--benchmark-seconds=$BenchmarkSeconds",
        "--benchmark-min-live-objects=1",
        "--benchmark-min-live-rendered=1",
        "--benchmark-min-fps-avg=$MinAverageFps",
        "--benchmark-min-fps-min=$MinMinimumFps"
    )
}

Push-Location -LiteralPath (Join-Path $Root "modern-client")
try {
    & $Exe @args
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
