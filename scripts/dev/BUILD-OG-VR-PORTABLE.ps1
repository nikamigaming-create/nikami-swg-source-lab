param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$OutputOverlay = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ClientToolsRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if ([string]::IsNullOrWhiteSpace($OutputOverlay)) {
    $OutputOverlay = Join-Path (Get-NikamiArtifactsRoot -WorkspaceRoot $WorkspaceRoot) "OG-VR-Binary"
}

function Find-MSBuild {
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\MSBuild\12.0\Bin\MSBuild.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "MSBuild not found. Install Visual Studio 2022 Build Tools with C++ workload."
}

function Invoke-BuildProject {
    param([string]$MSBuild, [string]$Project)
    $fullPath = Join-Path $ClientToolsRoot $Project
    if (!(Test-Path -LiteralPath $fullPath)) {
        throw "Project not found: $fullPath"
    }

    Write-Output "Building $Project [$Configuration|$Platform]"
    & $MSBuild $fullPath /p:Configuration=$Configuration /p:Platform=$Platform /m:1 /v:m
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed for $Project"
    }
}

function Invoke-BuildStlPort {
    if ($Platform -ne "x64") {
        return
    }

    $libraryPath = Join-Path $ClientToolsRoot "src\compile\$Platform\stlport453\$Configuration\stlport_vc71_static.lib"
    if (Test-Path -LiteralPath $libraryPath) {
        Write-Output "STLPort x64 library already present: $libraryPath"
        return
    }

    $vcVarsAll = "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat"
    if (!(Test-Path -LiteralPath $vcVarsAll)) {
        throw "Visual Studio 2013 VC tools not found: $vcVarsAll"
    }

    $stlPortSource = Join-Path $ClientToolsRoot "src\external\3rd\library\stlport453\src"
    $outputDir = Join-Path $ClientToolsRoot "src\compile\$Platform\stlport453\$Configuration"
    if (!(Test-Path -LiteralPath $stlPortSource)) {
        throw "STLPort source not found: $stlPortSource"
    }

    Write-Output "Building STLPort x64 static library"
    $command = "call `"$vcVarsAll`" amd64 && cd /d `"$stlPortSource`" && nmake /nologo /f vc71.mak release_static `"MKDIR=cmd /c mkdir`" OUTDIR=$outputDir LDFLAGS_COMMON=`"/nologo /machine:X64 /debugtype:cv`""
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "STLPort x64 build failed"
    }
    if (!(Test-Path -LiteralPath $libraryPath)) {
        throw "STLPort x64 build did not produce: $libraryPath"
    }
}

$msbuild = Find-MSBuild

if (!$SkipBuild) {
    Invoke-BuildStlPort

    $projects = @(
        "src\external\3rd\library\blat194\build\win32\blat.vcxproj",
        "src\external\3rd\library\dpvs\implementation\msvc8\dpvs.vcxproj",
        "src\external\3rd\library\lcdui_src\build\win32\lcdui.vcxproj",
        "src\external\3rd\library\libEverQuestTCG\build\win32\libEverQuestTCG.vcxproj",
        "src\external\3rd\library\udplibrary\udplibrary.vcxproj",
        "src\external\3rd\library\ui\build\win32\ui.vcxproj",
        "src\external\3rd\library\zlib-1.2.3\zlib.vcxproj",
        "src\external\ours\library\archive\build\win32\archive.vcxproj",
        "src\external\ours\library\crypto\build\win32\crypto.vcxproj",
        "src\external\ours\library\fileInterface\build\win32\fileInterface.vcxproj",
        "src\external\ours\library\localization\build\win32\localization.vcxproj",
        "src\external\ours\library\localizationArchive\build\win32\localizationArchive.vcxproj",
        "src\external\ours\library\unicode\build\win32\unicode.vcxproj",
        "src\external\ours\library\unicodeArchive\build\win32\unicodeArchive.vcxproj",
        "src\engine\shared\library\sharedCollision\build\win32\sharedCollision.vcxproj",
        "src\engine\shared\library\sharedCommandParser\build\win32\sharedCommandParser.vcxproj",
        "src\engine\shared\library\sharedCompression\build\win32\sharedCompression.vcxproj",
        "src\engine\shared\library\sharedDebug\build\win32\sharedDebug.vcxproj",
        "src\engine\shared\library\sharedFile\build\win32\sharedFile.vcxproj",
        "src\engine\shared\library\sharedFoundation\build\win32\sharedFoundation.vcxproj",
        "src\engine\shared\library\sharedFractal\build\win32\sharedFractal.vcxproj",
        "src\engine\shared\library\sharedGame\build\win32\sharedGame.vcxproj",
        "src\engine\shared\library\sharedImage\build\win32\sharedImage.vcxproj",
        "src\engine\shared\library\sharedInputMap\build\win32\sharedInputMap.vcxproj",
        "src\engine\shared\library\sharedIoWin\build\win32\sharedIoWin.vcxproj",
        "src\engine\shared\library\sharedLog\build\win32\sharedLog.vcxproj",
        "src\engine\shared\library\sharedMath\build\win32\sharedMath.vcxproj",
        "src\engine\shared\library\sharedMemoryManager\build\win32\sharedMemoryManager.vcxproj",
        "src\engine\shared\library\sharedMessageDispatch\build\win32\sharedMessageDispatch.vcxproj",
        "src\engine\shared\library\sharedNetwork\build\win32\sharedNetwork.vcxproj",
        "src\engine\shared\library\sharedNetworkMessages\build\win32\sharedNetworkMessages.vcxproj",
        "src\engine\shared\library\sharedObject\build\win32\sharedObject.vcxproj",
        "src\engine\shared\library\sharedPathfinding\build\win32\sharedPathfinding.vcxproj",
        "src\engine\shared\library\sharedRandom\build\win32\sharedRandom.vcxproj",
        "src\engine\shared\library\sharedRegex\build\win32\sharedRegex.vcxproj",
        "src\engine\shared\library\sharedRemoteDebugServer\build\win32\sharedRemoteDebugServer.vcxproj",
        "src\engine\shared\library\sharedSkillSystem\build\win32\sharedSkillSystem.vcxproj",
        "src\engine\shared\library\sharedStatusWindow\build\win32\sharedStatusWindow.vcxproj",
        "src\engine\shared\library\sharedSwitcher\build\win32\sharedSwitcher.vcxproj",
        "src\engine\shared\library\sharedSynchronization\build\win32\sharedSynchronization.vcxproj",
        "src\engine\shared\library\sharedTerrain\build\win32\sharedTerrain.vcxproj",
        "src\engine\shared\library\sharedThread\build\win32\sharedThread.vcxproj",
        "src\engine\shared\library\sharedUtility\build\win32\sharedUtility.vcxproj",
        "src\engine\shared\library\sharedXml\build\win32\sharedXml.vcxproj",
        "src\game\shared\library\swgSharedNetworkMessages\build\win32\swgSharedNetworkMessages.vcxproj",
        "src\game\shared\library\swgSharedUtility\build\win32\swgSharedUtility.vcxproj",
        "src\engine\client\library\clientAnimation\build\win32\clientAnimation.vcxproj",
        "src\engine\client\library\clientAudio\build\win32\clientAudio.vcxproj",
        "src\engine\client\library\clientBugReporting\build\win32\clientBugReporting.vcxproj",
        "src\engine\client\library\clientDirectInput\build\win32\clientDirectInput.vcxproj",
        "src\engine\client\library\clientObject\build\win32\clientObject.vcxproj",
        "src\engine\client\library\clientParticle\build\win32\clientParticle.vcxproj",
        "src\engine\client\library\clientSkeletalAnimation\build\win32\clientSkeletalAnimation.vcxproj",
        "src\engine\client\library\clientTerrain\build\win32\clientTerrain.vcxproj",
        "src\engine\client\library\clientTextureRenderer\build\win32\clientTextureRenderer.vcxproj",
        "src\engine\client\library\clientGraphics\build\win32\clientGraphics.vcxproj",
        "src\engine\client\library\clientGame\build\win32\clientGame.vcxproj",
        "src\engine\client\library\clientUserInterface\build\win32\clientUserInterface.vcxproj",
        "src\game\client\library\swgClientUserInterface\build\win32\swgClientUserInterface.vcxproj",
        "src\engine\client\application\DllExport\build\win32\DllExport.vcxproj",
        "src\engine\client\application\Direct3d11\build\win32\Direct3d11.vcxproj",
        "src\game\client\application\SwgClient\build\win32\SwgClient.vcxproj"
    )

    foreach ($project in $projects) {
        Invoke-BuildProject -MSBuild $msbuild -Project $project
    }
}

New-Item -ItemType Directory -Force -Path $OutputOverlay | Out-Null

$outputs = @(
    @{ Source = "src\compile\$Platform\SwgClient\$Configuration\SwgClient_r.exe"; Required = $true },
    @{ Source = "src\compile\$Platform\Direct3d11\$Configuration\gl05_r.dll"; Required = $true },
    @{ Source = "src\compile\$Platform\DllExport\$Configuration\DllExport.dll"; Required = $true },
    @{ Source = "src\compile\$Platform\dpvs\$Configuration\dpvs.dll"; Required = $false }
)

foreach ($item in $outputs) {
    $source = Join-Path $ClientToolsRoot $item.Source
    if (!(Test-Path -LiteralPath $source)) {
        if ($item.Required) {
            throw "Required build output missing: $source"
        }
        Write-Output "Optional build output missing, skipping: $source"
        continue
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $OutputOverlay (Split-Path -Leaf $source)) -Force
}

$milesRedistRoot = Join-Path $ClientToolsRoot "src\external\3rd\library\miles-7.2e\redist\win64"
$milesDll = Join-Path $milesRedistRoot "mss64.dll"
$milesPlugins = Join-Path $milesRedistRoot "miles"
if ((Test-Path -LiteralPath $milesDll) -and (Test-Path -LiteralPath $milesPlugins)) {
    Copy-Item -LiteralPath $milesDll -Destination (Join-Path $OutputOverlay "mss64.dll") -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $OutputOverlay "miles") | Out-Null
    Copy-Item -Path (Join-Path $milesPlugins "*") -Destination (Join-Path $OutputOverlay "miles") -Force
}
else {
    Write-Output "x64 Miles runtime not found in source tree; leaving runtime audio files to the official client/binary overlay flow."
}

$portableRuntime = Join-Path $ClientToolsRoot "scripts\dev\portable-runtime"
if (Test-Path -LiteralPath $portableRuntime) {
    Copy-Item -Path (Join-Path $portableRuntime "*") -Destination $OutputOverlay -Recurse -Force
}

@'
@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0START-OG-VR.ps1" %*
exit /b %ERRORLEVEL%
'@ | Set-Content -LiteralPath (Join-Path $OutputOverlay "START-OG-VR.cmd") -Encoding ASCII

@'
@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0START-OG-FLAT.ps1" %*
exit /b %ERRORLEVEL%
'@ | Set-Content -LiteralPath (Join-Path $OutputOverlay "START-OG-FLAT.cmd") -Encoding ASCII

@'
See the binary package README for launch instructions. This overlay is produced by scripts/dev/BUILD-OG-VR-PORTABLE.ps1.
'@ | Set-Content -LiteralPath (Join-Path $OutputOverlay "README-BUILT-OVERLAY.txt") -Encoding ASCII

@'
OG VR / Flat Binary Overlay

Base runtime:
  Download and extract the official SWG Source Client v3.0 release:
  https://github.com/SWG-Source/releases/releases/tag/swgsourceclientv3.0

  Or run GET-SWG-SOURCE-CLIENT.ps1 from this overlay to download and extract it.
  The script removes the downloaded split archives after a successful install.
  Pass -KeepDownloads if you want to retain the official download cache.

Install:
  Extract this zip directly into the official SWG Source Client folder.

Run:
  START-OG-FLAT.cmd
    Flat-screen DX11 client. VR environment is explicitly disabled.

  START-OG-VR.cmd
    VR DX11 client. Detached tracked-only hands are enabled and full-body IK is not forced.

Server/login:
  This package only ships generic local-dev defaults:
    loginServerAddress0=127.0.0.1
    loginServerPort0=44453
    loginClientID=test
    loginClientPassword=test

  To connect to a real server, edit login.cfg after extraction and set the server address,
  port, username, and password supplied by that server/operator.

Optional VR runtime selection:
  START-OG-VR.cmd -OpenXrRuntime System
  START-OG-VR.cmd -OpenXrRuntime Oculus
  START-OG-VR.cmd -OpenXrRuntime VirtualDesktop

What this overlay provides:
  SwgClient_r.exe x64
  gl05_r.dll x64 DX11 renderer
  DllExport.dll x64
  dpvs.dll x64
  x64 Miles audio runtime files are copied only when present locally
  self-configuring flat and VR launch scripts

The official SWG Source Client release provides the large TRE/TOC runtime baseline.
The launch scripts create missing local cfg stubs if the runtime does not have them.
'@ | Set-Content -LiteralPath (Join-Path $OutputOverlay "README_BINARY_OOB.txt") -Encoding ASCII

# Reuse the binary launcher from the source package when this script is shipped in the overlay.
$launcherSource = Join-Path $ClientToolsRoot "scripts\dev\START-OG-VR-BINARY.ps1"
if (Test-Path -LiteralPath $launcherSource) {
    Copy-Item -LiteralPath $launcherSource -Destination (Join-Path $OutputOverlay "START-OG-VR.ps1") -Force
} else {
    Write-Warning "START-OG-VR-BINARY.ps1 not found; copy START-OG-VR.ps1 from the binary zip into the overlay before distribution."
}

$flatLauncherSource = Join-Path $ClientToolsRoot "scripts\dev\START-OG-FLAT-BINARY.ps1"
if (Test-Path -LiteralPath $flatLauncherSource) {
    Copy-Item -LiteralPath $flatLauncherSource -Destination (Join-Path $OutputOverlay "START-OG-FLAT.ps1") -Force
} else {
    Write-Warning "START-OG-FLAT-BINARY.ps1 not found; copy START-OG-FLAT.ps1 from the binary zip into the overlay before distribution."
}

$fetchSource = Join-Path $ClientToolsRoot "scripts\dev\GET-SWG-SOURCE-CLIENT.ps1"
if (Test-Path -LiteralPath $fetchSource) {
    Copy-Item -LiteralPath $fetchSource -Destination (Join-Path $OutputOverlay "GET-SWG-SOURCE-CLIENT.ps1") -Force
}

Write-Output ""
Write-Output "Built binary overlay:"
Write-Output "  $OutputOverlay"
Write-Output "Copy that folder's contents into the SWG Source client runtime folder and run START-OG-FLAT.cmd or START-OG-VR.cmd."
