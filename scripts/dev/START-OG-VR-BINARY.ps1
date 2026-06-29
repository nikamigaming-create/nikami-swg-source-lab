param(
    [ValidateSet("System", "Oculus", "VirtualDesktop")]
    [string]$OpenXrRuntime = "System",
    [switch]$EnableLogs,
    [switch]$HandTrace,
    [switch]$ProofTrace,
    [switch]$PhysicsTrace,
    [switch]$HideWand,
    [switch]$TvMode,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"

$ClientRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $ClientRoot "SwgClient_r.exe"
$Renderer = Join-Path $ClientRoot "gl05_r.dll"
$MilesDll = Join-Path $ClientRoot "mss64.dll"
$VrCfg = Join-Path $ClientRoot "og_vr.cfg"
$ClientCfg = Join-Path $ClientRoot "client.cfg"
$LogRoot = Join-Path $ClientRoot "_og_vr_logs"
$ProofPath = Join-Path $LogRoot "og_vr_proof.jsonl"
$OculusRuntimeJson = "C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json"
$VirtualDesktopRuntimeJson = "C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json"

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

function Ensure-ClientCfgIncludesVrCfg {
    Assert-Exists -Path $ClientCfg -Label "client.cfg"
    $clientCfgText = Get-Content -LiteralPath $ClientCfg -Raw
    if ($clientCfgText -notmatch '(?im)^\s*\.include\s+"og_vr\.cfg"\s*$') {
        $backup = Join-Path $ClientRoot ("client.cfg.pre-og-vr-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".bak")
        Copy-Item -LiteralPath $ClientCfg -Destination $backup -Force
        Add-Content -LiteralPath $ClientCfg -Value ""
        Add-Content -LiteralPath $ClientCfg -Value "# Added by START-OG-VR.ps1. Remove this line to disable the VR override profile."
        Add-Content -LiteralPath $ClientCfg -Value '.include "og_vr.cfg"'
        Write-Output "Updated client.cfg to include og_vr.cfg"
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

function Write-VrCfg {
    @'
# og_vr.cfg - portable OG VR/DX11 runtime profile.

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
	modernShadowVrSinglePass=true
	modernShadowMaxCasters=4096
	modernShadowTerrain=true
	modernShadowCharacters=true
	modernShadowObjects=true
	legacyStencilShadowsInVr=false

[SharedUtility]
	cache=misc/cache_large.iff
'@ | Set-Content -LiteralPath $VrCfg -Encoding ASCII
}

Assert-Exists -Path $Exe -Label "SwgClient_r.exe"
Assert-Exists -Path $Renderer -Label "gl05_r.dll"
Assert-Exists -Path $MilesDll -Label "mss64.dll x64 Miles audio runtime"
Assert-X64 -Path $Exe -Label "SwgClient_r.exe"
Assert-X64 -Path $Renderer -Label "gl05_r.dll"
Assert-X64 -Path $MilesDll -Label "mss64.dll"
Ensure-BaseRuntimeFiles
Write-VrCfg
Ensure-ClientCfgIncludesVrCfg
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

$env:SWG_OG_VR = "1"
$env:SWG_D3D11_VR = "1"
$env:SWG_MSS64_PATH = $MilesDll
$env:SWG_OG_VR_FORCE_FIRST_PERSON = "1"
$env:SWG_OG_VR_OBJECT_CONTEXT_QUADS = "1"
$env:SWG_OG_VR_TARGET_WINDOW_QUADS = "1"
$env:SWG_OG_VR_OBJECT_CONTEXT_SCALE = "1.20"
$env:SWG_OG_VR_OBJECT_CONTEXT_HEIGHT_METERS = "0.70"
$env:SWG_OG_VR_OBJECT_CONTEXT_WIDTH_METERS = "1.25"
$env:SWG_OG_VR_OBJECT_CONTEXT_RADIUS_UP_SCALE = "0.90"
$env:SWG_OG_VR_OBJECT_CONTEXT_UP_METERS = "0.22"
$env:SWG_OG_VR_OBJECT_CONTEXT_HEADWARD_METERS = "0.00"
$env:SWG_OG_VR_OBJECT_CONTEXT_ANCHOR_CAMERA_LOCAL = "0"
$env:SWG_OG_VR_WRIST_DASHBOARD = "1"
$env:SWG_OG_VR_WRIST_DASHBOARD_WIDTH_METERS = "0.095"
$env:SWG_OG_VR_WRIST_DASHBOARD_HEIGHT_METERS = "0.095"
$env:SWG_OG_VR_WRIST_DASHBOARD_INNER_X_METERS = "0.035"
$env:SWG_OG_VR_WRIST_DASHBOARD_OFFSET_Y_METERS = "-0.035"
$env:SWG_OG_VR_WRIST_DASHBOARD_OFFSET_Z_METERS = "0.230"
$env:SWG_OG_VR_WRIST_DASHBOARD_ROLL_DEGREES = "0"
$env:SWG_OG_VR_HOVER_TARGET_FRAME = "1"
$env:SWG_OG_VR_MENU_BUTTON = "1"
$env:SWG_OG_VR_MENU_BUTTON_OPENS_BUTTON_BAR = "0"
$env:SWG_OG_VR_WRIST_MENU_BUTTON = "0"
$env:SWG_OG_VR_TRIGGER_SELECTS_WORLD = "0"
$env:SWG_OG_VR_PRIMARY_TRIGGER_ACTION = "1"
$env:SWG_OG_VR_PRIMARY_TRIGGER_HAND = "right"
$env:SWG_OG_VR_UI_OCCLUDES_PHYSICS = "0"
$env:SWG_OG_VR_SWAP_MOUSE_TRIGGERS = "1"
$env:SWG_OG_VR_WORLD_WAND_CURSOR_MODE = "center"
$env:SWG_OG_VR_WORLD_WAND_NATIVE_RAY = "1"
$env:SWG_OG_VR_GRIP_TARGETS_WORLD = "1"
$env:SWG_OG_VR_TARGETING_RANGE_METERS = "256"
$env:SWG_OG_VR_TARGET_RAY_BASE_RADIUS_METERS = "0.35"
$env:SWG_OG_VR_TARGET_RAY_RADIUS_PER_METER = "0.010"
$env:SWG_OG_VR_TARGET_RAY_MAX_RADIUS_METERS = "2.10"
$env:SWG_OG_VR_TARGET_CYCLE_BUTTONS = "1"
$env:SWG_OG_VR_OVERHEAD_TARGET_UI = "1"
Remove-Item Env:SWG_OG_VR_WORLD_WAND_TARGET_HAND -ErrorAction SilentlyContinue
$env:SWG_OG_VR_WORLD_RADIAL_FALLBACK = "0"
$env:SWG_OG_VR_WORLD_RECENTER_DELAY_FRAMES = "90"
$env:SWG_OG_VR_WORLD_CARRY_QUAD_RECENTER = "0"
$env:SWG_OG_VR_POINTER_VISUALS = if ($HideWand) { "0" } else { "1" }
$env:SWG_OG_VR_HANDS_HEAD_RELATIVE = "0"
$env:SWG_OG_VR_DETACHED_HANDS = "1"
$env:SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE = "0"
$env:SWG_OG_VR_DETACHED_HANDS_USE_AIM_POSE = "0"
$env:SWG_OG_VR_DETACHED_HANDS_REQUIRE_TRACKED = "0"
$env:SWG_OG_VR_DETACHED_HANDS_LEFT_YAW_DEGREES = "180"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_ROLL_DEGREES = "0"
$env:SWG_OG_VR_SHOW_PLAYER_BODY = "0"
Remove-Item Env:SWG_OG_VR_BODY_IK_FORCE -ErrorAction SilentlyContinue
Remove-Item Env:SWG_OG_VR_REAL_HANDS -ErrorAction SilentlyContinue
Remove-Item Env:SWG_OG_VR_REAL_ARMS -ErrorAction SilentlyContinue
Remove-Item Env:SWG_OG_VR_EXPERIMENTAL_LIVE_SKELETON_HANDS -ErrorAction SilentlyContinue
$env:SWG_OG_VR_WEAPON_INVENTORY = "1"
$env:SWG_OG_VR_WEAPON_INVENTORY_A_BUTTON = "1"
$env:SWG_OG_VR_MOVE_REFERENCE = "head"
$env:SWG_OG_VR_TURN_MODE = "snap"
$env:SWG_OG_VR_STEREO_SCALE = "1.00"
$env:SWG_OG_VR_HEAD_POSITION_SCALE = "1.00"
$env:SWG_OG_VR_EYE_SCALE = "1.25"
$env:SWG_OG_VR_NEAR_PLANE = "0.10"
$env:SWG_OG_VR_FIRST_PERSON_EYE_FORWARD = "0.34"
$env:SWG_OG_VR_FIRST_PERSON_EYE_UP = "0.12"
$env:SWG_OG_VR_AIM_RAY_OFFSET_X_METERS = "0.00"
$env:SWG_OG_VR_AIM_RAY_OFFSET_Y_METERS = "0.00"
$env:SWG_OG_VR_AIM_RAY_OFFSET_Z_METERS = "0.00"
$env:SWG_OG_VR_HAND_IK_OFFSET_X_METERS = "0.00"
$env:SWG_OG_VR_HAND_IK_OFFSET_Y_METERS = "0.00"
$env:SWG_OG_VR_HAND_IK_OFFSET_Z_METERS = "0.00"
$env:SWG_OG_VR_LEFT_HAND_IK_OFFSET_X_METERS = "0.00"
$env:SWG_OG_VR_LEFT_HAND_IK_OFFSET_Y_METERS = "0.00"
$env:SWG_OG_VR_LEFT_HAND_IK_OFFSET_Z_METERS = "0.00"
$env:SWG_OG_VR_RIGHT_HAND_IK_OFFSET_X_METERS = "0.00"
$env:SWG_OG_VR_RIGHT_HAND_IK_OFFSET_Y_METERS = "0.00"
$env:SWG_OG_VR_RIGHT_HAND_IK_OFFSET_Z_METERS = "0.00"
$env:SWG_OG_VR_FB_LEGS = "0"
$env:SWG_D3D11_PRESENT_INTERVAL = "0"
$env:SWG_OG_VR_PHYSICS_ALLOW_STATIC_SETDRESSING = "1"
$env:SWG_OG_VR_PHYSICS_REQUIRE_TRANSFERABLE = "0"
$env:SWG_OG_VR_TV_MODE = if ($TvMode) { "1" } else { "0" }

switch ($OpenXrRuntime) {
    "Oculus" { if (Test-Path -LiteralPath $OculusRuntimeJson) { $env:XR_RUNTIME_JSON = $OculusRuntimeJson } }
    "VirtualDesktop" { if (Test-Path -LiteralPath $VirtualDesktopRuntimeJson) { $env:XR_RUNTIME_JSON = $VirtualDesktopRuntimeJson } }
    default { Remove-Item Env:XR_RUNTIME_JSON -ErrorAction SilentlyContinue }
}

if ($EnableLogs) {
    $env:SWG_LOG_FILE = Join-Path $LogRoot "og_vr_client.log"
    $env:SWG_D3D11_DIAGNOSTICS = "1"
} else {
    Remove-Item Env:SWG_LOG_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:SWG_D3D11_DIAGNOSTICS -ErrorAction SilentlyContinue
}
if ($HandTrace) {
    $env:SWG_OG_VR_HAND_TRACE = "1"
    $env:SWG_OG_VR_HAND_TRACE_FILE = Join-Path $LogRoot "og_vr_hands_trace.log"
} else {
    Remove-Item Env:SWG_OG_VR_HAND_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:SWG_OG_VR_HAND_TRACE_FILE -ErrorAction SilentlyContinue
}
if ($ProofTrace) {
    Remove-Item -LiteralPath $ProofPath -Force -ErrorAction SilentlyContinue
    $env:SWG_OG_VR_PROOF = $ProofPath
} else {
    Remove-Item Env:SWG_OG_VR_PROOF -ErrorAction SilentlyContinue
}
if ($PhysicsTrace) {
    $env:SWG_OG_VR_PHYSICS_TRACE = "1"
    $env:SWG_OG_VR_PHYSICS_TRACE_FILE = Join-Path $LogRoot "og_vr_physics_trace.log"
} else {
    Remove-Item Env:SWG_OG_VR_PHYSICS_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:SWG_OG_VR_PHYSICS_TRACE_FILE -ErrorAction SilentlyContinue
}

Write-Output "Launching OG VR from $ClientRoot"
Write-Output "OpenXR runtime mode: $OpenXrRuntime"
Write-Output "Audio: x64 Miles runtime $MilesDll"
Write-Output "Hands: detached, valid-pose gated, body hidden, full-body IK not forced"
if ($ProofTrace) { Write-Output "Proof trace: $ProofPath" }
Start-Process -FilePath $Exe -WorkingDirectory $ClientRoot

if ($Wait) {
    Write-Output "Press Enter to close this launcher."
    [void][Console]::ReadLine()
}
