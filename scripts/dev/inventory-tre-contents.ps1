param(
    [string]$TreInventoryPath = "",
    [string]$OutputPath = "",
    [string[]]$Buckets,
    [int]$Limit = 0,
    [string]$ExtractorPath = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
$ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if ([string]::IsNullOrWhiteSpace($TreInventoryPath)) {
    $TreInventoryPath = Join-Path (Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot) "tre_inventory_system.json"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path (Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot) "tre_content_inventory.json"
}
if ([string]::IsNullOrWhiteSpace($ExtractorPath)) {
    $ExtractorPath = Join-Path $ClientToolsRoot "tools\TreeFileExtractor.exe"
}

if (!(Test-Path -LiteralPath $TreInventoryPath)) {
    throw "TRE inventory not found: $TreInventoryPath"
}
if (!(Test-Path -LiteralPath $ExtractorPath)) {
    throw "TreeFileExtractor not found: $ExtractorPath"
}

function Get-Category {
    param([string]$Path)

    $p = $Path.Replace('\', '/').ToLowerInvariant()
    $ext = [System.IO.Path]::GetExtension($p)

    if ($p.StartsWith("object/") -or $p.Contains("/object/")) { return "object-template" }
    if ($p.StartsWith("datatables/") -or $p.Contains("/datatables/")) { return "datatable" }
    if ($p.StartsWith("string/") -or $ext -eq ".stf") { return "string-table" }
    if ($p.StartsWith("appearance/") -or $p.Contains("/appearance/") -or $ext -in @(".apt", ".sat", ".lat")) { return "appearance-template" }
    if ($p.Contains("/mesh/") -or $ext -in @(".msh", ".mgn", ".lmg", ".skt", ".lod", ".cmp")) { return "mesh-skeleton" }
    if ($p.StartsWith("shader/") -or $p.Contains("/shader/") -or $ext -in @(".sht", ".spr", ".trt")) { return "shader-material" }
    if ($p.StartsWith("texture/") -or $p.Contains("/texture/") -or $ext -in @(".dds", ".tga", ".bmp", ".jpg", ".png")) { return "texture" }
    if ($p.StartsWith("animation/") -or $p.Contains("/animation/") -or $ext -in @(".ans", ".asn", ".mgn", ".lat")) { return "animation" }
    if ($p.StartsWith("effect/") -or $p.StartsWith("clienteffect/") -or $p.Contains("/effect/") -or $p.Contains("/clienteffect/") -or $ext -in @(".cef", ".prt", ".eft")) { return "effect-particle" }
    if ($p.StartsWith("terrain/") -or $p.Contains("/terrain/") -or $ext -in @(".trn", ".lay", ".flr", ".frn")) { return "terrain-world" }
    if ($p.StartsWith("ui/") -or $p.Contains("/ui/")) { return "ui" }
    if ($p.StartsWith("sound/") -or $p.StartsWith("music/") -or $p.Contains("/sound/") -or $p.Contains("/music/") -or $ext -in @(".wav", ".mp3", ".mpa", ".snd")) { return "audio" }
    if ($p.StartsWith("space/") -or $p.Contains("/space/")) { return "space" }
    if ($p.StartsWith("script/") -or $p.Contains("/script/") -or $ext -in @(".lua", ".script")) { return "script" }
    if ($p.StartsWith("misc/") -or $p.Contains("/misc/")) { return "misc" }

    return "other"
}

$inventory = Get-Content -LiteralPath $TreInventoryPath | ConvertFrom-Json
$treFiles = @($inventory.Files)
if ($Buckets -and $Buckets.Count -gt 0) {
    $bucketSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($bucket in $Buckets) { [void]$bucketSet.Add($bucket) }
    $treFiles = @($treFiles | Where-Object { $bucketSet.Contains($_.Bucket) })
}
if ($Limit -gt 0) {
    $treFiles = @($treFiles | Select-Object -First $Limit)
}

$fileSummaries = New-Object System.Collections.Generic.List[object]
$categoryTotals = @{}
$extensionTotals = @{}
$allPaths = @{}

$index = 0
foreach ($tre in $treFiles) {
    ++$index
    Write-Progress -Activity "Listing TRE contents" -Status "$index / $($treFiles.Count): $($tre.FullName)" -PercentComplete (($index / [math]::Max(1, $treFiles.Count)) * 100)

    $lines = & $ExtractorPath -l $tre.FullName 2>&1
    if ($LASTEXITCODE -ne 0) {
        $fileSummaries.Add([pscustomobject]@{
            Tre = $tre.FullName
            Bucket = $tre.Bucket
            Error = ($lines -join "`n")
            EntryCount = 0
            Categories = @()
            Extensions = @()
            Samples = @()
        })
        continue
    }

    $categoryCounts = @{}
    $extensionCounts = @{}
    $samples = New-Object System.Collections.Generic.List[string]
    $entryCount = 0

    foreach ($line in $lines) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $parts = $line -split "`t"
        if ($parts.Count -lt 2) { continue }

        $path = $parts[0].Trim()
        if ([string]::IsNullOrWhiteSpace($path)) { continue }

        ++$entryCount
        $category = Get-Category $path
        $ext = [System.IO.Path]::GetExtension($path).ToLowerInvariant()
        if ([string]::IsNullOrWhiteSpace($ext)) { $ext = "(none)" }

        if (!$categoryCounts.ContainsKey($category)) { $categoryCounts[$category] = 0 }
        if (!$categoryTotals.ContainsKey($category)) { $categoryTotals[$category] = 0 }
        if (!$extensionCounts.ContainsKey($ext)) { $extensionCounts[$ext] = 0 }
        if (!$extensionTotals.ContainsKey($ext)) { $extensionTotals[$ext] = 0 }

        ++$categoryCounts[$category]
        ++$categoryTotals[$category]
        ++$extensionCounts[$ext]
        ++$extensionTotals[$ext]

        if (!$allPaths.ContainsKey($path)) {
            $allPaths[$path] = [pscustomobject]@{
                Path = $path
                Category = $category
                Extension = $ext
                FirstTre = $tre.FullName
                FirstBucket = $tre.Bucket
                TreCount = 0
            }
        }
        ++$allPaths[$path].TreCount

        if ($samples.Count -lt 12) {
            $samples.Add($path)
        }
    }

    $fileSummaries.Add([pscustomobject]@{
        Tre = $tre.FullName
        Bucket = $tre.Bucket
        Error = $null
        EntryCount = $entryCount
        Categories = @($categoryCounts.GetEnumerator() | Sort-Object Name | ForEach-Object {
            [pscustomobject]@{ Category = $_.Key; Count = $_.Value }
        })
        Extensions = @($extensionCounts.GetEnumerator() | Sort-Object Name | ForEach-Object {
            [pscustomobject]@{ Extension = $_.Key; Count = $_.Value }
        })
        Samples = @($samples)
    })
}

Write-Progress -Activity "Listing TRE contents" -Completed

$result = [pscustomobject]@{
    GeneratedAt = (Get-Date).ToString("o")
    SourceInventory = $TreInventoryPath
    ExtractorPath = $ExtractorPath
    Buckets = @($Buckets)
    TreCount = $treFiles.Count
    CategoryTotals = @($categoryTotals.GetEnumerator() | Sort-Object Name | ForEach-Object {
        [pscustomobject]@{ Category = $_.Key; Count = $_.Value }
    })
    ExtensionTotals = @($extensionTotals.GetEnumerator() | Sort-Object Name | ForEach-Object {
        [pscustomobject]@{ Extension = $_.Key; Count = $_.Value }
    })
    UniquePathCount = $allPaths.Count
    UniquePaths = @()
    TreFiles = @($fileSummaries)
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath

$result.CategoryTotals | Format-Table Category, Count -AutoSize
Write-Output "Wrote $OutputPath"
