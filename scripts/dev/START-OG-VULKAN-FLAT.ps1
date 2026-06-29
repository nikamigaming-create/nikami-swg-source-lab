param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [switch]$NoDeploy,
    [switch]$DeployOnly,
    [switch]$ValidateOnly,
    [switch]$RendererOnly,
    [int]$WaitForUnlockSeconds = 0,
    [switch]$Wait,
    [switch]$AutoSelectAvatar,
    [string]$AutoCaptureBase = ""
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
if ([string]::IsNullOrWhiteSpace($AutoCaptureBase)) {
    $AutoCaptureBase = Join-Path (Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot) "og_vulkan_flat_autocapture"
}

Write-Output "=== SWG OG Vulkan Flat Launcher ==="
Write-Output "This launches the deployed x64 SWG client with the Vulkan renderer in flat screen mode."
Write-Output ""

$builtExe = Join-Path $ClientToolsRoot "src\compile\x64\SwgClient\Release\SwgClient_r.exe"
$builtVulkan = Join-Path $ClientToolsRoot "src\compile\x64\Vulkan\Release\gl05_r.dll"
$runtimeExe = Join-Path $ClientDataRoot "SwgClient_r.exe"
$runtimeRenderer = Join-Path $ClientDataRoot "gl05_r.dll"

if (!(Test-Path -LiteralPath $ClientToolsRoot)) {
    throw "Client tools root not found: $ClientToolsRoot"
}

if (!(Test-Path -LiteralPath $ClientDataRoot)) {
    throw "Client data root not found: $ClientDataRoot"
}

function Copy-IfChanged {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination,
        [Parameter(Mandatory=$true)][string]$Label
    )

    if ((Test-Path -LiteralPath $Destination)) {
        $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
        if ($sourceHash -eq $destinationHash) {
            Write-Output "  $Label unchanged; keeping existing runtime file."
            return
        }
    }

    if ($WaitForUnlockSeconds -gt 0) {
        $deadline = (Get-Date).AddSeconds($WaitForUnlockSeconds)
        while ($true) {
            try {
                Copy-Item -LiteralPath $Source -Destination $Destination -Force
                Write-Output "  $Label copied: $Destination"
                return
            }
            catch {
                if ((Get-Date) -ge $deadline) {
                    throw
                }
                Write-Output "  $Label is locked; waiting for unlock..."
                Start-Sleep -Seconds 2
            }
        }
    }

    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    Write-Output "  $Label copied: $Destination"
}

if (!$NoDeploy) {
    if (!$RendererOnly -and !(Test-Path -LiteralPath $builtExe)) {
        throw "Built x64 client exe not found: $builtExe"
    }
    if (!(Test-Path -LiteralPath $builtVulkan)) {
        throw "Built x64 Vulkan renderer not found: $builtVulkan"
    }

    try {
        if (!$RendererOnly) {
            Copy-IfChanged -Source $builtExe -Destination $runtimeExe -Label "SwgClient_r.exe"
        }
        else {
            Write-Output "  RendererOnly set; skipping client exe deploy."
        }
        Copy-IfChanged -Source $builtVulkan -Destination $runtimeRenderer -Label "gl05_r.dll"
    }
    catch {
        throw "Deploy failed. Close any running SWG client and retry. Source='$builtExe' Renderer='$builtVulkan' Destination='$ClientDataRoot'. PowerShell error: $($_.Exception.Message)"
    }
    Write-Output "Deployed latest Vulkan flat build:"
    Write-Output "  $runtimeExe"
    Write-Output "  $runtimeRenderer"
}
else {
    Write-Output "NoDeploy set; using existing runtime files:"
    Write-Output "  $runtimeExe"
    Write-Output "  $runtimeRenderer"
}

if (!(Test-Path -LiteralPath $runtimeExe)) {
    throw "Runtime client exe not found: $runtimeExe"
}
if (!(Test-Path -LiteralPath $runtimeRenderer)) {
    throw "Runtime Vulkan renderer not found: $runtimeRenderer"
}

if (Test-Path -LiteralPath (Join-Path $ClientDataRoot "user.cfg")) {
    $userCfg = Join-Path $ClientDataRoot "user.cfg"
    $userCfgText = Get-Content -LiteralPath $userCfg
    $userCfgText = $userCfgText -replace '^\s*meshShadows=.*$', "`tmeshShadows=false"
    $userCfgText = $userCfgText -replace '^\s*skeletalShadows=.*$', "`tskeletalShadows=1"
    Set-Content -LiteralPath $userCfg -Value $userCfgText
    Write-Output "Vulkan safety profile applied: mesh shadow volumes disabled, simple skeletal shadows enabled."
}

