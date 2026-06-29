param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [string]$ProofRoot = "",
    [switch]$Launch,
    [switch]$HideWand,
    [switch]$EnableLogs,
    [switch]$PhysicsTrace
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

$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
$configuration = "Release"
$platform = "x64"
$handBuildMarker = "SWGVRBodyIKProof"
$rendererBuildMarker = "rendererHandLayersHardDisabled_noFallback"

function Assert-Exists {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (!(Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-BinaryContainsMarker {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Assert-Exists -Path $Path -Label $Label
    if (!(Select-String -Path $Path -Pattern $Marker -SimpleMatch -Quiet)) {
        throw "$Label does not contain required build marker '$Marker': $Path"
    }
}

function Get-PeMachine {
    param([Parameter(Mandatory = $true)][string]$Path)

    Assert-Exists -Path $Path -Label "PE binary"
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

Assert-Exists -Path $msbuild -Label "MSBuild"

if ($platform -ne "x64") {
    throw "Build-Deploy-OG-VR-X64.ps1 is hard-gated to x64; got '$platform'."
}

$projects = @(
    "src\engine\client\library\clientGraphics\build\win32\clientGraphics.vcxproj",
    "src\engine\client\library\clientGame\build\win32\clientGame.vcxproj",
    "src\engine\client\library\clientUserInterface\build\win32\clientUserInterface.vcxproj",
    "src\game\client\library\swgClientUserInterface\build\win32\swgClientUserInterface.vcxproj",
    "src\engine\client\application\Direct3d11\build\win32\Direct3d11.vcxproj",
    "src\game\client\application\SwgClient\build\win32\SwgClient.vcxproj"
)

Push-Location -LiteralPath $ClientToolsRoot
try {
    foreach ($project in $projects) {
        Assert-Exists -Path $project -Label "Project"
        Write-Output "Building $project [$configuration|$platform]"
        & $msbuild $project /p:Configuration=$configuration /p:Platform=$platform /m:1 /v:m
        if ($LASTEXITCODE -ne 0) {
            throw "MSBuild failed for $project"
        }
    }

    $builtExe = Join-Path $ClientToolsRoot "src\compile\x64\SwgClient\Release\SwgClient_r.exe"
    $builtRenderer = Join-Path $ClientToolsRoot "src\compile\x64\Direct3d11\Release\gl05_r.dll"
    Assert-PeMachineX64 -Path $builtExe -Label "Built client exe"
    Assert-PeMachineX64 -Path $builtRenderer -Label "Built D3D11 renderer"
    Assert-BinaryContainsMarker -Path $builtExe -Marker $handBuildMarker -Label "Built x64 client exe"
    Assert-BinaryContainsMarker -Path $builtRenderer -Marker $rendererBuildMarker -Label "Built x64 D3D11 renderer"

    Write-Output "Built x64 client contains marker: $handBuildMarker"
    Write-Output "Built x64 D3D11 renderer contains marker: $rendererBuildMarker"
    Write-Output "Deploying through START-OG-VR.ps1 with pre-launch marker validation"
    Write-Output "Client data root: $ClientDataRoot"

    $launcherArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", "scripts\dev\START-OG-VR.ps1",
        "-ClientDataRoot", $ClientDataRoot,
        "-ProofRoot", $ProofRoot,
        "-DeployOnly"
    )
    & powershell @launcherArgs
    if ($LASTEXITCODE -ne 0) {
        throw "START-OG-VR.ps1 deploy validation failed"
    }

    if ($Launch) {
        $launchArgs = @(
            "-ExecutionPolicy", "Bypass",
            "-File", "scripts\dev\START-OG-VR.ps1",
            "-ClientDataRoot", $ClientDataRoot,
            "-ProofRoot", $ProofRoot
        )
        if ($HideWand) {
            $launchArgs += "-HideWand"
        }
        if ($EnableLogs) {
            $launchArgs += "-EnableLogs"
        }
        if ($PhysicsTrace) {
            $launchArgs += "-PhysicsTrace"
        }
        & powershell @launchArgs
        if ($LASTEXITCODE -ne 0) {
            throw "START-OG-VR.ps1 launch failed"
        }
    }
    else {
        Write-Output "Launch not requested. Run scripts\dev\START-OG-VR.ps1 when ready."
    }
}
finally {
    Pop-Location
}
