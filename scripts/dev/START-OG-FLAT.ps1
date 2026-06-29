param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [switch]$NoDeploy,
    [switch]$DeployOnly,
    [switch]$ValidateOnly,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
if ([string]::IsNullOrWhiteSpace($ClientToolsRoot)) {
    $ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
}
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if ([string]::IsNullOrWhiteSpace($ClientDataRoot)) {
    $ClientDataRoot = Get-NikamiRuntimeClientRoot -WorkspaceRoot $WorkspaceRoot
}

Write-Output "=== SWG OG Flat Launcher ==="
Write-Output "This launches the deployed x64 SWG client with the DX11 renderer in flat screen mode."
Write-Output ""

$builtExe = Join-Path $ClientToolsRoot "src\compile\x64\SwgClient\Release\SwgClient_r.exe"
$builtD3d11 = Join-Path $ClientToolsRoot "src\compile\x64\Direct3d11\Release\gl05_r.dll"
$runtimeExe = Join-Path $ClientDataRoot "SwgClient_r.exe"
$runtimeD3d11 = Join-Path $ClientDataRoot "gl05_r.dll"

if (!(Test-Path -LiteralPath $ClientToolsRoot)) {
    throw "Client tools root not found: $ClientToolsRoot"
}

if (!(Test-Path -LiteralPath $ClientDataRoot)) {
    throw "Client data root not found: $ClientDataRoot"
}

if (!$NoDeploy) {
    if (!(Test-Path -LiteralPath $builtExe)) {
        throw "Built x64 client exe not found: $builtExe"
    }
    if (!(Test-Path -LiteralPath $builtD3d11)) {
        throw "Built x64 D3D11 renderer not found: $builtD3d11"
    }

    try {
        Copy-Item -LiteralPath $builtExe -Destination $runtimeExe -Force
        Copy-Item -LiteralPath $builtD3d11 -Destination $runtimeD3d11 -Force
    }
    catch {
        throw "Deploy failed. Close any running SWG client and retry. Source='$builtExe' Renderer='$builtD3d11' Destination='$ClientDataRoot'. PowerShell error: $($_.Exception.Message)"
    }
    Write-Output "Deployed latest build:"
    Write-Output "  $runtimeExe"
    Write-Output "  $runtimeD3d11"
}
else {
    Write-Output "NoDeploy set; using existing runtime files:"
    Write-Output "  $runtimeExe"
    Write-Output "  $runtimeD3d11"
}

if (!(Test-Path -LiteralPath $runtimeExe)) {
    throw "Runtime client exe not found: $runtimeExe"
}
if (!(Test-Path -LiteralPath $runtimeD3d11)) {
    throw "Runtime D3D11 renderer not found: $runtimeD3d11"
}

if ($DeployOnly -or $ValidateOnly) {
    Write-Output ""
    Write-Output "$(if ($DeployOnly) { 'DeployOnly' } else { 'ValidateOnly' }) set; not launching client."
    exit 0
}

# Ensure VR is disabled in case environment variables leaked
Remove-Item Env:SWG_OG_VR -ErrorAction SilentlyContinue
if (!$env:SWG_D3D11_PRESENT_INTERVAL) { $env:SWG_D3D11_PRESENT_INTERVAL = "1" }

Push-Location -LiteralPath $ClientDataRoot
try {
    Write-Output "Launching: $runtimeExe"
    $process = Start-Process -FilePath $runtimeExe -WorkingDirectory $ClientDataRoot -PassThru
    Write-Output "Started SwgClient_r.exe pid=$($process.Id) in flat screen mode."
    if ($Wait) {
        Wait-Process -Id $process.Id
        Write-Output "SwgClient_r.exe exited."
    }
}
finally {
    Pop-Location
}
