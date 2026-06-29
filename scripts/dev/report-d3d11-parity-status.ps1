[CmdletBinding()]
param(
    [string]$ProofsRoot = (Join-Path $PSScriptRoot "..\..\..\proofs"),
    [string]$Dx11SourcePath = (Join-Path $PSScriptRoot "..\..\src\engine\client\application\Direct3d11\src\win32\Direct3d11.cpp"),
    [string]$DatePrefix,
    [int]$MaxRows = 25,
    [string]$OutputPath,
    [string]$JsonOutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-JsonFile {
    param([string]$Path)
    if (!(Test-Path -LiteralPath $Path)) {
        return $null
    }

    try {
        return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
    }
    catch {
        Write-Warning "Failed to parse JSON: $Path"
        return $null
    }
}

function Get-LatestGlApiDiffPath {
    param([string]$Root)

    $candidates = Get-ChildItem -LiteralPath $Root -Directory -Filter "d3d11-line-audit-*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending

    foreach ($candidate in $candidates) {
        $path = Join-Path $candidate.FullName "glapi-diff.csv"
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    return $null
}

function Get-GlApiMappingsFromSource {
    param([string]$Path)

    $mappings = @{}
    if (!(Test-Path -LiteralPath $Path)) {
        return $mappings
    }

    $content = Get-Content -LiteralPath $Path -Raw
    $matches = [regex]::Matches($content, 'ms_glApi\.(?<api>[A-Za-z0-9_]+)\s*=\s*(?<target>[^;]+);')
    foreach ($match in $matches) {
        $api = $match.Groups['api'].Value
        $target = $match.Groups['target'].Value.Trim()
        if ($api) {
            $mappings[$api] = $target
        }
    }

    return $mappings
}

if (!(Test-Path -LiteralPath $ProofsRoot)) {
    throw "Proofs root not found: $ProofsRoot"
}

$compareSummaryFiles = Get-ChildItem -LiteralPath $ProofsRoot -Recurse -Filter "compare-summary.json" -File |
    Where-Object { $_.FullName -match "og-render-compare-" }

if ($DatePrefix) {
    $compareSummaryFiles = $compareSummaryFiles |
        Where-Object { $_.Directory.Name -like "*$DatePrefix*" }
}

$rows = @()
foreach ($file in $compareSummaryFiles) {
    $summary = Read-JsonFile -Path $file.FullName
    if ($null -eq $summary) {
        continue
    }

    $pixelSummary = $summary.PixelDiffSummary
    if ($null -eq $pixelSummary) {
        $pixelSummaryPath = Join-Path $file.Directory.FullName "pixel-diff\summary.json"
        $pixelSummary = Read-JsonFile -Path $pixelSummaryPath
    }

    $validity = $summary.PixelComparisonValidity
    if ([string]::IsNullOrWhiteSpace($validity) -and $pixelSummary) {
        $validity = $pixelSummary.comparisonValidity
    }

    $rows += [pscustomobject]@{
        CompareRoot = $summary.CompareRoot
        FolderName = $file.Directory.Name
        SceneName = $summary.SceneName
        StartedAt = $summary.StartedAt
        Validity = $validity
        MeanAbsRgb = if ($pixelSummary) { [double]$pixelSummary.meanAbsRgb } else { $null }
        ThresholdPercent = if ($pixelSummary) { [double]$pixelSummary.thresholdPercent } else { $null }
        StateMismatch = if ($pixelSummary -and $pixelSummary.stateComparison) { [bool]$pixelSummary.stateComparison.probableStateMismatch } else { $null }
        PixelSummaryPath = if ($pixelSummary) { Join-Path $file.Directory.FullName "pixel-diff\summary.json" } else { $null }
        CompareSummaryPath = $file.FullName
    }
}

$rows = $rows | Sort-Object @{ Expression = { [datetime]$_.StartedAt }; Descending = $true }

$comparable = @($rows | Where-Object { $_.Validity -eq "ComparableFrameState" -and $_.MeanAbsRgb -ne $null })
$rejected = @($rows | Where-Object { $_.Validity -eq "RejectedStateMismatch" })
$unknown = @($rows | Where-Object { [string]::IsNullOrWhiteSpace($_.Validity) })

$bestComparable = $null
if ($comparable.Count -gt 0) {
    $bestComparable = $comparable | Sort-Object MeanAbsRgb, ThresholdPercent | Select-Object -First 1
}

$latestGlApiDiff = Get-LatestGlApiDiffPath -Root $ProofsRoot
$glApiRows = @()
if ($latestGlApiDiff) {
    $glApiRows = Import-Csv -LiteralPath $latestGlApiDiff
}

$needsAudit = @($glApiRows | Where-Object { $_.Risk -eq "needs-audit" })
$diffRisk = @($glApiRows | Where-Object { $_.Risk -eq "diff" })
$sourceMappings = Get-GlApiMappingsFromSource -Path $Dx11SourcePath

$needsAuditLiveStatus = @()
foreach ($item in $needsAudit) {
    $currentTarget = $null
    if ($sourceMappings.ContainsKey($item.Api)) {
        $currentTarget = $sourceMappings[$item.Api]
    }

    $status = "missing-in-source"
    if ($null -ne $currentTarget) {
        if ([string]$currentTarget -eq [string]$item.D3D11Target) {
            $status = "matches-ledger"
        }
        else {
            $status = "changed-from-ledger"
        }
    }

    $needsAuditLiveStatus += [pscustomobject]@{
        Api = $item.Api
        LedgerD3D11Target = $item.D3D11Target
        CurrentD3D11Target = $currentTarget
        Status = $status
        LedgerLine = $item.D3D11Line
    }
}

if (-not $OutputPath) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $suffix = if ($DatePrefix) { "-$DatePrefix" } else { "" }
    $OutputPath = Join-Path $ProofsRoot ("d3d11-parity-status{0}-{1}.md" -f $suffix, $stamp)
}

if (-not $JsonOutputPath) {
    $JsonOutputPath = [System.IO.Path]::ChangeExtension($OutputPath, ".json")
}

$recentRows = $rows | Select-Object -First $MaxRows

$reportObject = [pscustomobject]@{
    generatedAt = (Get-Date).ToString("o")
    proofsRoot = $ProofsRoot
    datePrefix = $DatePrefix
    totalCompareSummaries = $rows.Count
    comparableCount = $comparable.Count
    rejectedStateMismatchCount = $rejected.Count
    unknownValidityCount = $unknown.Count
    bestComparable = $bestComparable
    latestGlApiDiffPath = $latestGlApiDiff
    dx11SourcePath = $Dx11SourcePath
    glApiTotalRows = $glApiRows.Count
    glApiNeedsAuditCount = $needsAudit.Count
    glApiDiffRiskCount = $diffRisk.Count
    recentRows = $recentRows
    needsAuditApis = @($needsAudit | Select-Object Api, D3D9Target, D3D11Target, D3D11Line)
    needsAuditLiveStatus = $needsAuditLiveStatus
}

$reportObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $JsonOutputPath

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# D3D11 Parity Status Report")
$lines.Add("")
$lines.Add("Generated: $((Get-Date).ToString('yyyy-MM-dd HH:mm:ss zzz'))")
$lines.Add("Proofs root: $ProofsRoot")
if ($DatePrefix) {
    $lines.Add("Date filter: $DatePrefix")
}
$lines.Add("")
$lines.Add("## Capture Validity Summary")
$lines.Add("")
$lines.Add("- Total compare summaries: $($rows.Count)")
$lines.Add("- ComparableFrameState: $($comparable.Count)")
$lines.Add("- RejectedStateMismatch: $($rejected.Count)")
$lines.Add("- Unknown validity: $($unknown.Count)")

if ($bestComparable) {
    $lines.Add("- Best comparable capture: $($bestComparable.FolderName) (meanAbsRgb=$('{0:N4}' -f $bestComparable.MeanAbsRgb), thresholdPercent=$('{0:N4}' -f $bestComparable.ThresholdPercent))")
}

$lines.Add("")
$lines.Add("## Gl_api Diff Summary")
$lines.Add("")
if ($latestGlApiDiff) {
    $lines.Add("- Source: $latestGlApiDiff")
    $lines.Add("- Total rows: $($glApiRows.Count)")
    $lines.Add("- diff risk rows: $($diffRisk.Count)")
    $lines.Add("- needs-audit rows: $($needsAudit.Count)")
}
else {
    $lines.Add("- No glapi-diff.csv found under proofs.")
}

if ($needsAudit.Count -gt 0) {
    $lines.Add("")
    $lines.Add("### Needs-Audit APIs")
    $lines.Add("")
    foreach ($item in $needsAudit) {
        $lines.Add("- $($item.Api): D3D9=$($item.D3D9Target), D3D11=$($item.D3D11Target) (line $($item.D3D11Line))")
    }

    $lines.Add("")
    $lines.Add("### Live Source Mapping Status")
    $lines.Add("")
    $lines.Add("| API | Ledger D3D11 Target | Current D3D11 Target | Status |")
    $lines.Add("|---|---|---|---|")
    foreach ($item in $needsAuditLiveStatus) {
        $current = if ($item.CurrentD3D11Target) { $item.CurrentD3D11Target } else { "(missing)" }
        $lines.Add("| $($item.Api) | $($item.LedgerD3D11Target) | $current | $($item.Status) |")
    }
}

$lines.Add("")
$lines.Add("## Recent Captures")
$lines.Add("")
$lines.Add("| Folder | Scene | Validity | MeanAbsRgb | ThresholdPercent |")
$lines.Add("|---|---|---:|---:|---:|")
foreach ($row in $recentRows) {
    $meanAbs = if ($row.MeanAbsRgb -ne $null) { "{0:N4}" -f $row.MeanAbsRgb } else { "-" }
    $thresh = if ($row.ThresholdPercent -ne $null) { "{0:N4}" -f $row.ThresholdPercent } else { "-" }
    $lines.Add("| $($row.FolderName) | $($row.SceneName) | $($row.Validity) | $meanAbs | $thresh |")
}

$lines.Add("")
$lines.Add("JSON output: $JsonOutputPath")

$lines | Set-Content -LiteralPath $OutputPath

Write-Host "Wrote markdown report: $OutputPath"
Write-Host "Wrote JSON report: $JsonOutputPath"