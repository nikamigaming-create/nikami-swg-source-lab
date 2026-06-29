param(
    [string]$Configuration = "Release",
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [string]$Target = "SwgClient",
    [string]$ClientToolsRoot = "",
    [string]$MSBuild = "C:\Program Files (x86)\MSBuild\12.0\Bin\MSBuild.exe",
    [string]$LogPath = "",
    [switch]$AllowWin32Build
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
if ([string]::IsNullOrWhiteSpace($ClientToolsRoot)) {
    $ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
}
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path (Get-NikamiLogRoot -WorkspaceRoot $WorkspaceRoot) "build\og_client_build.log"
}

if ($Platform -eq "Win32" -and !$AllowWin32Build) {
    throw "Refusing Win32 build during the OG VR experiment. Use -Platform x64, or pass -AllowWin32Build only for an intentional legacy 32-bit diagnostic."
}

function Reset-ProcessPath {
    $pathValue = [Environment]::GetEnvironmentVariable("Path", "Process")
    if ([string]::IsNullOrWhiteSpace($pathValue)) {
        $pathValue = [Environment]::GetEnvironmentVariable("PATH", "Process")
    }
    if ([string]::IsNullOrWhiteSpace($pathValue)) {
        return
    }

    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    [Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
}

$projects = @{
    ClientGame = "src\engine\client\library\clientGame\build\win32\clientGame.vcxproj"
    SwgClientProject = "src\game\client\application\SwgClient\build\win32\SwgClient.vcxproj"
    Direct3d9 = "src\engine\client\application\Direct3d9\build\win32\Direct3d9.vcxproj"
    Direct3d9Ffp = "src\engine\client\application\Direct3d9\build\win32\Direct3d9_ffp.vcxproj"
    Direct3d9Vsps = "src\engine\client\application\Direct3d9\build\win32\Direct3d9_vsps.vcxproj"
    Direct3d11 = "src\engine\client\application\Direct3d11\build\win32\Direct3d11.vcxproj"
    Vulkan = "src\engine\client\application\Vulkan\build\win32\Vulkan.vcxproj"
    DllExport = "src\engine\client\application\DllExport\build\win32\DllExport.vcxproj"
    DebugWindow = "src\engine\client\application\DebugWindow\build\win32\DebugWindow.vcxproj"
}

$solutionTargets = @{
    SwgClient = "SwgClient"
    AllClient = "_all_client"
    AllClientLibraries = "_all_client_libraries"
    AllTools = "_all_tools"
}

if (!(Test-Path -LiteralPath $MSBuild)) {
    throw "MSBuild 12.0 not found at $MSBuild"
}

if (!$projects.ContainsKey($Target) -and !$solutionTargets.ContainsKey($Target)) {
    throw "Unknown target '$Target'. Known targets: $(($projects.Keys + $solutionTargets.Keys) -join ', ')"
}

if ($solutionTargets.ContainsKey($Target)) {
    $projectPath = Join-Path $ClientToolsRoot "src\build\win32\swg.sln"
    $targetArgs = @("/t:$($solutionTargets[$Target])")
} else {
    $projectPath = Join-Path $ClientToolsRoot $projects[$Target]
    $targetArgs = @()
}
if (!(Test-Path -LiteralPath $projectPath)) {
    throw "Build file not found: $projectPath"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null

Reset-ProcessPath

Write-Output "Building $Target $Configuration|$Platform"
Write-Output "Project: $projectPath"
Write-Output "Log: $LogPath"

& $MSBuild $projectPath @targetArgs /p:Configuration=$Configuration /p:Platform=$Platform /nologo /v:minimal /m 2>&1 |
    Tee-Object -FilePath $LogPath

exit $LASTEXITCODE
