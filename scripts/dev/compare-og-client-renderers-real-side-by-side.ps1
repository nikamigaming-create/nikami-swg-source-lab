param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [string]$ProofRoot = "",
    [string]$SceneName = "worldquick",
    [ValidateSet("None", "LoginEnterWorld", "ConfigAutoConnect")]
    [string]$SceneDriver = "ConfigAutoConnect",
    [string]$AutoConnectUser = "test",
    [string]$AutoConnectPassword = "test",
    [string]$AutoConnectCluster = "swg",
    [string]$AutoConnectAvatar = "Cyruss",
    [int]$LoginDelaySeconds = 8,
    [int]$CharacterDelaySeconds = 8,
    [int]$WorldDelaySeconds = 20,
    [int]$TimeoutSeconds = 90,
    [int]$InterRunDelaySeconds = 8,
    [int]$ScreenWidth = 1280,
    [int]$ScreenHeight = 960,
    [int]$D3D11AutocapturePresent = 30,
    [int]$RendererAutocaptureMax = 12,
    [int]$RendererAutocaptureInterval = 4,
    [int]$RendererAutocaptureWorldRows = 1600,
    [int]$RendererInventoryLimit = 50000,
    [int]$RendererInventoryStartFrame = 0,
    [int]$WorldCaptureCount = 4,
    [int]$WorldCaptureIntervalSeconds = 10,
    [ValidateSet("BestShaderOverlap", "LatestInGameWorld", "LastCommon", "All")]
    [string]$RendererInventoryFrameMode = "BestShaderOverlap",
    [bool]$PinEnvironmentTime = $true,
    [double]$EnvironmentNormalizedTime = 0.525,
    [switch]$BuildFirst,
    [switch]$CleanRuntime
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

$compareScript = Join-Path $PSScriptRoot "compare-og-client-renderers.ps1"
if (!(Test-Path -LiteralPath $compareScript)) {
    throw "Missing compare script: $compareScript"
}

$runParams = @{
    ClientToolsRoot = $ClientToolsRoot
    ClientDataRoot = $ClientDataRoot
    ProofRoot = $ProofRoot
    SceneName = $SceneName
    SceneDriver = $SceneDriver
    AutoConnectUser = $AutoConnectUser
    AutoConnectPassword = $AutoConnectPassword
    AutoConnectCluster = $AutoConnectCluster
    AutoConnectAvatar = $AutoConnectAvatar
    LoginDelaySeconds = $LoginDelaySeconds
    CharacterDelaySeconds = $CharacterDelaySeconds
    WorldDelaySeconds = $WorldDelaySeconds
    TimeoutSeconds = $TimeoutSeconds
    InterRunDelaySeconds = $InterRunDelaySeconds
    ScreenWidth = $ScreenWidth
    ScreenHeight = $ScreenHeight
    D3D11AutocapturePresent = $D3D11AutocapturePresent
    RendererAutocaptureMax = $RendererAutocaptureMax
    RendererAutocaptureInterval = $RendererAutocaptureInterval
    RendererAutocaptureWorldRows = $RendererAutocaptureWorldRows
    RendererInventoryLimit = $RendererInventoryLimit
    RendererInventoryStartFrame = $RendererInventoryStartFrame
    WorldCaptureCount = $WorldCaptureCount
    WorldCaptureIntervalSeconds = $WorldCaptureIntervalSeconds
    RendererInventoryFrameMode = $RendererInventoryFrameMode
    PinEnvironmentTime = $PinEnvironmentTime
    EnvironmentNormalizedTime = $EnvironmentNormalizedTime
    RendererInventory = $true
}

if ($BuildFirst) {
    $runParams.BuildFirst = $true
}

if ($CleanRuntime) {
    $runParams.CleanRuntime = $true
}

& $compareScript @runParams
exit $LASTEXITCODE
