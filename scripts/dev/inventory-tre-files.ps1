param(
    [string[]]$Roots = @(),
    [switch]$ScanFixedDrives,
    [switch]$Hash,
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
$ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if (!$Roots -or $Roots.Count -eq 0) {
    $Roots = @(
        (Get-NikamiRuntimeClientRoot -WorkspaceRoot $WorkspaceRoot),
        $ClientToolsRoot,
        (Join-Path $WorkspaceRoot "downloads"),
        (Join-Path $WorkspaceRoot "cleanroom")
    )
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path (Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot) "tre_inventory.json"
}

$excludedPathParts = @(
    "\`$Recycle.Bin\",
    "\System Volume Information\",
    "\Windows\",
    "\Program Files\",
    "\Program Files (x86)\",
    "\ProgramData\",
    "\AppData\",
    "\node_modules\",
    "\.git\"
)

function Get-OriginInfo {
    param(
        [string]$FullName,
        [string]$Root
    )

    $path = $FullName.ToLowerInvariant()
    if ($path.StartsWith("d:\code\swg\client\tre\")) {
        return @{
            Bucket = "D local TRE archive copy"
            Meaning = "Historical TRE archive copy under the local workspace. Useful for archaeology and deltas; not the launch baseline."
        }
    }
    if ($path.StartsWith("c:\code\swg\client\tre\")) {
        return @{
            Bucket = "C local TRE archive copy"
            Meaning = "Historical TRE archive copy on C. Useful for archaeology and deltas; not the launch baseline."
        }
    }
    if ($path.StartsWith("c:\code\swg\tre-archive\")) {
        return @{
            Bucket = "C TRE Archive"
            Meaning = "Large historical archive spanning Beta, Pre-CU, CU, and NGE eras. Best source for era comparison and asset archaeology."
        }
    }
    if ($path.StartsWith("d:\code\swg\_github\swg-source\client-assets\")) {
        return @{
            Bucket = "D GitHub client-assets"
            Meaning = "Upstream SWG-Source client asset package. Good authority for SWGSource-specific custom TREs."
        }
    }
    if ($path.StartsWith("d:\code\swg\client\")) {
        return @{
            Bucket = "D clean workspace client data"
            Meaning = "Local launch/data target. Good for current experiments, but verify binaries separately."
        }
    }
    if ($path.StartsWith($ClientToolsRoot.ToLowerInvariant() + "\", [System.StringComparison]::OrdinalIgnoreCase)) {
        return @{
            Bucket = "Nikami clean OG source tree"
            Meaning = "Clean GitHub-source tool/client checkout. Prefer for source/build authority, not TRE authority unless TREs are explicitly present."
        }
    }
    if ($path.StartsWith("c:\code\swg-client-tools\")) {
        return @{
            Bucket = "C reference client-tools tree"
            Meaning = "Known working reference, but has DX11 experiments. Use for comparison only."
        }
    }
    if ($path.StartsWith("c:\code\swg\client\")) {
        return @{
            Bucket = "C reference SWGSource client data"
            Meaning = "Reference data folder from the working C setup. Compare before harvesting because binaries may be experimental."
        }
    }
    if ($path.StartsWith("d:\starwarsgalaxies\")) {
        return @{
            Bucket = "D StarWarsGalaxies install"
            Meaning = "Likely standard/older SWG client install. Strong baseline candidate if TRE names and hashes look common."
        }
    }
    if ($path.StartsWith("d:\swg restoration\")) {
        return @{
            Bucket = "D SWG Restoration install"
            Meaning = "Private-server/modded client data. Harvest only intentionally; do not treat as clean baseline."
        }
    }
    if ($path.StartsWith("d:\star wars genesis\")) {
        return @{
            Bucket = "D Star Wars Genesis install"
            Meaning = "Private-server/modded client data. Useful as a delta/hunt source, not baseline authority."
        }
    }
    if ($path.StartsWith("d:\wabba\")) {
        return @{
            Bucket = "D WABBA install"
            Meaning = "Custom install folder. Inspect contents before using as baseline or mod source."
        }
    }
    if ($path.Contains("\steamlibrary\")) {
        return @{
            Bucket = "Steam library"
            Meaning = "Steam-managed data. Potential retail/baseline candidate depending on folder."
        }
    }

    return @{
        Bucket = "Unclassified"
        Meaning = "Found outside known SWG roots. Inspect manually before harvesting."
    }
}

function Test-IsExcluded {
    param([string]$FullName)

    foreach ($part in $excludedPathParts) {
        if ($FullName.IndexOf($part, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            return $true
        }
    }
    return $false
}

if ($ScanFixedDrives) {
    $Roots = Get-PSDrive -PSProvider FileSystem |
        Where-Object { $_.Root -match "^[A-Z]:\\$" -and (Test-Path -LiteralPath $_.Root) } |
        ForEach-Object { $_.Root }
}

$seenRoots = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$normalizedRoots = foreach ($root in $Roots) {
    if ([string]::IsNullOrWhiteSpace($root)) { continue }
    if (!(Test-Path -LiteralPath $root)) { continue }
    $resolved = (Resolve-Path -LiteralPath $root).Path
    if ($seenRoots.Add($resolved)) { $resolved }
}

$files = foreach ($root in $normalizedRoots) {
    Get-ChildItem -LiteralPath $root -Recurse -File -Filter "*.tre" -ErrorAction SilentlyContinue | Where-Object {
        !(Test-IsExcluded $_.FullName)
    } | ForEach-Object {
        $origin = Get-OriginInfo -FullName $_.FullName -Root $root
        $sha = $null
        if ($Hash) {
            $sha = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }

        [pscustomobject]@{
            Name = $_.Name
            FullName = $_.FullName
            Root = $root
            Bucket = $origin.Bucket
            Meaning = $origin.Meaning
            Length = $_.Length
            LastWriteTime = $_.LastWriteTime
            SHA256 = $sha
        }
    }
}

$summary = $files |
    Group-Object Bucket |
    Sort-Object Name |
    ForEach-Object {
        [pscustomobject]@{
            Bucket = $_.Name
            Count = $_.Count
            Bytes = ($_.Group | Measure-Object Length -Sum).Sum
            Roots = @($_.Group.Root | Sort-Object -Unique)
        }
    }

$duplicateCandidates = $files |
    Group-Object Name, Length |
    Where-Object { $_.Count -gt 1 } |
    Sort-Object -Property @{ Expression = "Count"; Descending = $true }, Name |
    ForEach-Object {
        [pscustomobject]@{
            NameAndLength = $_.Name
            Count = $_.Count
            Buckets = @($_.Group.Bucket | Sort-Object -Unique)
            Paths = @($_.Group.FullName | Sort-Object)
        }
    }

$inventory = [pscustomobject]@{
    GeneratedAt = (Get-Date).ToString("o")
    Hashed = [bool]$Hash
    ScannedRoots = @($normalizedRoots)
    Summary = @($summary)
    DuplicateCandidatesByNameAndLength = @($duplicateCandidates)
    Files = @($files | Sort-Object Bucket, Name, FullName)
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$inventory | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath

$summary | Format-Table Bucket, Count, Bytes -AutoSize
Write-Output "Wrote $OutputPath"
