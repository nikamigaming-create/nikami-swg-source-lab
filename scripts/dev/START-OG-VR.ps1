param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [string]$ProofRoot = "",
    [ValidateSet("Oculus", "VirtualDesktop", "System")]
    [string]$OpenXrRuntime = "Oculus",
    [switch]$NoDeploy,
    [switch]$EnableLogs,
    [switch]$PhysicsTrace,
    [switch]$HandTrace,
    [switch]$ProofTrace,
    [switch]$BindSystemMenuButton,
    [switch]$TvMode,
    [switch]$HideWand,
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
if ([string]::IsNullOrWhiteSpace($ProofRoot)) {
    $ProofRoot = Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot
}

Write-Output "=== SWG OG VR Launcher ==="
Write-Output "This launches the deployed x64 SWG client with the DX11/OpenXR VR bridge enabled."
Write-Output ""

$builtExe = Join-Path $ClientToolsRoot "src\compile\x64\SwgClient\Release\SwgClient_r.exe"
$builtD3d11 = Join-Path $ClientToolsRoot "src\compile\x64\Direct3d11\Release\gl05_r.dll"
$runtimeExe = Join-Path $ClientDataRoot "SwgClient_r.exe"
$runtimeD3d11 = Join-Path $ClientDataRoot "gl05_r.dll"
$proofPath = Join-Path $ProofRoot "og_vr_start.log"
$clientLogPath = Join-Path $ProofRoot "og_vr_client.log"
$physicsTracePath = Join-Path $ProofRoot "og_vr_physics_trace.log"
$handTracePath = Join-Path $ProofRoot "og_vr_hands_trace.log"
$oculusRuntimeJson = "C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json"
$virtualDesktopRuntimeJson = "C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json"
$oculusOpenXrLoader = "C:\Program Files\Oculus\Software\Software\stress-level-zero-inc-bonelab\BONELAB_Oculus_Windows64_Data\Plugins\x86_64\openxr_loader.dll"
$handBuildMarker = "SWGVRBodyIKProof"
$rendererBuildMarker = "rendererHandLayersHardDisabled_noFallback"

function Assert-BinaryContainsMarker {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (!(Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }

    if (!(Select-String -Path $Path -Pattern $Marker -SimpleMatch -Quiet)) {
        throw "$Label does not contain required build marker '$Marker': $Path"
    }
}

function Get-PeMachine {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        return "<missing>"
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40) {
        return "<invalid>"
    }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or ($peOffset + 6) -ge $bytes.Length) {
        return "<invalid>"
    }

    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    switch ($machine) {
        0x8664 { return "AMD64" }
        0x14c { return "I386" }
        default { return ("0x{0:X4}" -f $machine) }
    }
}

function Assert-PeMachineX64 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $machine = Get-PeMachine -Path $Path
    if ($machine -ne "AMD64") {
        throw "$Label must be x64 AMD64, got $($machine): $Path"
    }
}

if (!(Test-Path -LiteralPath $ClientToolsRoot)) {
    throw "Client tools root not found: $ClientToolsRoot"
}

if (!(Test-Path -LiteralPath $ClientDataRoot)) {
    throw "Client data root not found: $ClientDataRoot"
}

New-Item -ItemType Directory -Force -Path $ProofRoot | Out-Null

$deployRequested = !$NoDeploy -and !$ValidateOnly

if ($deployRequested) {
    if (!(Test-Path -LiteralPath $builtExe)) {
        throw "Built x64 client exe not found: $builtExe"
    }
    if (!(Test-Path -LiteralPath $builtD3d11)) {
        throw "Built x64 D3D11 renderer not found: $builtD3d11"
    }
    Assert-PeMachineX64 -Path $builtExe -Label "Built client exe"
    Assert-PeMachineX64 -Path $builtD3d11 -Label "Built D3D11 renderer"
    Assert-BinaryContainsMarker -Path $builtExe -Marker $handBuildMarker -Label "Built x64 client exe"
    Assert-BinaryContainsMarker -Path $builtD3d11 -Marker $rendererBuildMarker -Label "Built x64 D3D11 renderer"

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
    Write-Output "$(if ($ValidateOnly) { 'ValidateOnly set; not deploying' } else { 'NoDeploy set' }); using existing runtime files:"
    Write-Output "  $runtimeExe"
    Write-Output "  $runtimeD3d11"
}