if ($DeployOnly -or $ValidateOnly) {
    Write-Output ""
    Write-Output "$(if ($DeployOnly) { 'DeployOnly' } else { 'ValidateOnly' }) set; not launching client."
    exit 0
}

# Keep this launcher flat-only until the Vulkan backend has world/UI parity.
Remove-Item Env:SWG_OG_VR -ErrorAction SilentlyContinue
Remove-Item Env:SWG_D3D11_VR -ErrorAction SilentlyContinue
if (!$env:SWG_VULKAN_GPU_PRESENT) { $env:SWG_VULKAN_GPU_PRESENT = "1" }
if (!$env:SWG_VULKAN_TEXTURE_MIPS) { $env:SWG_VULKAN_TEXTURE_MIPS = "1" }
if (!$env:SWG_VULKAN_TEXTURE_ANISO) { $env:SWG_VULKAN_TEXTURE_ANISO = "1" }
if (!$env:SWG_VULKAN_TEXTURE_ANISO_LEVEL) { $env:SWG_VULKAN_TEXTURE_ANISO_LEVEL = "8" }
if (!$env:SWG_VULKAN_TEXTURE_UPLOADS_PER_PRESENT) { $env:SWG_VULKAN_TEXTURE_UPLOADS_PER_PRESENT = "16" }
if (!$env:SWG_VULKAN_TEXTURE_UPLOAD_MB_PER_PRESENT) { $env:SWG_VULKAN_TEXTURE_UPLOAD_MB_PER_PRESENT = "32" }
if (!$env:SWG_VULKAN_VSYNC) { $env:SWG_VULKAN_VSYNC = "1" }
if (!$env:SWG_VULKAN_ACTOR_AUX_COLOR) { $env:SWG_VULKAN_ACTOR_AUX_COLOR = "1" }
if (!$env:SWG_VULKAN_ACTOR_AUX_ALPHA) { $env:SWG_VULKAN_ACTOR_AUX_ALPHA = "0.10" }
if (!$env:SWG_VULKAN_DROP_TERRAIN_SKY_TRI) { $env:SWG_VULKAN_DROP_TERRAIN_SKY_TRI = "1" }
if (!$env:SWG_VULKAN_DROP_BLACK_SKYBOX) { $env:SWG_VULKAN_DROP_BLACK_SKYBOX = "1" }
if (!$env:SWG_VULKAN_TERRAIN_FRUSTUM_CLIP) { $env:SWG_VULKAN_TERRAIN_FRUSTUM_CLIP = "1" }
if (!$env:SWG_VULKAN_ATMOSPHERE_FRUSTUM_CLIP) { $env:SWG_VULKAN_ATMOSPHERE_FRUSTUM_CLIP = "1" }
if (!$env:SWG_VULKAN_ATMOSPHERE_NEAR_REJECT) { $env:SWG_VULKAN_ATMOSPHERE_NEAR_REJECT = "1" }
if ($AutoSelectAvatar) {
    $env:SWG_OG_AUTO_SELECT_AVATAR = "1"
    Write-Output "Internal avatar auto-select enabled."
}
if ($AutoCaptureBase) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $AutoCaptureBase) | Out-Null
    $env:SWG_VULKAN_AUTOCAPTURE = $AutoCaptureBase
    $env:SWG_VULKAN_AUTOCAPTURE_PRESENT = "45"
    $env:SWG_RENDERER_AUTOCAPTURE_MAX = "40"
    $env:SWG_RENDERER_AUTOCAPTURE_INTERVAL = "45"
    Write-Output "Vulkan auto-capture base: $AutoCaptureBase"
}

Push-Location -LiteralPath $ClientDataRoot
try {
    Write-Output "Launching: $runtimeExe"
    $process = Start-Process -FilePath $runtimeExe -WorkingDirectory $ClientDataRoot -PassThru
    Write-Output "Started SwgClient_r.exe pid=$($process.Id) with the Vulkan flat renderer."
    if ($Wait) {
        Wait-Process -Id $process.Id
        Write-Output "SwgClient_r.exe exited."
    }
}
finally {
    Pop-Location
}
