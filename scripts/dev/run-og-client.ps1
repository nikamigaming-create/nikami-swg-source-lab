param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [switch]$BuildFirst,
    [switch]$CopyBuiltSupportDlls,
    [switch]$UseD3D9Proxy,
    [switch]$AllowWin32Run,
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

if (!$AllowWin32Run) {
    throw "run-og-client.ps1 is the legacy Win32 launcher and is blocked during the OG VR experiment. Use START-OG-FLAT.ps1 or START-OG-VR.ps1 for x64, or pass -AllowWin32Run only for an intentional legacy 32-bit diagnostic."
}

function Restore-QuarantinedD3D9 {
    param(
        [string]$Root
    )

    $quarantineRoot = Join-Path $Root "_quarantine_dx11"
    if (!(Test-Path -LiteralPath $quarantineRoot)) {
        return $false
    }

    $candidate = Get-ChildItem -LiteralPath $quarantineRoot -Filter 'd3d9.dll' -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($null -eq $candidate) {
        return $false
    }

    Copy-Item -LiteralPath $candidate.FullName -Destination (Join-Path $Root 'd3d9.dll') -Force
    Write-Output "Restored quarantined d3d9.dll from $($candidate.FullName)"
    return $true
}

if ($BuildFirst) {
    & (Join-Path $PSScriptRoot "build-og-client.ps1") -Target SwgClient -ClientToolsRoot $ClientToolsRoot -Platform Win32 -AllowWin32Build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$exePath = Join-Path $ClientToolsRoot "src\compile\win32\SwgClient\Release\SwgClient_r.exe"
if (!(Test-Path -LiteralPath $exePath)) {
    throw "OG client exe not found. Build it first: scripts\dev\build-og-client.ps1 -Target SwgClient"
}
if (!(Test-Path -LiteralPath $ClientDataRoot)) {
    throw "Client data directory not found: $ClientDataRoot"
}

$copies = @(@{ Source = $exePath; Name = "SwgClient_r.exe" })
if ($CopyBuiltSupportDlls) {
    $copies += @(
        @{ Source = (Join-Path $ClientToolsRoot "src\compile\win32\dpvs\Release\dpvs.dll"); Name = "dpvs.dll" },
        @{ Source = (Join-Path $ClientToolsRoot "src\compile\win32\Direct3d9\Release\gl05_r.dll"); Name = "gl05_r.dll" },
        @{ Source = (Join-Path $ClientToolsRoot "src\compile\win32\Direct3d9_ffp\Release\gl06_r.dll"); Name = "gl06_r.dll" },
        @{ Source = (Join-Path $ClientToolsRoot "src\compile\win32\Direct3d9_vsps\Release\gl07_r.dll"); Name = "gl07_r.dll" },
        @{ Source = (Join-Path $ClientToolsRoot "src\compile\win32\DllExport\Release\DllExport.dll"); Name = "DllExport.dll" }
    )
}

$backup = Join-Path $ClientDataRoot "SwgClient_r.exe.original-bak"
$destExe = Join-Path $ClientDataRoot "SwgClient_r.exe"
if ((Test-Path -LiteralPath $destExe) -and !(Test-Path -LiteralPath $backup)) {
    Copy-Item -LiteralPath $destExe -Destination $backup -Force
}

foreach ($copy in $copies) {
    if (Test-Path -LiteralPath $copy.Source) {
        Copy-Item -LiteralPath $copy.Source -Destination (Join-Path $ClientDataRoot $copy.Name) -Force
    }
}

$d3d9Path = Join-Path $ClientDataRoot 'd3d9.dll'
if ($UseD3D9Proxy) {
    if (!(Test-Path -LiteralPath $d3d9Path)) {
        if (-not (Restore-QuarantinedD3D9 -Root $ClientDataRoot)) {
            throw "UseD3D9Proxy was requested, but no d3d9.dll was present and no quarantined copy was found under _quarantine_dx11."
        }
    }
}
elseif (Test-Path -LiteralPath $d3d9Path) {
    throw "Refusing clean OG launch while a local d3d9.dll proxy exists in $ClientDataRoot. Run clean-og-client-runtime.ps1 or pass -UseD3D9Proxy intentionally."
}

Push-Location -LiteralPath $ClientDataRoot
try {
    $process = Start-Process -FilePath ".\SwgClient_r.exe" -PassThru
    if ($Wait) {
        Wait-Process -Id $process.Id
    }
}
finally {
    Pop-Location
}