if (!(Test-Path -LiteralPath $runtimeExe)) {
    throw "Runtime client exe not found: $runtimeExe"
}
if (!(Test-Path -LiteralPath $runtimeD3d11)) {
    throw "Runtime D3D11 renderer not found: $runtimeD3d11"
}
Assert-PeMachineX64 -Path $runtimeExe -Label "Runtime client exe"
Assert-PeMachineX64 -Path $runtimeD3d11 -Label "Runtime D3D11 renderer"
Assert-BinaryContainsMarker -Path $runtimeExe -Marker $handBuildMarker -Label "Runtime client exe"
Assert-BinaryContainsMarker -Path $runtimeD3d11 -Marker $rendererBuildMarker -Label "Runtime D3D11 renderer"

if ($ProofTrace -and (Test-Path -LiteralPath $proofPath)) {
    Remove-Item -LiteralPath $proofPath -Force
}

if ($EnableLogs -and (Test-Path -LiteralPath $clientLogPath)) {
    Remove-Item -LiteralPath $clientLogPath -Force
}

if ($PhysicsTrace -and (Test-Path -LiteralPath $physicsTracePath)) {
    Remove-Item -LiteralPath $physicsTracePath -Force
}

if ($HandTrace -and (Test-Path -LiteralPath $handTracePath)) {
    Remove-Item -LiteralPath $handTracePath -Force
}

$selectedRuntimeName = $OpenXrRuntime
$selectedRuntimeJson = $oculusRuntimeJson
if ($OpenXrRuntime -eq "VirtualDesktop") {
    $selectedRuntimeJson = $virtualDesktopRuntimeJson
}
elseif ($OpenXrRuntime -eq "System") {
    $selectedRuntimeName = "System"
    $selectedRuntimeJson = ""
}

if ($selectedRuntimeJson -and !(Test-Path -LiteralPath $selectedRuntimeJson)) {
    throw "Selected OpenXR runtime '$OpenXrRuntime' manifest not found: $selectedRuntimeJson"
}

