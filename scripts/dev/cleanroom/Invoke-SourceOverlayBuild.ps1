param(
    [string]$CleanroomRoot = "",
    [string]$SourceOverlayZip = "",
    [string]$PackagedBinaryZip = "",
    [switch]$ResetSource,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptRoot "..\Resolve-NikamiWorkspace.ps1")
$repoRoot = (Resolve-Path (Join-Path $scriptRoot "..\..\..")).Path
$workspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $repoRoot
if ([string]::IsNullOrWhiteSpace($CleanroomRoot)) {
    $CleanroomRoot = Get-NikamiCleanroomRoot -WorkspaceRoot $workspaceRoot
}
if ([string]::IsNullOrWhiteSpace($SourceOverlayZip)) {
    $SourceOverlayZip = Join-Path (Get-NikamiArtifactsRoot -WorkspaceRoot $workspaceRoot) "OG-VR-Source.zip"
}
if ([string]::IsNullOrWhiteSpace($PackagedBinaryZip)) {
    $PackagedBinaryZip = Join-Path (Get-NikamiArtifactsRoot -WorkspaceRoot $workspaceRoot) "OG-VR-Binary.zip"
}

$sourceRoot = Join-Path $CleanroomRoot "source-upstream-client-tools"
$overlayExtractRoot = Join-Path $CleanroomRoot "source-overlay-extracted"
$builtOverlayRoot = Join-Path $CleanroomRoot "built-binary-overlay"
$packagedOverlayRoot = Join-Path $CleanroomRoot "packaged-binary-overlay"
$manifestRoot = Join-Path $CleanroomRoot "manifests"

function Invoke-Git {
    param([string[]]$Arguments, [string]$WorkingDirectory = "")
    if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        & git @Arguments
    }
    else {
        & git -C $WorkingDirectory @Arguments
    }
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed" }
}

function Copy-DirectoryContents {
    param([string]$Source, [string]$Destination)

    Get-ChildItem -LiteralPath $Source -Recurse -File -Force | ForEach-Object {
        $relativePath = $_.FullName.Substring($Source.Length + 1)
        $target = Join-Path $Destination $relativePath
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
}

function New-FileInventory {
    param([string]$Root)

    Get-ChildItem -LiteralPath $Root -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            [pscustomobject]@{
                RelativePath = $_.FullName.Substring($Root.Length + 1)
                Length = $_.Length
                Sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        }
}

New-Item -ItemType Directory -Force -Path $CleanroomRoot, $manifestRoot | Out-Null

if ($ResetSource) {
    foreach ($target in @($sourceRoot, $overlayExtractRoot, $builtOverlayRoot, $packagedOverlayRoot)) {
        if (Test-Path -LiteralPath $target) {
            $resolved = (Resolve-Path -LiteralPath $target).Path
            if (!$resolved.StartsWith((Resolve-Path -LiteralPath $CleanroomRoot).Path, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to remove outside cleanroom: $resolved"
            }
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}

if (!(Test-Path -LiteralPath $sourceRoot)) {
    Invoke-Git -Arguments @("clone", "https://github.com/SWG-Source/client-tools.git", $sourceRoot)
}
Invoke-Git -Arguments @("fetch", "origin", "master") -WorkingDirectory $sourceRoot
Invoke-Git -Arguments @("checkout", "master") -WorkingDirectory $sourceRoot
Invoke-Git -Arguments @("reset", "--hard", "origin/master") -WorkingDirectory $sourceRoot
Invoke-Git -Arguments @("clean", "-xdf") -WorkingDirectory $sourceRoot

$upstreamHead = (& git -C $sourceRoot rev-parse HEAD).Trim()
$upstreamRemote = (& git -C $sourceRoot remote get-url origin).Trim()

Remove-Item -LiteralPath $overlayExtractRoot, $packagedOverlayRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $overlayExtractRoot, $packagedOverlayRoot | Out-Null
Expand-Archive -LiteralPath $SourceOverlayZip -DestinationPath $overlayExtractRoot -Force

$overlayRoot = Join-Path $overlayExtractRoot "overlay"
if (!(Test-Path -LiteralPath $overlayRoot)) { throw "Source overlay zip does not contain overlay/: $SourceOverlayZip" }
Copy-DirectoryContents -Source $overlayRoot -Destination $sourceRoot

$statusPath = Join-Path $manifestRoot "source-overlay-git-status.txt"
(& git -C $sourceRoot status --short) | Set-Content -LiteralPath $statusPath -Encoding ASCII

$buildScript = Join-Path $sourceRoot "scripts\dev\BUILD-OG-VR-PORTABLE.ps1"
if (!(Test-Path -LiteralPath $buildScript)) { throw "Overlay did not add build script: $buildScript" }

Remove-Item -LiteralPath $builtOverlayRoot -Recurse -Force -ErrorAction SilentlyContinue
if ($SkipBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript -SkipBuild -OutputOverlay $builtOverlayRoot
}
else {
    powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript -OutputOverlay $builtOverlayRoot
}
if ($LASTEXITCODE -ne 0) { throw "Clean source overlay build failed" }

Expand-Archive -LiteralPath $PackagedBinaryZip -DestinationPath $packagedOverlayRoot -Force

$built = New-FileInventory -Root $builtOverlayRoot
$packaged = New-FileInventory -Root $packagedOverlayRoot
$comparison = foreach ($path in (($built.RelativePath + $packaged.RelativePath) | Sort-Object -Unique)) {
    $b = $built | Where-Object { $_.RelativePath -eq $path } | Select-Object -First 1
    $p = $packaged | Where-Object { $_.RelativePath -eq $path } | Select-Object -First 1
    [pscustomobject]@{
        RelativePath = $path
        BuiltLength = if ($b) { $b.Length } else { $null }
        PackagedLength = if ($p) { $p.Length } else { $null }
        BuiltSha256 = if ($b) { $b.Sha256 } else { $null }
        PackagedSha256 = if ($p) { $p.Sha256 } else { $null }
        Match = ($b -and $p -and $b.Length -eq $p.Length -and $b.Sha256 -eq $p.Sha256)
    }
}

$comparison | Export-Csv -LiteralPath (Join-Path $manifestRoot "binary-overlay-comparison.csv") -NoTypeInformation

[ordered]@{
    StartedUtc = (Get-Date).ToUniversalTime().ToString("o")
    SourceRemote = $upstreamRemote
    SourceHead = $upstreamHead
    SourceOverlayZip = $SourceOverlayZip
    PackagedBinaryZip = $PackagedBinaryZip
    BuiltOverlayRoot = $builtOverlayRoot
    PackagedOverlayRoot = $packagedOverlayRoot
    MismatchCount = @($comparison | Where-Object { !$_.Match }).Count
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $manifestRoot "source-overlay-build-metadata.json") -Encoding ASCII

Write-Output ""
Write-Output "Source overlay build complete:"
Write-Output "  $builtOverlayRoot"
Write-Output "Comparison:"
Write-Output "  $(Join-Path $manifestRoot 'binary-overlay-comparison.csv')"
