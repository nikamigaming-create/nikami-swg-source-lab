param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [string]$DxvkRoot = "",
    [switch]$NoDeploy,
    [switch]$DeployOnly,
    [switch]$Wait,
    [string]$AutoCaptureBase = "",
    [int]$AutoCapturePresent = 9000,
    [int]$AutoCaptureMax = 40,
    [int]$AutoCaptureInterval = 500,
    [int]$PresentInterval = 0,
    [switch]$Hud
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
if ([string]::IsNullOrWhiteSpace($DxvkRoot)) {
    $DxvkRoot = Join-Path $WorkspaceRoot "tools\dxvk\dxvk-2.7.1"
}

Write-Output "=== SWG OG DXVK Flat Launcher ==="
Write-Output "This launches the x64 OG client with the DX11 renderer translated to Vulkan by DXVK."
Write-Output ""

$builtExe = Join-Path $ClientToolsRoot "src\compile\x64\SwgClient\Release\SwgClient_r.exe"
$builtD3d11 = Join-Path $ClientToolsRoot "src\compile\x64\Direct3d11\Release\gl05_r.dll"
$runtimeExe = Join-Path $ClientDataRoot "SwgClient_r.exe"
$runtimeD3d11 = Join-Path $ClientDataRoot "gl05_r.dll"
$dxvkBin = Join-Path $DxvkRoot "x64"
$dxvkD3d11 = Join-Path $dxvkBin "d3d11.dll"
$dxvkDxgi = Join-Path $dxvkBin "dxgi.dll"
$runtimeDxvkD3d11 = Join-Path $ClientDataRoot "d3d11.dll"
$runtimeDxvkDxgi = Join-Path $ClientDataRoot "dxgi.dll"

if (!(Test-Path -LiteralPath $ClientToolsRoot)) { throw "Client tools root not found: $ClientToolsRoot" }
if (!(Test-Path -LiteralPath $ClientDataRoot)) { throw "Client data root not found: $ClientDataRoot" }
if (!(Test-Path -LiteralPath $dxvkD3d11)) { throw "DXVK d3d11.dll not found: $dxvkD3d11" }
if (!(Test-Path -LiteralPath $dxvkDxgi)) { throw "DXVK dxgi.dll not found: $dxvkDxgi" }

if (!$NoDeploy) {
    if (!(Test-Path -LiteralPath $builtExe)) { throw "Built x64 client exe not found: $builtExe" }
    if (!(Test-Path -LiteralPath $builtD3d11)) { throw "Built x64 D3D11 renderer not found: $builtD3d11" }

    try {
        Copy-Item -LiteralPath $builtExe -Destination $runtimeExe -Force
        Copy-Item -LiteralPath $builtD3d11 -Destination $runtimeD3d11 -Force
    }
    catch {
        throw "Deploy failed. Close any running SWG client and retry. PowerShell error: $($_.Exception.Message)"
    }
    Write-Output "Deployed latest DX11 runtime:"
    Write-Output "  $runtimeExe"
    Write-Output "  $runtimeD3d11"
}
else {
    Write-Output "NoDeploy set; using existing runtime files:"
    Write-Output "  $runtimeExe"
    Write-Output "  $runtimeD3d11"
}

Copy-Item -LiteralPath $dxvkD3d11 -Destination $runtimeDxvkD3d11 -Force
Copy-Item -LiteralPath $dxvkDxgi -Destination $runtimeDxvkDxgi -Force
Write-Output "Installed DXVK beside the client:"
Write-Output "  $runtimeDxvkD3d11"
Write-Output "  $runtimeDxvkDxgi"

if ($DeployOnly) {
    Write-Output ""
    Write-Output "DeployOnly set; not launching client."
    exit 0
}

Remove-Item Env:SWG_OG_VR -ErrorAction SilentlyContinue
$env:SWG_D3D11_PRESENT_INTERVAL = "$PresentInterval"
Write-Output "D3D11 present interval: $PresentInterval"
if ($AutoCaptureBase.Length -gt 0) {
    $env:SWG_D3D11_AUTOCAPTURE = $AutoCaptureBase
    $env:SWG_D3D11_AUTOCAPTURE_PRESENT = "$AutoCapturePresent"
    $env:SWG_RENDERER_AUTOCAPTURE_MAX = "$AutoCaptureMax"
    $env:SWG_RENDERER_AUTOCAPTURE_INTERVAL = "$AutoCaptureInterval"
    Write-Output "DX11 auto-capture base: $AutoCaptureBase"
}
if ($Hud) {
    $env:DXVK_HUD = "api,fps"
}
else {
    Remove-Item Env:DXVK_HUD -ErrorAction SilentlyContinue
}

Push-Location -LiteralPath $ClientDataRoot
try {
    Write-Output "Launching: $runtimeExe"
    $process = Start-Process -FilePath $runtimeExe -WorkingDirectory $ClientDataRoot -PassThru
    Write-Output "Started SwgClient_r.exe pid=$($process.Id) in DXVK flat mode."
    if ($Wait) {
        Wait-Process -Id $process.Id
        Write-Output "SwgClient_r.exe exited."
    }
}
finally {
    Pop-Location
    if ($AutoCaptureBase.Length -gt 0) {
        Remove-Item Env:SWG_D3D11_AUTOCAPTURE -ErrorAction SilentlyContinue
        Remove-Item Env:SWG_D3D11_AUTOCAPTURE_PRESENT -ErrorAction SilentlyContinue
        Remove-Item Env:SWG_RENDERER_AUTOCAPTURE_MAX -ErrorAction SilentlyContinue
        Remove-Item Env:SWG_RENDERER_AUTOCAPTURE_INTERVAL -ErrorAction SilentlyContinue
    }
    Remove-Item Env:SWG_D3D11_PRESENT_INTERVAL -ErrorAction SilentlyContinue
    if ($Hud) {
        Remove-Item Env:DXVK_HUD -ErrorAction SilentlyContinue
    }
}