$env:SWG_OG_VR = "1"
if ($selectedRuntimeJson) {
    $env:XR_RUNTIME_JSON = $selectedRuntimeJson
}
else {
    Remove-Item Env:XR_RUNTIME_JSON -ErrorAction SilentlyContinue
}
$env:SWG_OPENXR_LOADER = $oculusOpenXrLoader
$env:SWG_OG_VR_FORCE_FIRST_PERSON = "1"
$env:SWG_OG_VR_STOCK_HUD_QUAD = "0"
$env:SWG_OG_VR_OBJECT_CONTEXT_QUADS = "1"
$env:SWG_OG_VR_TARGET_WINDOW_QUADS = "1"
$env:SWG_OG_VR_OBJECT_CONTEXT_DIRECT_FALLBACK = "0"
$env:SWG_OG_VR_OBJECT_CONTEXT_TEXTURE_WIDTH = "1920"
$env:SWG_OG_VR_OBJECT_CONTEXT_TEXTURE_HEIGHT = "1080"
$env:SWG_OG_VR_SELECTED_MOB_RENDER_MIN_SCALE = "3.40"
$env:SWG_OG_VR_SELECTED_MOB_RENDER_MAX_SCALE = "8.00"
$env:SWG_OG_VR_OBJECT_CONTEXT_SCALE = "2.20"
$env:SWG_OG_VR_OBJECT_CONTEXT_HEIGHT_METERS = "1.55"
$env:SWG_OG_VR_OBJECT_CONTEXT_WIDTH_METERS = "3.10"
$env:SWG_OG_VR_OBJECT_CONTEXT_RADIUS_UP_SCALE = "0.90"
$env:SWG_OG_VR_OBJECT_CONTEXT_UP_METERS = "0.62"
$env:SWG_OG_VR_OBJECT_CONTEXT_HEADWARD_METERS = "0.00"
$env:SWG_OG_VR_OBJECT_CONTEXT_ANCHOR_CAMERA_LOCAL = "0"
$env:SWG_OG_VR_WRIST_DASHBOARD = "1"
$env:SWG_OG_VR_WRIST_DASHBOARD_TEXTURE_WIDTH = "2048"
$env:SWG_OG_VR_WRIST_DASHBOARD_TEXTURE_HEIGHT = "1024"
$env:SWG_OG_VR_WRIST_DASHBOARD_WIDTH_METERS = "0.520"
$env:SWG_OG_VR_WRIST_DASHBOARD_HEIGHT_METERS = "0.520"
$env:SWG_OG_VR_WRIST_DASHBOARD_INNER_X_METERS = "0.055"
$env:SWG_OG_VR_WRIST_DASHBOARD_OFFSET_Y_METERS = "0.160"
$env:SWG_OG_VR_WRIST_DASHBOARD_OFFSET_Z_METERS = "0.070"
$env:SWG_OG_VR_WRIST_DASHBOARD_ROLL_DEGREES = "0"
$env:SWG_OG_VR_WRIST_DASHBOARD_FACE_HEAD = "1"
$env:SWG_OG_VR_HOVER_TARGET_FRAME = "0"
$env:SWG_OG_VR_HOVER_TARGET_FRAME_SCALE = "3.20"
$env:SWG_OG_VR_HOVER_TARGET_FRAME_WIDTH_METERS = "4.20"
$env:SWG_OG_VR_HOVER_TARGET_FRAME_HEIGHT_METERS = "2.20"
$env:SWG_OG_VR_MENU_BUTTON = "1"
$env:SWG_OG_VR_MENU_BUTTON_OPENS_BUTTON_BAR = "0"
$env:SWG_OG_VR_WRIST_MENU_BUTTON = "0"
$env:SWG_OG_VR_WRIST_MENU_BUTTON_HAND = "0"
$env:SWG_OG_VR_WRIST_MENU_BUTTON_ROLL_DEGREES = "0"
$env:SWG_OG_VR_WRIST_MENU_BUTTON_OFFSET_X_METERS = "0.105"
$env:SWG_OG_VR_WRIST_MENU_BUTTON_OFFSET_Y_METERS = "0.065"
$env:SWG_OG_VR_WRIST_MENU_BUTTON_OFFSET_Z_METERS = "-0.045"
$env:SWG_OG_VR_WRIST_MENU_BUTTON_WIDTH_METERS = "0.420"
$env:SWG_OG_VR_WRIST_MENU_BUTTON_HEIGHT_METERS = "0.280"
$env:SWG_OG_VR_TRIGGER_SELECTS_WORLD = "0"
$env:SWG_OG_VR_PRIMARY_TRIGGER_ACTION = "1"
$env:SWG_OG_VR_PRIMARY_TRIGGER_HAND = "right"
$env:SWG_OG_VR_PRIMARY_TRIGGER_TARGETS_WORLD = "1"
$env:SWG_OG_VR_UI_OCCLUDES_PHYSICS = "1"
$env:SWG_OG_VR_SWAP_MOUSE_TRIGGERS = "1"
$env:SWG_OG_VR_WORLD_WAND_CURSOR_MODE = "center"
$env:SWG_OG_VR_WORLD_WAND_NATIVE_RAY = "1"
$env:SWG_OG_VR_POINTER_LAYER_LENGTH_METERS = "12.0"
$env:SWG_OG_VR_GRIP_TARGETS_WORLD = "1"
$env:SWG_OG_VR_TAB_COMBAT_TARGETING = "1"
$env:SWG_OG_VR_TAB_COMBAT_PROJECTILE_MUZZLE_ORIGIN = "1"
$env:SWG_OG_VR_TAB_COMBAT_TARGET_APPEARANCE_COLLIDE = "0"
$env:SWG_OG_VR_TAB_COMBAT_TARGET_HEIGHT_FRACTION = "0.40"
$env:SWG_OG_VR_TARGETING_RANGE_METERS = "256"
$env:SWG_OG_VR_TARGET_RAY_BASE_RADIUS_METERS = "0.35"
$env:SWG_OG_VR_TARGET_RAY_RADIUS_PER_METER = "0.010"
$env:SWG_OG_VR_TARGET_RAY_MAX_RADIUS_METERS = "2.10"
$env:SWG_OG_VR_TARGET_CYCLE_BUTTONS = "1"
$env:SWG_OG_VR_OVERHEAD_TARGET_UI = "0"
$env:SWG_OG_VR_WORLD_WAND_TARGET_HAND = "right"
$env:SWG_OG_VR_WORLD_RADIAL_FALLBACK = "0"
$env:SWG_OG_VR_WORLD_RECENTER_DELAY_FRAMES = "90"
$env:SWG_OG_VR_WORLD_CARRY_QUAD_RECENTER = "0"
$wandVisualsEnabled = !$HideWand
$env:SWG_OG_VR_POINTER_VISUALS = if ($wandVisualsEnabled) { "1" } else { "0" }
$env:SWG_OG_VR_HANDS_HEAD_RELATIVE = "0"
$env:SWG_OG_VR_DETACHED_HANDS = "1"
$env:SWG_OG_VR_DETACHED_HANDS_MESH_LOD = "0"
$env:SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE = "1"
$env:SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE_EXACT = "1"
$env:SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE_TUCK_BACK_METERS = "0.055"
$env:SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE_TUCK_DOWN_METERS = "0.025"
$env:SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE_TUCK_IN_METERS = "0.018"
$env:SWG_OG_VR_DETACHED_HANDS_UPPER_ARM_COLLAPSE = "1"
$env:SWG_OG_VR_DETACHED_HANDS_USE_AIM_POSE = "0"
$env:SWG_OG_VR_DETACHED_HANDS_REQUIRE_TRACKED = "0"
$env:SWG_OG_VR_DETACHED_HANDS_LEFT_YAW_DEGREES = "180"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_YAW_DEGREES = "0"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_ROLL_DEGREES = "180"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_YAW_DEGREES = "180"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_PITCH_DEGREES = "0"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_ROLL_DEGREES = "0"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_PALM_YAW_DEGREES = "0"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_PALM_PITCH_DEGREES = "0"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_PALM_ROLL_DEGREES = "0"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_USE_HOLD_R = "1"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_USE_AIM_ORIENTATION = "1"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_RIGHT_METERS = "0"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_UP_METERS = "0"
$env:SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_FORWARD_METERS = "0"
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
$env:SWG_OG_VR_IK_SEATED_HEAD_ANCHOR = "0"
$env:SWG_OG_VR_IK_HEAD_XZ_CLAMP_M = "0.12"
$env:SWG_OG_VR_FB_LEGS = "0"
$env:SWG_OG_VR_IK_MOVING_WEIGHT = "1.00"
$env:SWG_OG_VR_IK_MOVE_BLEND_START_SPEED = "9999"
$env:SWG_OG_VR_IK_MOVE_BLEND_FULL_SPEED = "10000"
$env:SWG_OG_VR_IK_HAND_MOVE_WEIGHT = "1.00"
$env:SWG_OG_VR_IK_FOREARM_MOVE_WEIGHT = "1.00"
$env:SWG_OG_VR_IK_HANDS_ONLY = "1"
$env:SWG_OG_VR_IK_ARM_POSITION_LOCK = "0"
$env:SWG_OG_VR_IK_LIMB_ROTATION_WEIGHT = "0.55"
$env:SWG_OG_VR_IK_WRIST_INSET_M = "0.060"
$env:SWG_OG_VR_IK_LEFT_WRIST_PITCH_DEG = "0"
$env:SWG_OG_VR_IK_LEFT_WRIST_YAW_DEG = "180"
$env:SWG_OG_VR_IK_LEFT_WRIST_ROLL_DEG = "0"
$env:SWG_OG_VR_IK_RIGHT_WRIST_PITCH_DEG = "0"
$env:SWG_OG_VR_IK_RIGHT_WRIST_YAW_DEG = "0"
$env:SWG_OG_VR_IK_RIGHT_WRIST_ROLL_DEG = "180"
$env:SWG_OG_VR_IK_USE_AIM_POSITION = "1"
$env:SWG_OG_VR_IK_RIGHT_HAND_USES_AIM_POSE = "0"
$env:SWG_OG_VR_IK_RIGHT_HOLD_USES_AIM_RAY_ORIGIN = "1"
$env:SWG_OG_VR_IK_RIGHT_HOLD_PITCH_DEG = "0"
$env:SWG_OG_VR_IK_RIGHT_HOLD_YAW_DEG = "180"
$env:SWG_OG_VR_IK_RIGHT_HOLD_ROLL_DEG = "0"
$env:SWG_OG_VR_BODY_IK_SHOW_SKELETON = "0"
if ($HandTrace) {
    $env:SWG_OG_VR_HAND_TRACE = "1"
    $env:SWG_OG_VR_HAND_TRACE_FILE = $handTracePath
}
else {
    Remove-Item Env:SWG_OG_VR_HAND_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:SWG_OG_VR_HAND_TRACE_FILE -ErrorAction SilentlyContinue
}
$env:SWG_D3D11_PRESENT_INTERVAL = "0"
$env:SWG_OG_VR_PHYSICS_ALLOW_STATIC_SETDRESSING = "1"
$env:SWG_OG_VR_PHYSICS_REQUIRE_TRANSFERABLE = "0"
if ($ProofTrace) {
    $env:SWG_OG_VR_PROOF = $proofPath
}
else {
    Remove-Item Env:SWG_OG_VR_PROOF -ErrorAction SilentlyContinue
}
Remove-Item Env:SWG_OG_VR_DEBUG_OUTPUT -ErrorAction SilentlyContinue

