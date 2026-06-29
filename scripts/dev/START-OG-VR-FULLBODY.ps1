<#
.SYNOPSIS
    Launch SWG VR with body IK calibration values.

.DESCRIPTION
    Wrapper around START-OG-VR.ps1 that supplies body IK calibration values.
    Pass any additional START-OG-VR.ps1 parameters through.

    FULL-BODY IK ENV KNOBS
    ----------------------
    SWG_OG_VR_FB_SHOULDER_WIDTH   = 0.38   Shoulder width in meters
    SWG_OG_VR_FB_UPPER_ARM_LEN    = 0.28   Upper arm length in meters
    SWG_OG_VR_FB_FORE_ARM_LEN     = 0.24   Forearm length in meters
    SWG_OG_VR_FB_SHOULDER_DROP    = 0.22   Head origin to shoulder level (m)
    SWG_OG_VR_FB_BODY_HEIGHT      = 1.75   Estimated body height (m) for leg heuristic
    SWG_OG_VR_FB_ARM_SCALE_L      = 1.0    Per-side arm length multiplier (left)
    SWG_OG_VR_FB_ARM_SCALE_R      = 1.0    Per-side arm length multiplier (right)

    TESTING WORKFLOW
    ----------------
    1. Run this script.
    2. Log in to SWG. Arms and torso should visibly track your controllers/HMD.
    3. If arms are too long or short, adjust SWG_OG_VR_FB_UPPER_ARM_LEN / FORE_ARM_LEN.
    4. If shoulders feel too wide/narrow, adjust SWG_OG_VR_FB_SHOULDER_WIDTH.
    5. Use verify-og-vr-fullbody.ps1 for automated capture and regression.

.EXAMPLE
    .\START-OG-VR-FULLBODY.ps1
    .\START-OG-VR-FULLBODY.ps1 -OpenXrRuntime VirtualDesktop -EnableLogs
#>
param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [string]$ProofRoot = "",
    [ValidateSet("Oculus", "VirtualDesktop", "System")]
    [string]$OpenXrRuntime = "Oculus",
    [switch]$NoDeploy,
    [switch]$EnableLogs,
    [switch]$PhysicsTrace,
    [switch]$BindSystemMenuButton,
    [switch]$TvMode,
    [switch]$HideWand,
    [switch]$DeployOnly,
    [switch]$ValidateOnly,
    [switch]$Wait,

    # Full-body IK calibration overrides (all optional).
    [float]$ShoulderWidth   = 0.38,
    [float]$UpperArmLen     = 0.28,
    [float]$ForeArmLen      = 0.24,
    [float]$ShoulderDrop    = 0.22,
    [float]$BodyHeight      = 1.75,
    [float]$ArmScaleLeft    = 1.0,
    [float]$ArmScaleRight   = 1.0
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

# Set body calibration env vars BEFORE calling the main launcher so they are
# visible to the child process.
$env:SWG_OG_VR_FB_SHOULDER_WIDTH = [string]$ShoulderWidth
$env:SWG_OG_VR_FB_UPPER_ARM_LEN  = [string]$UpperArmLen
$env:SWG_OG_VR_FB_FORE_ARM_LEN   = [string]$ForeArmLen
$env:SWG_OG_VR_FB_SHOULDER_DROP  = [string]$ShoulderDrop
$env:SWG_OG_VR_FB_BODY_HEIGHT    = [string]$BodyHeight
$env:SWG_OG_VR_FB_ARM_SCALE_L    = [string]$ArmScaleLeft
$env:SWG_OG_VR_FB_ARM_SCALE_R    = [string]$ArmScaleRight

Write-Output "=== SWG Full-Body VR IK ==="
Write-Output "  Body IK: always on"
Write-Output "  Skeleton overlay: always on"
Write-Output "  In-game hands: real character/inventory hands only"
Write-Output "  Shoulder width:  $ShoulderWidth m"
Write-Output "  Upper arm:       $UpperArmLen m"
Write-Output "  Forearm:         $ForeArmLen m"
Write-Output "  Shoulder drop:   $ShoulderDrop m"
Write-Output "  Body height:     $BodyHeight m"
Write-Output "  Arm scale L/R:   $ArmScaleLeft / $ArmScaleRight"
Write-Output ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$mainLauncher = Join-Path $scriptDir "START-OG-VR.ps1"

# Forward all base params, but skip our extra ones.
$splatArgs = @{
    ClientToolsRoot       = $ClientToolsRoot
    ClientDataRoot        = $ClientDataRoot
    ProofRoot             = $ProofRoot
    OpenXrRuntime         = $OpenXrRuntime
}
if ($NoDeploy)            { $splatArgs['NoDeploy']            = $true }
if ($EnableLogs)          { $splatArgs['EnableLogs']          = $true }
if ($PhysicsTrace)        { $splatArgs['PhysicsTrace']        = $true }
if ($BindSystemMenuButton){ $splatArgs['BindSystemMenuButton']= $true }
if ($TvMode)              { $splatArgs['TvMode']              = $true }
if ($HideWand)            { $splatArgs['HideWand']            = $true }
if ($DeployOnly)          { $splatArgs['DeployOnly']          = $true }
if ($ValidateOnly)        { $splatArgs['ValidateOnly']        = $true }
if ($Wait)                { $splatArgs['Wait']                = $true }

& $mainLauncher @splatArgs
