param(
    [string]$OutputZip = "",
    [string]$UpstreamRef = "949451032647e45e42c3aaef3f41b132c8af36e3"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ClientToolsRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if ([string]::IsNullOrWhiteSpace($OutputZip)) {
    $OutputZip = Join-Path (Get-NikamiArtifactsRoot -WorkspaceRoot $WorkspaceRoot) "OG-VR-Source.zip"
}

$stageRoot = Join-Path (Split-Path -Parent $OutputZip) "_source_overlay_build"
$overlayRoot = Join-Path $stageRoot "overlay"

$binaryOrRuntimeExtensions = @(
    ".7z",
    ".asi",
    ".bmp",
    ".dds",
    ".dll",
    ".exe",
    ".flt",
    ".gif",
    ".iff",
    ".jpg",
    ".jpeg",
    ".lib",
    ".mp3",
    ".obj",
    ".pdb",
    ".png",
    ".spv",
    ".tga",
    ".toc",
    ".tre",
    ".wav",
    ".zip"
)

$trackedDelta = & git -C $ClientToolsRoot diff --name-only "$UpstreamRef..HEAD"
if ($LASTEXITCODE -ne 0) {
    throw "Could not diff upstream ref $UpstreamRef"
}

$workingDelta = & git -C $ClientToolsRoot diff --name-only
if ($LASTEXITCODE -ne 0) {
    throw "Could not list working-tree diff"
}

$stagedDelta = & git -C $ClientToolsRoot diff --cached --name-only
if ($LASTEXITCODE -ne 0) {
    throw "Could not list staged diff"
}

$untrackedDelta = & git -C $ClientToolsRoot ls-files --others --exclude-standard
if ($LASTEXITCODE -ne 0) {
    throw "Could not list untracked files"
}

$sourceOverlayFiles = @($trackedDelta + $workingDelta + $stagedDelta + $untrackedDelta) |
    Where-Object { ![string]::IsNullOrWhiteSpace($_) } |
    ForEach-Object { $_ -replace '/', '\' } |
    Sort-Object -Unique |
    Where-Object {
        $relative = $_
        $extension = [System.IO.Path]::GetExtension($relative).ToLowerInvariant()
        ($binaryOrRuntimeExtensions -notcontains $extension) -and
            ($relative -notlike "artifacts\*") -and
            ($relative -notlike "src\compile\*") -and
            ($relative -notlike "*.user") -and
            (Test-Path -LiteralPath (Join-Path $ClientToolsRoot $relative) -PathType Leaf)
    }

if (!$sourceOverlayFiles -or $sourceOverlayFiles.Count -eq 0) {
    throw "No source overlay files were selected."
}

Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $overlayRoot | Out-Null

foreach ($relative in $sourceOverlayFiles) {
    $source = Join-Path $ClientToolsRoot $relative
    if (!(Test-Path -LiteralPath $source)) {
        throw "Missing source overlay file: $relative"
    }

    $target = Join-Path $overlayRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}

@'
OG VR / Flat Source Overlay

Apply this overlay to a fresh clone of the SWG Source client-tools repository, preserving paths.
Then build with scripts/dev/BUILD-OG-VR-PORTABLE.ps1 to produce the binary overlay.

Runtime data is not included here. Use scripts/dev/cleanroom/Invoke-OfficialBaseline.ps1
or the binary overlay's GET-SWG-SOURCE-CLIENT.ps1 flow to download the official SWG Source
Client v3.0 release and update it from SWG-Source/client-assets.

The overlay is generated from source changes relative to SWG-Source/client-tools
upstream ref 949451032647e45e42c3aaef3f41b132c8af36e3, plus current working
 source/script/doc changes. Binary and runtime asset extensions are intentionally excluded.
'@ | Set-Content -LiteralPath (Join-Path $stageRoot "README_SOURCE_OOB.txt") -Encoding ASCII

$manifest = foreach ($relative in $sourceOverlayFiles) {
    $source = Join-Path $ClientToolsRoot $relative
    [pscustomobject]@{
        RelativePath = $relative
        Length = (Get-Item -LiteralPath $source).Length
        Sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    }
}
$manifest | Export-Csv -LiteralPath (Join-Path $stageRoot "SOURCE_OVERLAY_MANIFEST.csv") -NoTypeInformation

[ordered]@{
    UpstreamRef = $UpstreamRef
    GeneratedUtc = (Get-Date).ToUniversalTime().ToString("o")
    FileCount = @($sourceOverlayFiles).Count
    ExcludedExtensions = $binaryOrRuntimeExtensions
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stageRoot "SOURCE_OVERLAY_BASE.json") -Encoding ASCII

Remove-Item -LiteralPath $OutputZip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stageRoot "*") -DestinationPath $OutputZip -CompressionLevel Optimal
Remove-Item -LiteralPath $stageRoot -Recurse -Force

Write-Output "Built source overlay:"
Write-Output "  $OutputZip"