if ($BindSystemMenuButton) {
    $env:SWG_OG_VR_BIND_SYSTEM_MENU_BUTTON = "1"
}
else {
    Remove-Item Env:SWG_OG_VR_BIND_SYSTEM_MENU_BUTTON -ErrorAction SilentlyContinue
}

if ($TvMode) {
    $env:SWG_OG_VR_TV_MODE = "1"
}
else {
    Remove-Item Env:SWG_OG_VR_TV_MODE -ErrorAction SilentlyContinue
}

if ($EnableLogs) {
    $env:SWG_LOG_FILE = $clientLogPath
    $env:SWG_D3D11_DIAGNOSTICS = "1"
}
else {
    Remove-Item Env:SWG_LOG_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:SWG_D3D11_DIAGNOSTICS -ErrorAction SilentlyContinue
}

if ($PhysicsTrace) {
    $env:SWG_OG_VR_PHYSICS_TRACE = "1"
    $env:SWG_OG_VR_PHYSICS_TRACE_FILE = $physicsTracePath
}
else {
    Remove-Item Env:SWG_OG_VR_PHYSICS_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:SWG_OG_VR_PHYSICS_TRACE_FILE -ErrorAction SilentlyContinue
}

Write-Output ""
Write-Output "VR environment:"
Write-Output "  SWG_OG_VR=1"
Write-Output "  Force first person=on"
Write-Output "  HUD full panel=off"
Write-Output "  Main HUD quads=off; no full-HUD slab and no generic cropped HUD panels"
Write-Output "  Object context quads=on; target/status windows anchor to the object under the VR ray"
Write-Output "  Object context stock panel size=$env:SWG_OG_VR_OBJECT_CONTEXT_SCALE scale, height=$env:SWG_OG_VR_OBJECT_CONTEXT_HEIGHT_METERS m"
Write-Output "  Wrist forearm quads=$env:SWG_OG_VR_WRIST_DASHBOARD left=stats right=radar size=$env:SWG_OG_VR_WRIST_DASHBOARD_WIDTH_METERS x $env:SWG_OG_VR_WRIST_DASHBOARD_HEIGHT_METERS m"
Write-Output "  Object context input=left/right mouse routes stay native for default action and radial/context menus"
Write-Output "  World recenter=delayed 90 world frames after game entry; avoids loading-pose capture"
Write-Output "  Stereo scale=$env:SWG_OG_VR_STEREO_SCALE; head position scale=$env:SWG_OG_VR_HEAD_POSITION_SCALE; eye scale=$env:SWG_OG_VR_EYE_SCALE; near plane=$env:SWG_OG_VR_NEAR_PLANE"
Write-Output "  First-person eye anchor=forward $env:SWG_OG_VR_FIRST_PERSON_EYE_FORWARD m, up $env:SWG_OG_VR_FIRST_PERSON_EYE_UP m"
Write-Output "  Controller calibration=hand IK up $env:SWG_OG_VR_HAND_IK_OFFSET_Y_METERS m, aim ray down $env:SWG_OG_VR_AIM_RAY_OFFSET_Y_METERS m"
Write-Output "  Primary trigger action=on; right trigger drives SWG primary action/attack with normal auto-fire pacing"
Write-Output "  World wand target=either hand, native VR aim ray into SWG skeletal/tangible collision; center cursor is fallback only"
Write-Output "  World grip casts targeting ray; physical left trigger remains context/right-click"
Write-Output "  Target/status UI=stock SWG page rendered directly into a VR panel texture"
Write-Output "  Menu button=A/B cycles and equips a real inventory weapon; Y/X cycle targets"
Write-Output "  Wrist menu button=off for hand-focus pass"
Write-Output "  System menu binding=$(if ($BindSystemMenuButton) { 'on' } else { 'off' })"
Write-Output "  Wand visuals=$(if ($wandVisualsEnabled) { 'visible' } else { 'hidden' })"
Write-Output "  Menu controller hand quads=off"
Write-Output "  2D OpenXR hand layers=hard disabled in renderer"
Write-Output "  Hand pose space=recentered OpenXR controller tracking space; no head-relative hand sway"
Write-Output "  In-game hands=detached hand-only SWG rig; player body mesh hidden"
Write-Output "  Body IK=disabled while detached hands are active"
Write-Output "  Detached forearm collapse=off; hands are controller-origin only"
Write-Output "  Desktop present interval=0"
Write-Output "  OpenXR runtime=$selectedRuntimeName$(if ($selectedRuntimeJson) { " JSON=$selectedRuntimeJson" } else { " active registry" }); explicit loader=$oculusOpenXrLoader"
Write-Output "  Renderer hand layer marker=$rendererBuildMarker"
Write-Output "  VR weapon inventory=A/B cycle/equip real inventory weapon"
Write-Output "  Hand trace=$(if ($HandTrace) { $handTracePath } else { 'off' })"
Write-Output "  Static set dressing physics=on"
Write-Output "  TV mode=$(if ($TvMode) { 'forced on' } else { 'normal VR/world flow' })"
Write-Output "  Proof log=$(if ($ProofTrace) { $proofPath } else { 'off' })"
if ($EnableLogs) {
    Write-Output "  Client log=$clientLogPath"
}
if ($PhysicsTrace) {
    Write-Output "  Physics trace=$physicsTracePath"
}

