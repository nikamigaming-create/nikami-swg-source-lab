param(
    [switch]$EnableLogs,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"

$ClientRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $ClientRoot "SwgClient_r.exe"
$Renderer = Join-Path $ClientRoot "gl05_r.dll"
$MilesDll = Join-Path $ClientRoot "mss64.dll"
$FlatCfg = Join-Path $ClientRoot "og_flat.cfg"
$ClientCfg = Join-Path $ClientRoot "client.cfg"
$LogRoot = Join-Path $ClientRoot "_og_flat_logs"

function Assert-Exists {
    param([string]$Path, [string]$Label)
    if (!(Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Get-PeMachine {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40) { return "<invalid>" }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or ($peOffset + 6) -ge $bytes.Length) { return "<invalid>" }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    switch ($machine) {
        0x8664 { "AMD64" }
        0x14c { "I386" }
        default { "0x{0:X4}" -f $machine }
    }
}

function Assert-X64 {
    param([string]$Path, [string]$Label)
    $machine = Get-PeMachine -Path $Path
    if ($machine -ne "AMD64") {
        throw "$Label must be x64/AMD64, got $machine at $Path"
    }
}

function Ensure-ClientCfgIncludesFlatCfg {
    Assert-Exists -Path $ClientCfg -Label "client.cfg"
    $clientCfgText = Get-Content -LiteralPath $ClientCfg -Raw
    if ($clientCfgText -notmatch '(?im)^\s*\.include\s+"og_flat\.cfg"\s*$') {
        $backup = Join-Path $ClientRoot ("client.cfg.pre-og-flat-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".bak")
        Copy-Item -LiteralPath $ClientCfg -Destination $backup -Force
        Add-Content -LiteralPath $ClientCfg -Value ""
        Add-Content -LiteralPath $ClientCfg -Value "# Added by START-OG-FLAT.ps1. Remove this line to disable the flat DX11 override profile."
        Add-Content -LiteralPath $ClientCfg -Value '.include "og_flat.cfg"'
        Write-Output "Updated client.cfg to include og_flat.cfg"
        Write-Output "Backup: $backup"
    }
}

function Ensure-BaseRuntimeFiles {
    $stubs = @{
        "login.cfg" = @'
[ClientGame]
# Default local-dev login target. Replace these values with the server
# address, account name, and password supplied by whoever is hosting.
loginServerAddress0=127.0.0.1
loginServerPort0=44453
loginClientID=test
loginClientPassword=test
'@
        "live.cfg" = @'
[SharedFile]
	maxSearchPriority=12
	searchTree_00_0=extracted_slot_def
	searchTree_00_8=swgsource_3.0.tre

[SwgClient]
	allowMultipleInstances=true
'@
        "options.cfg" = @'
[ClientGame]
	skipIntro=1
	disableCutScenes=1
	skipSplash=1
	cameraFarPlane=16384
	cameraFarPlaneSpace=32768

[ClientGraphics]
	useHardwareMouseCursor=1
	windowed=true
	useSafeRenderer=1
	rasterMajor=5

[SharedUtility]
	cache=misc/cache_large.iff
'@
        "preload.cfg" = @'
[PreloadedAssets]
'@
        "user.cfg" = @'
[SwgClient]
	allowMultipleInstances=true

[Station]
	gameFeatures=0xffffffff
	subscriptionFeatures=0x01
'@
    }

    foreach ($entry in $stubs.GetEnumerator()) {
        $path = Join-Path $ClientRoot $entry.Key
        if (!(Test-Path -LiteralPath $path)) {
            $entry.Value | Set-Content -LiteralPath $path -Encoding ASCII
            Write-Output "Created missing runtime config: $($entry.Key)"
        }
    }
}

function Write-FlatCfg {
    @'
# og_flat.cfg - portable OG flat-screen/DX11 runtime profile.

[SwgClient]
	allowMultipleInstances=true

[ClientAudio]
	enabled=true
	masterVolume=1.0
	soundEffectVolume=1.0
	backGroundMusicVolume=1.0
	playerMusicVolume=1.0
	userInterfaceVolume=1.0
	ambientEffectVolume=1.0
	soundProvider="5.1 Speakers"

[ClientGame]
	skipIntro=1
	disableCutScenes=1
	skipSplash=1
	cameraFarPlane=16384
	cameraFarPlaneSpace=32768

[ClientGraphics]
	useHardwareMouseCursor=1
	windowed=true
	useSafeRenderer=1
	rasterMajor=5
	disableOcclusionCulling=true
	dpvsMinimumObjectWidth=0.0
	dpvsMinimumObjectHeight=0.0
	dpvsMinimumObjectOpacity=0.0
	dpvsImageScale=1.0
	discardHighestMipMapLevels=0
	discardHighestNormalMipMapLevels=0
	loadAllAssetsRegardlessOfShaderCapability=true

[Direct3d11]
	modernShadows=true
	modernShadowQuality=4
	modernShadowCascadeCount=4
	modernShadowMapSize=4096
	modernShadowDistance=512.0
	modernShadowSplitLambda=0.85
	modernShadowStabilize=true
	modernShadowFilter=2
	modernShadowPcfTaps=32
	modernShadowFilterRadius=2.50
	modernShadowDepthBias=0.0008
	modernShadowSlopeBias=2.0
	modernShadowNormalBias=0.05
	modernShadowFadeStart=0.85
	modernShadowContactShadows=true
	modernShadowContactDistance=12.0
	modernShadowVrSinglePass=false
	modernShadowMaxCasters=4096
	modernShadowTerrain=true
	modernShadowCharacters=true
	modernShadowObjects=true

[SharedUtility]
	cache=misc/cache_large.iff
'@ | Set-Content -LiteralPath $FlatCfg -Encoding ASCII
}

Assert-Exists -Path $Exe -Label "SwgClient_r.exe"
Assert-Exists -Path $Renderer -Label "gl05_r.dll"
Assert-Exists -Path $MilesDll -Label "mss64.dll x64 Miles audio runtime"
Assert-X64 -Path $Exe -Label "SwgClient_r.exe"
Assert-X64 -Path $Renderer -Label "gl05_r.dll"
Assert-X64 -Path $MilesDll -Label "mss64.dll"
Ensure-BaseRuntimeFiles
Write-FlatCfg
Ensure-ClientCfgIncludesFlatCfg
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

Remove-Item Env:SWG_OG_VR -ErrorAction SilentlyContinue
Remove-Item Env:SWG_D3D11_VR -ErrorAction SilentlyContinue
Remove-Item Env:SWG_OG_VR_FORCE_FIRST_PERSON -ErrorAction SilentlyContinue
Remove-Item Env:SWG_OG_VR_BODY_IK_FORCE -ErrorAction SilentlyContinue
Remove-Item Env:SWG_OG_VR_REAL_HANDS -ErrorAction SilentlyContinue
Remove-Item Env:SWG_OG_VR_REAL_ARMS -ErrorAction SilentlyContinue
Remove-Item Env:SWG_OG_VR_EXPERIMENTAL_LIVE_SKELETON_HANDS -ErrorAction SilentlyContinue
Remove-Item Env:XR_RUNTIME_JSON -ErrorAction SilentlyContinue
$env:SWG_MSS64_PATH = $MilesDll
$env:SWG_D3D11_PRESENT_INTERVAL = "1"

if ($EnableLogs) {
    $env:SWG_LOG_FILE = Join-Path $LogRoot "og_flat_client.log"
    $env:SWG_D3D11_DIAGNOSTICS = "1"
} else {
    Remove-Item Env:SWG_LOG_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:SWG_D3D11_DIAGNOSTICS -ErrorAction SilentlyContinue
}

Write-Output "Launching OG flat-screen DX11 from $ClientRoot"
Write-Output "VR environment disabled for this process."
Write-Output "Audio: x64 Miles runtime $MilesDll"
$process = Start-Process -FilePath $Exe -WorkingDirectory $ClientRoot -PassThru
Write-Output "Started SwgClient_r.exe pid=$($process.Id) in flat-screen mode."

if ($Wait) {
    Wait-Process -Id $process.Id
    Write-Output "SwgClient_r.exe exited."
}