if ($DeployOnly -or $ValidateOnly) {
    Write-Output ""
    Write-Output "$(if ($DeployOnly) { 'DeployOnly' } else { 'ValidateOnly' }) set; not launching client."
    exit 0
}

Write-Output ""
Write-Output "Controls:"
Write-Output "  A/B: cycle real inventory weapons, including empty hands"
Write-Output "  Y: target next; X: target previous"
Write-Output "  Grip press: object selects that object; air clears intended target; hover only highlights"
Write-Output "  Grip on HUD: disabled during hand-focus pass"
Write-Output "  Grip in world: casts/holds pointer ray"
Write-Output "  Right trigger: SWG primary action/attack for equipped weapon"
Write-Output "  Left trigger: context/right-click through active grip ray"
Write-Output "  Grip on eligible small prop: highlight/snap/grab"
Write-Output ""

Push-Location -LiteralPath $ClientDataRoot
try {
    Write-Output "Launching: $runtimeExe"
    $process = Start-Process -FilePath $runtimeExe -WorkingDirectory $ClientDataRoot -PassThru
    Write-Output "Started SwgClient_r.exe pid=$($process.Id)"
    if ($Wait) {
        Wait-Process -Id $process.Id
        Write-Output "SwgClient_r.exe exited."
    }
}
finally {
    Pop-Location
}
