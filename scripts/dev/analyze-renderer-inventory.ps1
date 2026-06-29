param(
    [Parameter(Mandatory = $true)]
    [string]$CompareRoot,
    [int]$InventoryLimit = 50000,
    [int]$MinimumFrameRows = 80,
    [ValidateSet("BestShaderOverlap", "LatestInGameWorld", "LastCommon", "All")]
    [string]$FrameMode = "BestShaderOverlap"
)

$ErrorActionPreference = "Stop"

function Get-Sha256Text {
    param([string]$Text)

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return -join ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") })
    }
    finally {
        $sha.Dispose()
    }
}

function Convert-InventoryLine {
    param(
        [string]$Backend,
        [string]$Line,
        [int]$Ordinal
    )

    if ($Line -notmatch "renderer inventory $($Backend.ToLowerInvariant())") {
        return $null
    }

    $fields = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, "([A-Za-z0-9_]+)=([^ ]+)")) {
        $fields[$match.Groups[1].Value] = $match.Groups[2].Value
    }

    if (!$fields.Contains("shader")) {
        return $null
    }

    $drawValue = $null
    if ($fields.Contains("draw")) {
        $drawValue = [int]$fields["draw"]
    }

    return [pscustomobject]@{
        Backend = $Backend
        Ordinal = $Ordinal
        Draw = $drawValue
        Shader = $fields["shader"]
        Kind = if ($fields.Contains("kind")) { $fields["kind"] } else { "" }
        Fields = $fields
        RawLine = $Line
        RawSha256 = Get-Sha256Text $Line
    }
}

function Read-Inventory {
    param(
        [string]$Backend,
        [string]$Path
    )

    if (!(Test-Path -LiteralPath $Path)) {
        throw "Missing $Backend dbwin log: $Path"
    }

    $rows = @()
    $ordinal = 0
    foreach ($line in Get-Content -LiteralPath $Path) {
        $row = Convert-InventoryLine -Backend $Backend -Line $line -Ordinal ($ordinal + 1)
        if ($row) {
            ++$ordinal
            $rows += $row
        }
    }

    return $rows
}

function Set-ShaderOccurrences {
    param([object[]]$Rows)

    $occurrences = @{}
    foreach ($row in $Rows) {
        $shader = $row.Shader
        if (!$occurrences.ContainsKey($shader)) {
            $occurrences[$shader] = 0
        }
        ++$occurrences[$shader]
        $row | Add-Member -Force -NotePropertyName ShaderOccurrence -NotePropertyValue $occurrences[$shader]
        $row | Add-Member -Force -NotePropertyName OccurrenceKey -NotePropertyValue ("{0}#{1:D5}" -f $shader, $occurrences[$shader])
    }

    return $Rows
}

function Get-ComparableDrawCount {
    param([object]$Row)

    if (!$Row) {
        return ""
    }

    if ($Row.Backend -eq "D3D9") {
        if ((Get-FieldValue -Row $Row -Name "kind") -eq "drawIndexed") {
            return Get-FieldValue -Row $Row -Name "indices"
        }

        return ""
    }

    return Get-FieldValue -Row $Row -Name "count"
}

function Set-ComparableCountOccurrences {
    param([object[]]$Rows)

    $occurrences = @{}
    foreach ($row in $Rows) {
        $count = Get-ComparableDrawCount -Row $row
        if (!$count) {
            $row | Add-Member -Force -NotePropertyName ComparableCountKey -NotePropertyValue ""
            continue
        }

        $key = "{0}|{1}" -f $row.Shader, $count
        if (!$occurrences.ContainsKey($key)) {
            $occurrences[$key] = 0
        }
        ++$occurrences[$key]
        $row | Add-Member -Force -NotePropertyName ComparableCountKey -NotePropertyValue ("{0}|{1}#{2:D5}" -f $row.Shader, $count, $occurrences[$key])
    }

    return $Rows
}

function Export-Ledger {
    param(
        [object[]]$Rows,
        [string]$Path
    )

    $fieldNames = New-Object System.Collections.Generic.SortedSet[string]
    foreach ($row in $Rows) {
        foreach ($key in $row.Fields.Keys) {
            [void]$fieldNames.Add($key)
        }
    }

    $csvRows = foreach ($row in $Rows) {
        $properties = [ordered]@{
            Backend = $row.Backend
            Ordinal = $row.Ordinal
            Draw = $row.Draw
            Shader = $row.Shader
            ShaderOccurrence = $row.ShaderOccurrence
            OccurrenceKey = $row.OccurrenceKey
            ComparableCountKey = if ($row.PSObject.Properties.Name -contains "ComparableCountKey") { $row.ComparableCountKey } else { "" }
            RawSha256 = $row.RawSha256
        }

        foreach ($fieldName in $fieldNames) {
            $properties[$fieldName] = if ($row.Fields.Contains($fieldName)) { $row.Fields[$fieldName] } else { "" }
        }

        $properties["RawLine"] = $row.RawLine
        [pscustomobject]$properties
    }

    $csvRows | Export-Csv -LiteralPath $Path -NoTypeInformation
}

function Get-InventoryFrame {
    param([object]$Row)

    $frame = Get-FieldValue -Row $Row -Name "frame"
    if ($null -eq $frame -or $frame -eq "") {
        return $null
    }

    return [int]$frame
}

function Get-FieldValue {
    param(
        [object]$Row,
        [string]$Name
    )

    if ($Row -and $Row.Fields.Contains($Name)) {
        return $Row.Fields[$Name]
    }

    return $null
}

function Get-CountsByShader {
    param([object[]]$Rows)

    $counts = @{}
    foreach ($row in $Rows) {
        if (!$counts.ContainsKey($row.Shader)) {
            $counts[$row.Shader] = 0
        }
        ++$counts[$row.Shader]
    }

    return $counts
}

$d3d9ToD3d11ShaderAliases = @{
    "shader/skybox.sht" = "shader/gradient_sky.sht"
    "shader/terrain_blend0.sht" = "shader/terrain_dot3_blend0_spec.sht"
    "shader/terrain_blend1.sht" = "shader/terrain_dot3_blend1_spec.sht"
    "shader/terrain_blend2.sht" = "shader/terrain_dot3_blend2_spec.sht"
    "shader/terrain_blend3.sht" = "shader/terrain_dot3_blend3_spec.sht"
}

function Get-NormalizedShader {
    param([string]$Shader)

    if ($d3d9ToD3d11ShaderAliases.ContainsKey($Shader)) {
        return $d3d9ToD3d11ShaderAliases[$Shader]
    }

    return $Shader
}

function Get-FrameShaderCounts {
    param([object[]]$Rows)

    $counts = @{}
    foreach ($row in $Rows) {
        $shader = Get-NormalizedShader $row.Shader
        if (!$counts.ContainsKey($shader)) {
            $counts[$shader] = 0
        }
        ++$counts[$shader]
    }

    return $counts
}

function Test-UiOrLoadingShader {
    param([string]$Shader)

    if (!$Shader) {
        return $true
    }

    return $Shader -match "^shader/(uicanvas|ui_|2d_|font|text)"
}

function Get-WorldRowCount {
    param([object[]]$Rows)

    return @($Rows | Where-Object { !(Test-UiOrLoadingShader $_.Shader) }).Count
}

function Test-InGameWorldShader {
    param([string]$Shader)

    if (!$Shader) {
        return $false
    }

    return $Shader -match "^shader/(terrain_|skybox|stars|sun_|cloudtile|cels_moon|wter_|pt_|stco_|tato_|tatt_|thm_|mun_|radl_|glss_|metl_|all_|yavin_|ins_)"
}

function Get-InGameWorldRowCount {
    param([object[]]$Rows)

    return @($Rows | Where-Object { Test-InGameWorldShader $_.Shader }).Count
}

function Get-FrameOverlapScore {
    param(
        [hashtable]$LeftCounts,
        [hashtable]$RightCounts,
        [int]$LeftRowCount,
        [int]$RightRowCount
    )

    $intersection = 0
    foreach ($shader in $LeftCounts.Keys) {
        if ($RightCounts.ContainsKey($shader)) {
            $intersection += [Math]::Min($LeftCounts[$shader], $RightCounts[$shader])
        }
    }

    $unionRows = [Math]::Max(1, $LeftRowCount + $RightRowCount - $intersection)
    $jaccard = [double]$intersection / [double]$unionRows
    $rowPenalty = [Math]::Abs($LeftRowCount - $RightRowCount) * 0.001
    return $jaccard - $rowPenalty
}

$compareRootPath = (Resolve-Path -LiteralPath $CompareRoot).Path
$d3d9Dbwin = @(Get-ChildItem -LiteralPath $compareRootPath -Recurse -Filter dbwin.txt | Where-Object { $_.FullName -match "\\d3d9\\dbwin\.txt$|[-_]d3d9\\dbwin\.txt$" } | Select-Object -First 1)
$d3d11Dbwin = @(Get-ChildItem -LiteralPath $compareRootPath -Recurse -Filter dbwin.txt | Where-Object { $_.FullName -match "\\d3d11\\dbwin\.txt$|[-_]d3d11\\dbwin\.txt$" } | Select-Object -First 1)

if ($d3d9Dbwin.Count -eq 0) {
    $d3d9Dbwin = @(Get-ChildItem -LiteralPath $compareRootPath -Recurse -Filter dbwin.txt | Where-Object { $_.Directory.Name -match "d3d9" } | Select-Object -First 1)
}
if ($d3d11Dbwin.Count -eq 0) {
    $d3d11Dbwin = @(Get-ChildItem -LiteralPath $compareRootPath -Recurse -Filter dbwin.txt | Where-Object { $_.Directory.Name -match "d3d11" } | Select-Object -First 1)
}
if ($d3d9Dbwin.Count -eq 0 -or $d3d11Dbwin.Count -eq 0) {
    throw "Could not find both D3D9 and D3D11 dbwin.txt files under $compareRootPath"
}

$outputRoot = Join-Path $compareRootPath "renderer-inventory"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$d3d9Rows = Read-Inventory -Backend "D3D9" -Path $d3d9Dbwin[0].FullName
$d3d11Rows = Read-Inventory -Backend "D3D11" -Path $d3d11Dbwin[0].FullName

$allD3d9Rows = Set-ShaderOccurrences $d3d9Rows
$allD3d11Rows = Set-ShaderOccurrences $d3d11Rows
$allD3d9Rows = Set-ComparableCountOccurrences $allD3d9Rows
$allD3d11Rows = Set-ComparableCountOccurrences $allD3d11Rows
$selectedFrame = $null
$selectedD3D9Frame = $null
$selectedD3D11Frame = $null
$frameOverlapScore = $null
$frameSelectionApplied = $false
$d3d9WorldFrameGroupCount = 0
$d3d11WorldFrameGroupCount = 0
$d3d9InGameWorldFrameGroupCount = 0
$d3d11InGameWorldFrameGroupCount = 0
$frameSelectionRequiredWorldRows = $false
$frameSelectionWorldRowsSatisfied = $false
$frameSelectionRequiredInGameWorldRows = $false
$frameSelectionInGameWorldRowsSatisfied = $false
$selectedD3D9WorldRows = $null
$selectedD3D11WorldRows = $null
$selectedD3D9InGameWorldRows = $null
$selectedD3D11InGameWorldRows = $null

if ($FrameMode -eq "BestShaderOverlap") {
    $d3d9FrameGroups = @($allD3d9Rows | Group-Object { Get-InventoryFrame $_ } | Where-Object { $_.Name -ne "" -and $_.Count -ge $MinimumFrameRows })
    $d3d11FrameGroups = @($allD3d11Rows | Group-Object { Get-InventoryFrame $_ } | Where-Object { $_.Name -ne "" -and $_.Count -ge $MinimumFrameRows })
    if ($d3d9FrameGroups.Count -eq 0 -or $d3d11FrameGroups.Count -eq 0) {
        $d3d9FrameGroups = @($allD3d9Rows | Group-Object { Get-InventoryFrame $_ } | Where-Object { $_.Name -ne "" -and $_.Count -gt 0 })
        $d3d11FrameGroups = @($allD3d11Rows | Group-Object { Get-InventoryFrame $_ } | Where-Object { $_.Name -ne "" -and $_.Count -gt 0 })
    }
    $d3d9WorldFrameGroups = @($d3d9FrameGroups | Where-Object { (Get-WorldRowCount $_.Group) -gt 0 })
    $d3d11WorldFrameGroups = @($d3d11FrameGroups | Where-Object { (Get-WorldRowCount $_.Group) -gt 0 })
    $d3d9InGameWorldFrameGroups = @($d3d9FrameGroups | Where-Object { (Get-InGameWorldRowCount $_.Group) -gt 0 })
    $d3d11InGameWorldFrameGroups = @($d3d11FrameGroups | Where-Object { (Get-InGameWorldRowCount $_.Group) -gt 0 })
    $d3d9WorldFrameGroupCount = $d3d9WorldFrameGroups.Count
    $d3d11WorldFrameGroupCount = $d3d11WorldFrameGroups.Count
    $d3d9InGameWorldFrameGroupCount = $d3d9InGameWorldFrameGroups.Count
    $d3d11InGameWorldFrameGroupCount = $d3d11InGameWorldFrameGroups.Count
    if ($d3d9InGameWorldFrameGroupCount -gt 0 -or $d3d11InGameWorldFrameGroupCount -gt 0) {
        $frameSelectionRequiredInGameWorldRows = $true
    }
    if ($d3d9InGameWorldFrameGroupCount -gt 0 -and $d3d11InGameWorldFrameGroupCount -gt 0) {
        $d3d9FrameGroups = $d3d9InGameWorldFrameGroups
        $d3d11FrameGroups = $d3d11InGameWorldFrameGroups
        $frameSelectionInGameWorldRowsSatisfied = $true
        $frameSelectionWorldRowsSatisfied = $true
    }
    if ($d3d9WorldFrameGroupCount -gt 0 -or $d3d11WorldFrameGroupCount -gt 0) {
        $frameSelectionRequiredWorldRows = $true
    }
    if (!$frameSelectionInGameWorldRowsSatisfied -and $d3d9WorldFrameGroupCount -gt 0 -and $d3d11WorldFrameGroupCount -gt 0) {
        $d3d9FrameGroups = $d3d9WorldFrameGroups
        $d3d11FrameGroups = $d3d11WorldFrameGroups
        $frameSelectionWorldRowsSatisfied = $true
    }
    $bestPair = $null
    foreach ($d3d9FrameGroup in $d3d9FrameGroups) {
        $d3d9FrameRows = @($d3d9FrameGroup.Group)
        $d3d9CountsForFrame = Get-FrameShaderCounts $d3d9FrameRows
        foreach ($d3d11FrameGroup in $d3d11FrameGroups) {
            $d3d11FrameRows = @($d3d11FrameGroup.Group)
            $score = Get-FrameOverlapScore -LeftCounts $d3d9CountsForFrame -RightCounts (Get-FrameShaderCounts $d3d11FrameRows) -LeftRowCount $d3d9FrameRows.Count -RightRowCount $d3d11FrameRows.Count
            $d3d9FrameNumber = [int]$d3d9FrameGroup.Name
            $d3d11FrameNumber = [int]$d3d11FrameGroup.Name
            $scoreTie = $bestPair -and ([Math]::Abs($score - $bestPair.Score) -lt 0.0000001)
            if (!$bestPair -or $score -gt $bestPair.Score -or ($scoreTie -and (($d3d9FrameNumber + $d3d11FrameNumber) -gt ($bestPair.D3D9Frame + $bestPair.D3D11Frame)))) {
                $bestPair = [pscustomobject]@{
                    D3D9Frame = $d3d9FrameNumber
                    D3D11Frame = $d3d11FrameNumber
                    Score = $score
                }
            }
        }
    }

    if ($bestPair) {
        $selectedD3D9Frame = $bestPair.D3D9Frame
        $selectedD3D11Frame = $bestPair.D3D11Frame
        $selectedFrame = if ($selectedD3D9Frame -eq $selectedD3D11Frame) { $selectedD3D9Frame } else { $null }
        $frameOverlapScore = $bestPair.Score
        $d3d9Rows = @($allD3d9Rows | Where-Object { (Get-InventoryFrame $_) -eq $selectedD3D9Frame })
        $d3d11Rows = @($allD3d11Rows | Where-Object { (Get-InventoryFrame $_) -eq $selectedD3D11Frame })
        $d3d9Rows = Set-ShaderOccurrences $d3d9Rows
        $d3d11Rows = Set-ShaderOccurrences $d3d11Rows
        $d3d9Rows = Set-ComparableCountOccurrences $d3d9Rows
        $d3d11Rows = Set-ComparableCountOccurrences $d3d11Rows
        $selectedD3D9WorldRows = Get-WorldRowCount $d3d9Rows
        $selectedD3D11WorldRows = Get-WorldRowCount $d3d11Rows
        $selectedD3D9InGameWorldRows = Get-InGameWorldRowCount $d3d9Rows
        $selectedD3D11InGameWorldRows = Get-InGameWorldRowCount $d3d11Rows
        $frameSelectionApplied = $true
    }
}
elseif ($FrameMode -eq "LastCommon") {
    $d3d9Frames = @($allD3d9Rows | ForEach-Object { Get-InventoryFrame $_ } | Where-Object { $null -ne $_ })
    $d3d11Frames = @($allD3d11Rows | ForEach-Object { Get-InventoryFrame $_ } | Where-Object { $null -ne $_ })
    if ($d3d9Frames.Count -gt 0 -and $d3d11Frames.Count -gt 0) {
        $maxCommonFrame = [Math]::Min((($d3d9Frames | Measure-Object -Maximum).Maximum), (($d3d11Frames | Measure-Object -Maximum).Maximum))
        $selectedFrame = [Math]::Max(0, [int]$maxCommonFrame - 1)
        $selectedD3D9Frame = $selectedFrame
        $selectedD3D11Frame = $selectedFrame
        $d3d9Rows = @($allD3d9Rows | Where-Object { (Get-InventoryFrame $_) -eq $selectedD3D9Frame })
        $d3d11Rows = @($allD3d11Rows | Where-Object { (Get-InventoryFrame $_) -eq $selectedD3D11Frame })
        $d3d9Rows = Set-ShaderOccurrences $d3d9Rows
        $d3d11Rows = Set-ShaderOccurrences $d3d11Rows
        $d3d9Rows = Set-ComparableCountOccurrences $d3d9Rows
        $d3d11Rows = Set-ComparableCountOccurrences $d3d11Rows
        $selectedD3D9WorldRows = Get-WorldRowCount $d3d9Rows
        $selectedD3D11WorldRows = Get-WorldRowCount $d3d11Rows
        $selectedD3D9InGameWorldRows = Get-InGameWorldRowCount $d3d9Rows
        $selectedD3D11InGameWorldRows = Get-InGameWorldRowCount $d3d11Rows
        $frameSelectionApplied = $true
    }
}
elseif ($FrameMode -eq "LatestInGameWorld") {
    $d3d9AllInGameFrameGroups = @($allD3d9Rows | Group-Object { Get-InventoryFrame $_ } | Where-Object { $_.Name -ne "" -and (Get-InGameWorldRowCount $_.Group) -gt 0 })
    $d3d11AllInGameFrameGroups = @($allD3d11Rows | Group-Object { Get-InventoryFrame $_ } | Where-Object { $_.Name -ne "" -and (Get-InGameWorldRowCount $_.Group) -gt 0 })
    $d3d9FrameGroups = @($d3d9AllInGameFrameGroups | Where-Object { $_.Count -ge $MinimumFrameRows })
    $d3d11FrameGroups = @($d3d11AllInGameFrameGroups | Where-Object { $_.Count -ge $MinimumFrameRows })
    if ($d3d9FrameGroups.Count -eq 0) {
        $d3d9FrameGroups = $d3d9AllInGameFrameGroups
    }
    if ($d3d11FrameGroups.Count -eq 0) {
        $d3d11FrameGroups = $d3d11AllInGameFrameGroups
    }
    $d3d9InGameWorldFrameGroupCount = $d3d9FrameGroups.Count
    $d3d11InGameWorldFrameGroupCount = $d3d11FrameGroups.Count
    $d3d9WorldFrameGroupCount = @($allD3d9Rows | Group-Object { Get-InventoryFrame $_ } | Where-Object { $_.Name -ne "" -and (Get-WorldRowCount $_.Group) -gt 0 }).Count
    $d3d11WorldFrameGroupCount = @($allD3d11Rows | Group-Object { Get-InventoryFrame $_ } | Where-Object { $_.Name -ne "" -and (Get-WorldRowCount $_.Group) -gt 0 }).Count
    $frameSelectionRequiredWorldRows = ($d3d9WorldFrameGroupCount -gt 0 -or $d3d11WorldFrameGroupCount -gt 0)
    $frameSelectionRequiredInGameWorldRows = ($d3d9InGameWorldFrameGroupCount -gt 0 -or $d3d11InGameWorldFrameGroupCount -gt 0)
    if ($d3d9FrameGroups.Count -gt 0 -and $d3d11FrameGroups.Count -gt 0) {
        $d3d9LatestFrame = ($d3d9FrameGroups | ForEach-Object { [int]$_.Name } | Measure-Object -Maximum).Maximum
        $d3d11LatestFrame = ($d3d11FrameGroups | ForEach-Object { [int]$_.Name } | Measure-Object -Maximum).Maximum
        $selectedD3D9Frame = [int]$d3d9LatestFrame
        $selectedD3D11Frame = [int]$d3d11LatestFrame
        $selectedFrame = if ($selectedD3D9Frame -eq $selectedD3D11Frame) { $selectedD3D9Frame } else { $null }
        $d3d9Rows = @($allD3d9Rows | Where-Object { (Get-InventoryFrame $_) -eq $selectedD3D9Frame })
        $d3d11Rows = @($allD3d11Rows | Where-Object { (Get-InventoryFrame $_) -eq $selectedD3D11Frame })
        $d3d9Rows = Set-ShaderOccurrences $d3d9Rows
        $d3d11Rows = Set-ShaderOccurrences $d3d11Rows
        $d3d9Rows = Set-ComparableCountOccurrences $d3d9Rows
        $d3d11Rows = Set-ComparableCountOccurrences $d3d11Rows
        $selectedD3D9WorldRows = Get-WorldRowCount $d3d9Rows
        $selectedD3D11WorldRows = Get-WorldRowCount $d3d11Rows
        $selectedD3D9InGameWorldRows = Get-InGameWorldRowCount $d3d9Rows
        $selectedD3D11InGameWorldRows = Get-InGameWorldRowCount $d3d11Rows
        $frameSelectionWorldRowsSatisfied = ($selectedD3D9WorldRows -gt 0 -and $selectedD3D11WorldRows -gt 0)
        $frameSelectionInGameWorldRowsSatisfied = ($selectedD3D9InGameWorldRows -gt 0 -and $selectedD3D11InGameWorldRows -gt 0)
        $frameSelectionApplied = $true
    }
}
if (!$frameSelectionApplied) {
    $selectedD3D9WorldRows = Get-WorldRowCount $d3d9Rows
    $selectedD3D11WorldRows = Get-WorldRowCount $d3d11Rows
    $selectedD3D9InGameWorldRows = Get-InGameWorldRowCount $d3d9Rows
    $selectedD3D11InGameWorldRows = Get-InGameWorldRowCount $d3d11Rows
}

Export-Ledger -Rows $allD3d9Rows -Path (Join-Path $outputRoot "d3d9-ledger-all.csv")
Export-Ledger -Rows $allD3d11Rows -Path (Join-Path $outputRoot "d3d11-ledger-all.csv")
Export-Ledger -Rows $d3d9Rows -Path (Join-Path $outputRoot "d3d9-ledger.csv")
Export-Ledger -Rows $d3d11Rows -Path (Join-Path $outputRoot "d3d11-ledger.csv")

$d3d9ByKey = @{}
$d3d11ByKey = @{}
$d3d11ByComparableCountKey = @{}
foreach ($row in $d3d9Rows) { $d3d9ByKey[$row.OccurrenceKey] = $row }
foreach ($row in $d3d11Rows) {
    $d3d11ByKey[$row.OccurrenceKey] = $row
    if ($row.PSObject.Properties.Name -contains "ComparableCountKey" -and $row.ComparableCountKey) {
        $d3d11ByComparableCountKey[$row.ComparableCountKey] = $row
    }
}
$matchedD3d11Keys = @{}

$d3d9Counts = Get-CountsByShader $d3d9Rows
$d3d11Counts = Get-CountsByShader $d3d11Rows
$allShaders = New-Object System.Collections.Generic.SortedSet[string]
foreach ($key in $d3d9Counts.Keys) { [void]$allShaders.Add($key) }
foreach ($key in $d3d11Counts.Keys) { [void]$allShaders.Add($key) }

$shaderSummary = foreach ($shader in $allShaders) {
    $count9 = if ($d3d9Counts.ContainsKey($shader)) { $d3d9Counts[$shader] } else { 0 }
    $count11 = if ($d3d11Counts.ContainsKey($shader)) { $d3d11Counts[$shader] } else { 0 }
    [pscustomobject]@{
        Shader = $shader
        D3D9Count = $count9
        D3D11Count = $count11
        DeltaD3D11MinusD3D9 = $count11 - $count9
        Status = if ($count9 -eq $count11) { "count-match" } elseif ($count9 -eq 0) { "d3d11-only" } elseif ($count11 -eq 0) { "missing-from-d3d11" } else { "count-mismatch" }
    }
}
$shaderSummary | Export-Csv -LiteralPath (Join-Path $outputRoot "shader-summary.csv") -NoTypeInformation

function Add-FieldDifference {
    param(
        [System.Collections.Generic.List[string]]$Differences,
        [object]$LeftRow,
        [object]$RightRow,
        [string]$Field
    )

    $left = Get-FieldValue -Row $LeftRow -Name $Field
    $right = Get-FieldValue -Row $RightRow -Name $Field
    if ($null -ne $left -and $null -ne $right -and $left -ne $right) {
        $Differences.Add(("{0}:{1}!={2}" -f $Field, $left, $right))
    }
}

function Test-FieldEnabled {
    param(
        [object]$LeftRow,
        [object]$RightRow,
        [string]$Field
    )

    return ((Get-FieldValue -Row $LeftRow -Name $Field) -eq "1") -or ((Get-FieldValue -Row $RightRow -Name $Field) -eq "1")
}

function Test-D3D11EmulatedPrimitive {
    param(
        [object]$Dx9Row,
        [object]$Dx11Row
    )

    if (!$Dx9Row -or !$Dx11Row) {
        return $false
    }

    $dx11Kind = Get-FieldValue -Row $Dx11Row -Name "kind"
    $dx9Kind = Get-FieldValue -Row $Dx9Row -Name "kind"
    if ($dx11Kind -eq "triangleFan") {
        return $dx9Kind -eq "draw"
    }

    if ($dx11Kind -eq "indexedTriangleFan") {
        return $dx9Kind -eq "drawIndexed"
    }

    if ($dx11Kind -eq "quadList") {
        return $dx9Kind -eq "drawIndexed"
    }

    return $false
}

function Test-Dx9FoldedSpecularPass {
    param([object]$Row)

    return ((Get-FieldValue -Row $Row -Name "pass") -ne "0") -and
        ((Get-FieldValue -Row $Row -Name "alphaBlend") -eq "1") -and
        ((Get-FieldValue -Row $Row -Name "src") -eq "5") -and
        ((Get-FieldValue -Row $Row -Name "dst") -eq "2") -and
        ((Get-FieldValue -Row $Row -Name "write") -eq "0") -and
        ((Get-FieldValue -Row $Row -Name "alphaTest") -eq "1")
}

function Find-FoldedSpecularD3D11Row {
    param(
        [object]$Dx9Row,
        [object[]]$D3D11Rows
    )

    if (!(Test-Dx9FoldedSpecularPass $Dx9Row)) {
        return $null
    }

    $dx9Indices = Get-FieldValue -Row $Dx9Row -Name "indices"
    foreach ($candidate in $D3D11Rows) {
        if ($candidate.Shader -ne $Dx9Row.Shader) {
            continue
        }

        $program = Get-FieldValue -Row $candidate -Name "pprogram"
        $count = Get-FieldValue -Row $candidate -Name "count"
        if ($program -and $program -match "specmap" -and (!$dx9Indices -or $count -eq $dx9Indices)) {
            return $candidate
        }
    }

    return $null
}

function Resolve-D3D11RowForDx9Row {
    param([object]$Dx9Row)

    $aliasShader = if ($d3d9ToD3d11ShaderAliases.ContainsKey($Dx9Row.Shader)) { $d3d9ToD3d11ShaderAliases[$Dx9Row.Shader] } else { "" }
    $aliasKey = if ($aliasShader) { "{0}#{1:D5}" -f $aliasShader, $Dx9Row.ShaderOccurrence } else { "" }
    $countKey = if ($Dx9Row.PSObject.Properties.Name -contains "ComparableCountKey") { $Dx9Row.ComparableCountKey } else { "" }
    $dx11 = if ($countKey -and $d3d11ByComparableCountKey.ContainsKey($countKey)) { $d3d11ByComparableCountKey[$countKey] } elseif ($d3d11ByKey.ContainsKey($Dx9Row.OccurrenceKey)) { $d3d11ByKey[$Dx9Row.OccurrenceKey] } else { $null }
    $mappedByAlias = $false
    $mappedByFoldedSpecularPass = $false

    if (!$dx11 -and $aliasKey -and $d3d11ByKey.ContainsKey($aliasKey)) {
        $dx11 = $d3d11ByKey[$aliasKey]
        $mappedByAlias = $true
    }
    if (!$dx11) {
        $foldedSpecularPass = Find-FoldedSpecularD3D11Row -Dx9Row $Dx9Row -D3D11Rows $d3d11Rows
        if ($foldedSpecularPass) {
            $dx11 = $foldedSpecularPass
            $mappedByFoldedSpecularPass = $true
        }
    }

    return [pscustomobject]@{
        Row = $dx11
        AliasShader = $aliasShader
        AliasOccurrenceKey = $aliasKey
        MappedByAlias = $mappedByAlias
        MappedByFoldedSpecularPass = $mappedByFoldedSpecularPass
    }
}

function Get-D3D11FieldNameForD3D9Field {
    param([string]$Field)

    switch ($Field) {
        "alphaRef" { return "ref" }
        "indices" { return "count" }
        "prim" { return "topo" }
        "sliceIndices" { return "count" }
        default { return $Field }
    }
}

function Get-AccountingActionClass {
    param([string]$Status)

    if ($Status -eq "byte-exact" -or $Status -eq "value-exact") {
        return "exact-or-equivalent"
    }

    if ($Status -eq "mismatch" -or $Status -like "*mismatch" -or $Status -eq "no-d3d11-logged-field" -or $Status -eq "no-mapped-d3d11-draw") {
        return "fix-required"
    }

    return "accounted-normalized"
}

function Convert-PackedD3D9ColorToFloatText {
    param([string]$Value)

    if ($Value -notmatch "^0x([0-9a-fA-F]{8})$") {
        return $Value
    }

    $number = [Convert]::ToUInt32($Matches[1], 16)
    $r = (($number -shr 16) -band 0xff) / 255.0
    $g = (($number -shr 8) -band 0xff) / 255.0
    $b = ($number -band 0xff) / 255.0
    return ("{0:0.000},{1:0.000},{2:0.000}" -f $r, $g, $b)
}

function Convert-D3D9CompareToShaderCompareText {
    param([string]$Value)

    $compare = 0
    if ([int]::TryParse($Value, [ref]$compare) -and $compare -ge 1 -and $compare -le 8) {
        return ($compare - 1).ToString([System.Globalization.CultureInfo]::InvariantCulture)
    }

    return $Value
}

function Convert-D3D9AlphaRefToFloatText {
    param([string]$Value)

    $alphaRef = 0
    if ([int]::TryParse($Value, [ref]$alphaRef)) {
        return ("{0:0.000}" -f ([Math]::Max(0, [Math]::Min(255, $alphaRef)) / 255.0))
    }

    return $Value
}

function Get-StageFieldIndex {
    param([string]$Field)

    if ($Field -match "^s([0-9]+)(fmt|tci|cop|c0|c1|c2|aop|a0|a1|a2|res)$") {
        return [int]$Matches[1]
    }

    return $null
}

function Get-IntegerFieldValue {
    param(
        [object]$Row,
        [string]$Name
    )

    $value = Get-FieldValue -Row $Row -Name $Name
    $parsed = 0
    if ([int]::TryParse($value, [ref]$parsed)) {
        return $parsed
    }

    return $null
}

function Test-NumericEquivalent {
    param(
        [string]$Left,
        [string]$Right
    )

    $leftValue = 0.0
    $rightValue = 0.0
    if ([double]::TryParse($Left, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$leftValue) -and
        [double]::TryParse($Right, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$rightValue)) {
        return [Math]::Abs($leftValue - $rightValue) -lt 0.0005
    }

    return $false
}

function Test-TripletEquivalent {
    param(
        [string]$Left,
        [string]$Right
    )

    $leftParts = @($Left -split ",")
    $rightParts = @($Right -split ",")
    if ($leftParts.Count -ne 3 -or $rightParts.Count -ne 3) {
        return $false
    }

    for ($i = 0; $i -lt 3; ++$i) {
        if (!(Test-NumericEquivalent -Left $leftParts[$i] -Right $rightParts[$i])) {
            return $false
        }
    }

    return $true
}

$mappingRows = foreach ($dx9 in $d3d9Rows) {
    $resolved = Resolve-D3D11RowForDx9Row -Dx9Row $dx9
    $dx11 = $resolved.Row
    $aliasShader = $resolved.AliasShader
    $aliasKey = $resolved.AliasOccurrenceKey
    $mappedByAlias = $resolved.MappedByAlias
    $mappedByFoldedSpecularPass = $resolved.MappedByFoldedSpecularPass

    $differences = New-Object System.Collections.Generic.List[string]
    if ($dx11) {
        $matchedD3d11Keys[$dx11.OccurrenceKey] = $true
        foreach ($field in @("base", "start", "alphaBlend", "depth", "write")) {
            Add-FieldDifference -Differences $differences -LeftRow $dx9 -RightRow $dx11 -Field $field
        }
        if (!(Test-D3D11EmulatedPrimitive -Dx9Row $dx9 -Dx11Row $dx11)) {
            Add-FieldDifference -Differences $differences -LeftRow $dx9 -RightRow $dx11 -Field "kind"
        }

        $dx9Program = Get-FieldValue -Row $dx9 -Name "pprogram"
        $dx11Program = Get-FieldValue -Row $dx11 -Name "pprogram"
        if ($dx9Program -and $dx9Program -ne "<null>" -and $dx11Program -and $dx9Program -ne $dx11Program) {
            $differences.Add(("pprogram:{0}!={1}" -f $dx9Program, $dx11Program))
        }

        if (Test-FieldEnabled -LeftRow $dx9 -RightRow $dx11 -Field "alphaBlend") {
            foreach ($field in @("src", "dst", "op")) {
                Add-FieldDifference -Differences $differences -LeftRow $dx9 -RightRow $dx11 -Field $field
            }
        }

        if (Test-FieldEnabled -LeftRow $dx9 -RightRow $dx11 -Field "alphaTest") {
            Add-FieldDifference -Differences $differences -LeftRow $dx9 -RightRow $dx11 -Field "alphaTest"
            Add-FieldDifference -Differences $differences -LeftRow $dx9 -RightRow $dx11 -Field "cmp"
        }

        $dx9Indices = Get-FieldValue -Row $dx9 -Name "indices"
        $dx11Count = Get-FieldValue -Row $dx11 -Name "count"
        if ((Get-FieldValue -Row $dx9 -Name "kind") -eq "drawIndexed" -and !(Test-D3D11EmulatedPrimitive -Dx9Row $dx9 -Dx11Row $dx11) -and $dx9Indices -and $dx11Count -and $dx9Indices -ne $dx11Count) {
            $differences.Add(("indexCount:{0}!={1}" -f $dx9Indices, $dx11Count))
        }
    }

    [pscustomobject]@{
        D3D9Ordinal = $dx9.Ordinal
        D3D9Draw = $dx9.Draw
        Shader = $dx9.Shader
        ShaderOccurrence = $dx9.ShaderOccurrence
        OccurrenceKey = $dx9.OccurrenceKey
        AliasShader = $aliasShader
        AliasOccurrenceKey = $aliasKey
        Status = if ($mappedByFoldedSpecularPass -and $differences.Count -gt 0) { "mapped-via-folded-specular-pass-field-mismatch" } elseif ($mappedByFoldedSpecularPass) { "mapped-via-folded-specular-pass" } elseif ($mappedByAlias -and $differences.Count -gt 0) { "mapped-via-alias-field-mismatch" } elseif ($mappedByAlias) { "mapped-via-alias" } elseif (!$d3d11Counts.ContainsKey($dx9.Shader)) { "missing-shader-from-d3d11" } elseif (!$dx11) { "missing-occurrence-from-d3d11" } elseif ($differences.Count -gt 0) { "mapped-field-mismatch" } else { "mapped" }
        Differences = ($differences -join ";")
        D3D9RawSha256 = $dx9.RawSha256
        D3D11Ordinal = if ($dx11) { $dx11.Ordinal } else { $null }
        D3D11Draw = if ($dx11) { $dx11.Draw } else { $null }
        D3D11RawSha256 = if ($dx11) { $dx11.RawSha256 } else { "" }
        D3D9RawLine = $dx9.RawLine
        D3D11RawLine = if ($dx11) { $dx11.RawLine } else { "" }
    }
}
$mappingRows | Export-Csv -LiteralPath (Join-Path $outputRoot "dx9-to-dx11-map.csv") -NoTypeInformation

$inactiveBlendFields = @("src", "dst", "op")
$inactiveAlphaTestFields = @("cmp", "alphaRef")
$foldedSpecularStateFields = @("alphaBlend", "src", "dst", "op", "alphaTest", "cmp", "alphaRef", "depth", "write", "pprogram")
$captureMetadataFields = @("frame", "draw")
$derivedMetadataFields = @("pass", "primCount", "sliceVerts", "sliceIndices")

$fieldAccountingRows = foreach ($dx9 in $d3d9Rows) {
    $resolved = Resolve-D3D11RowForDx9Row -Dx9Row $dx9
    $dx11 = $resolved.Row

    foreach ($field in @($dx9.Fields.Keys | Sort-Object)) {
        $dx9Value = Get-FieldValue -Row $dx9 -Name $field
        $d3d11Field = Get-D3D11FieldNameForD3D9Field -Field $field
        if ($field -eq "fogColor" -and $dx11 -and $dx11.Fields.Contains("fogColorPacked")) {
            $d3d11Field = "fogColorPacked"
        }
        $d3d11Value = if ($dx11 -and $dx11.Fields.Contains($d3d11Field)) { $dx11.Fields[$d3d11Field] } else { "" }
        $normalizedDx9Value = if ($field -eq "fogColor" -and $d3d11Field -ne "fogColorPacked") { Convert-PackedD3D9ColorToFloatText -Value $dx9Value } elseif ($field -eq "cmp") { Convert-D3D9CompareToShaderCompareText -Value $dx9Value } elseif ($field -eq "alphaRef") { Convert-D3D9AlphaRefToFloatText -Value $dx9Value } else { $dx9Value }
        $normalizedD3D11Value = $d3d11Value
        $byteExact = ($dx9Value -eq $d3d11Value)
        $valueEquivalent = $byteExact -or (Test-NumericEquivalent -Left $normalizedDx9Value -Right $normalizedD3D11Value) -or (($field -eq "fogColor" -and $d3d11Field -ne "fogColorPacked") -and (Test-TripletEquivalent -Left $normalizedDx9Value -Right $normalizedD3D11Value))

        $status = "mismatch"
        $reason = ""

        if (!$dx11) {
            $status = "no-mapped-d3d11-draw"
            $reason = "The DX9 draw did not resolve to a D3D11 draw row."
        }
        elseif ($captureMetadataFields -contains $field) {
            $status = "capture-metadata"
            $reason = "Capture sequencing metadata is logged but is not a renderer state parity byte."
        }
        elseif ($field -eq "shader" -and $resolved.MappedByAlias -and $d3d11Value -eq $resolved.AliasShader) {
            $status = "backend-alias-exact"
            $reason = "D3D11 intentionally uses the configured backend shader alias for this DX9 shader."
        }
        elseif ($field -eq "pprogram" -and $dx9Value -eq "<null>" -and $d3d11Value) {
            $status = "fixed-function-synthesized"
            $reason = "DX9 fixed-function pass is represented by a synthetic D3D11 pixel program."
        }
        elseif ($derivedMetadataFields -contains $field) {
            $status = "dx9-derived-metadata"
            $reason = "DX9-only draw metadata; coverage is checked through concrete vertex/index counts where available."
        }
        elseif ($field -eq "indices" -and (Get-FieldValue -Row $dx9 -Name "kind") -ne "drawIndexed") {
            $status = "dx9-nonindexed-count-metadata"
            $reason = "DX9 non-indexed draw has no index count; D3D11 count is tracked through topology-specific draw handling."
        }
        elseif (($field -eq "kind") -and (Test-D3D11EmulatedPrimitive -Dx9Row $dx9 -Dx11Row $dx11)) {
            $status = "d3d11-emulated-primitive"
            $reason = "D3D11 logs the expanded backend primitive path used to emulate this DX9 source draw."
        }
        elseif (($field -eq "prim") -and (Get-FieldValue -Row $dx11 -Name "kind") -eq "triangleFan" -and $dx9Value -eq "6") {
            $status = "d3d11-emulated-primitive"
            $reason = "DX9 triangle fans are expanded to D3D11 triangle lists; topology is covered by the emulated draw path."
        }
        elseif (($field -eq "indices") -and (Test-D3D11EmulatedPrimitive -Dx9Row $dx9 -Dx11Row $dx11)) {
            $status = "d3d11-emulated-primitive"
            $reason = "D3D11 logs the expanded index count for this emulated DX9 primitive; source coverage is checked through the mapped draw row."
        }
        elseif (!$dx11.Fields.Contains($d3d11Field)) {
            $status = "no-d3d11-logged-field"
            $reason = "D3D11 has no emitted field for this DX9 byte yet."
        }
        elseif (($null -ne (Get-StageFieldIndex -Field $field)) -and ($null -ne (Get-IntegerFieldValue -Row $dx11 -Name "stages")) -and ((Get-StageFieldIndex -Field $field) -ge (Get-IntegerFieldValue -Row $dx11 -Name "stages"))) {
            $status = "inactive-stage-state"
            $reason = "DX9 retained this texture stage cache byte, but the mapped draw has that stage disabled; the byte is logged and accounted but not consumed by the renderer."
        }
        elseif (($field -eq "fogColor") -and ((Get-FieldValue -Row $dx9 -Name "fog") -eq "0") -and ((Get-FieldValue -Row $dx11 -Name "fog") -eq "0")) {
            $status = "inactive-fog-state"
            $reason = "Fog color differs while fog is disabled for both rows."
        }
        elseif ($valueEquivalent) {
            $status = if ($byteExact) { "byte-exact" } else { "value-exact" }
            $reason = if ($byteExact) { "Raw logged values match exactly." } else { "Values match after numeric/encoding normalization." }
        }
        elseif ($resolved.MappedByFoldedSpecularPass -and ($foldedSpecularStateFields -contains $field)) {
            $status = "folded-specular-pass-state"
            $reason = "DX9 additive specular pass is intentionally folded into the D3D11 one-pass specmap shader; math still needs pixel parity proof."
        }
        elseif (($inactiveBlendFields -contains $field) -and ((Get-FieldValue -Row $dx9 -Name "alphaBlend") -ne "1") -and ((Get-FieldValue -Row $dx11 -Name "alphaBlend") -ne "1")) {
            $status = "inactive-blend-state"
            $reason = "Blend factors differ while blending is disabled for both rows."
        }
        elseif (($inactiveAlphaTestFields -contains $field) -and ((Get-FieldValue -Row $dx9 -Name "alphaTest") -ne "1") -and ((Get-FieldValue -Row $dx11 -Name "alphaTest") -ne "1")) {
            $status = "inactive-alpha-test-state"
            $reason = "Alpha-test state differs while alpha test is disabled for both rows."
        }
        elseif ($resolved.MappedByAlias -and $field -eq "indices") {
            $status = "backend-alias-count-mismatch"
            $reason = "Aliased D3D11 shader changed the emitted index count; inspect geometry generation or replacement asset."
        }
        else {
            $reason = "DX9 and D3D11 logged values differ."
        }

        $actionClass = Get-AccountingActionClass -Status $status

        [pscustomobject]@{
            D3D9Ordinal = $dx9.Ordinal
            D3D9Draw = $dx9.Draw
            Shader = $dx9.Shader
            ShaderOccurrence = $dx9.ShaderOccurrence
            OccurrenceKey = $dx9.OccurrenceKey
            D3D9Field = $field
            D3D9Value = $dx9Value
            NormalizedD3D9Value = $normalizedDx9Value
            D3D11Ordinal = if ($dx11) { $dx11.Ordinal } else { $null }
            D3D11Draw = if ($dx11) { $dx11.Draw } else { $null }
            D3D11Shader = if ($dx11) { $dx11.Shader } else { "" }
            D3D11Field = if ($dx11) { $d3d11Field } else { "" }
            D3D11Value = $d3d11Value
            NormalizedD3D11Value = $normalizedD3D11Value
            ByteExact = $byteExact
            ValueEquivalent = $valueEquivalent
            AccountStatus = $status
            ActionClass = $actionClass
            FixRequired = ($actionClass -eq "fix-required")
            Reason = $reason
            MappedByAlias = $resolved.MappedByAlias
            MappedByFoldedSpecularPass = $resolved.MappedByFoldedSpecularPass
            D3D9RawSha256 = $dx9.RawSha256
            D3D11RawSha256 = if ($dx11) { $dx11.RawSha256 } else { "" }
        }
    }
}
$fieldAccountingPath = Join-Path $outputRoot "dx9-field-accounting.csv"
$fieldAccountingRows | Export-Csv -LiteralPath $fieldAccountingPath -NoTypeInformation

$fieldAccountingStatusSummary = @($fieldAccountingRows | Group-Object AccountStatus | Sort-Object Name | ForEach-Object {
    [pscustomobject]@{
        AccountStatus = $_.Name
        Count = $_.Count
    }
})
$fieldAccountingStatusSummaryPath = Join-Path $outputRoot "dx9-field-accounting-status-summary.csv"
$fieldAccountingStatusSummary | Export-Csv -LiteralPath $fieldAccountingStatusSummaryPath -NoTypeInformation

$fieldAccountingFieldSummary = @($fieldAccountingRows | Group-Object D3D9Field, AccountStatus | Sort-Object Name | ForEach-Object {
    [pscustomobject]@{
        D3D9Field = $_.Group[0].D3D9Field
        AccountStatus = $_.Group[0].AccountStatus
        Count = $_.Count
    }
})
$fieldAccountingFieldSummaryPath = Join-Path $outputRoot "dx9-field-accounting-field-summary.csv"
$fieldAccountingFieldSummary | Export-Csv -LiteralPath $fieldAccountingFieldSummaryPath -NoTypeInformation

$fieldAccountingFixRows = @($fieldAccountingRows | Where-Object {
    $_.AccountStatus -eq "mismatch" -or
    $_.AccountStatus -like "*mismatch" -or
    $_.AccountStatus -eq "no-d3d11-logged-field" -or
    $_.AccountStatus -eq "no-mapped-d3d11-draw"
})
$fieldAccountingFixPath = Join-Path $outputRoot "dx9-unaccounted-byte-list.csv"
$fieldAccountingFixRows | Export-Csv -LiteralPath $fieldAccountingFixPath -NoTypeInformation

$fieldAccountingByteExactRows = @($fieldAccountingRows | Where-Object { $_.ByteExact -eq $true })
$fieldAccountingValueEquivalentRows = @($fieldAccountingRows | Where-Object { $_.ByteExact -ne $true -and $_.ValueEquivalent -eq $true })
$fieldAccountingNormalizedRows = @($fieldAccountingRows | Where-Object { $_.FixRequired -ne $true -and $_.ByteExact -ne $true -and $_.ValueEquivalent -ne $true -and $_.ActionClass -eq "accounted-normalized" })
$fieldAccountingReviewRows = @($fieldAccountingRows | Where-Object { $_.FixRequired -ne $true -and $_.ByteExact -ne $true })

$fieldAccountingByteExactPath = Join-Path $outputRoot "dx9-byte-exact-list.csv"
$fieldAccountingValueEquivalentPath = Join-Path $outputRoot "dx9-value-equivalent-byte-list.csv"
$fieldAccountingNormalizedPath = Join-Path $outputRoot "dx9-accounted-normalized-byte-list.csv"
$fieldAccountingReviewPath = Join-Path $outputRoot "dx9-review-required-byte-list.csv"

$fieldAccountingByteExactRows | Export-Csv -LiteralPath $fieldAccountingByteExactPath -NoTypeInformation
$fieldAccountingValueEquivalentRows | Export-Csv -LiteralPath $fieldAccountingValueEquivalentPath -NoTypeInformation
$fieldAccountingNormalizedRows | Export-Csv -LiteralPath $fieldAccountingNormalizedPath -NoTypeInformation
$fieldAccountingReviewRows | Export-Csv -LiteralPath $fieldAccountingReviewPath -NoTypeInformation

function New-FieldAccountingQueue {
    param([object[]]$Rows)

    return @($Rows | Group-Object Shader, D3D9Draw, D3D11Draw, ActionClass, AccountStatus, Reason | Sort-Object Name | ForEach-Object {
        $groupRows = @($_.Group)
        $first = $groupRows[0]
        $fields = @($groupRows | Select-Object -ExpandProperty D3D9Field -Unique | Sort-Object)
        [pscustomobject]@{
            QueueKey = "{0}|{1}|{2}|{3}" -f $first.ActionClass, $first.Shader, $first.D3D9Draw, $first.AccountStatus
            ActionClass = $first.ActionClass
            AccountStatus = $first.AccountStatus
            Shader = $first.Shader
            D3D9Draw = $first.D3D9Draw
            D3D11Draw = $first.D3D11Draw
            FieldCount = $groupRows.Count
            Fields = ($fields -join ";")
            Reason = $first.Reason
            D3D9FirstOrdinal = ($groupRows | Measure-Object D3D9Ordinal -Minimum).Minimum
            D3D11FirstOrdinal = ($groupRows | Where-Object { $_.D3D11Ordinal } | Measure-Object D3D11Ordinal -Minimum).Minimum
            D3D9RawSha256 = $first.D3D9RawSha256
            D3D11RawSha256 = $first.D3D11RawSha256
        }
    })
}

$fieldAccountingFixQueueRows = New-FieldAccountingQueue -Rows $fieldAccountingFixRows
$fieldAccountingReviewQueueRows = New-FieldAccountingQueue -Rows $fieldAccountingReviewRows
$fieldAccountingFixQueuePath = Join-Path $outputRoot "dx9-byte-fix-queue.csv"
$fieldAccountingReviewQueuePath = Join-Path $outputRoot "dx9-byte-review-queue.csv"
$fieldAccountingFixQueueRows | Export-Csv -LiteralPath $fieldAccountingFixQueuePath -NoTypeInformation
$fieldAccountingReviewQueueRows | Export-Csv -LiteralPath $fieldAccountingReviewQueuePath -NoTypeInformation

$fieldAccountingActionSummary = @($fieldAccountingRows | ForEach-Object {
    [pscustomobject]@{
        D3D9Field = $_.D3D9Field
        AccountStatus = $_.AccountStatus
        ActionClass = $_.ActionClass
    }
} | Group-Object D3D9Field, AccountStatus, ActionClass | Sort-Object Name | ForEach-Object {
    [pscustomobject]@{
        D3D9Field = $_.Group[0].D3D9Field
        AccountStatus = $_.Group[0].AccountStatus
        ActionClass = $_.Group[0].ActionClass
        Count = $_.Count
    }
})
$fieldAccountingActionSummaryPath = Join-Path $outputRoot "dx9-field-accounting-action-summary.csv"
$fieldAccountingActionSummary | Export-Csv -LiteralPath $fieldAccountingActionSummaryPath -NoTypeInformation

function New-StateCoverageManifestRow {
    param(
        [string]$Category,
        [string]$Requirement,
        [string[]]$Dx9Fields,
        [string[]]$D3D11Fields,
        [string]$Dx9Source,
        [string]$D3D11Source,
        [string]$Notes
    )

    $presentDx9Fields = @($Dx9Fields | Where-Object {
        $fieldName = $_
        $fieldName -and @($fieldAccountingRows | Where-Object { $_.D3D9Field -eq $fieldName }).Count -gt 0
    })
    $missingDx9Fields = @($Dx9Fields | Where-Object { $_ -and !($presentDx9Fields -contains $_) })
    $rows = @($fieldAccountingRows | Where-Object { $presentDx9Fields -contains $_.D3D9Field })
    $fixRows = @($rows | Where-Object { $_.ActionClass -eq "fix-required" })
    $reviewRows = @($rows | Where-Object { $_.ActionClass -eq "accounted-normalized" })
    $exactRows = @($rows | Where-Object { $_.ActionClass -eq "exact-or-equivalent" })

    $coverageStatus = "needs-ledger-instrumentation"
    if ($Dx9Fields.Count -gt 0 -and $missingDx9Fields.Count -eq 0 -and $fixRows.Count -eq 0 -and $reviewRows.Count -eq 0) {
        $coverageStatus = "accounted-exact-or-equivalent"
    }
    elseif ($Dx9Fields.Count -gt 0 -and $missingDx9Fields.Count -eq 0 -and $fixRows.Count -eq 0) {
        $coverageStatus = "accounted-with-normalization"
    }
    elseif ($Dx9Fields.Count -gt 0 -and $missingDx9Fields.Count -eq 0) {
        $coverageStatus = "fix-required"
    }
    elseif ($presentDx9Fields.Count -gt 0) {
        $coverageStatus = "partial-ledger-instrumentation"
    }

    [pscustomobject]@{
        Category = $Category
        Requirement = $Requirement
        CoverageStatus = $coverageStatus
        Dx9Fields = ($Dx9Fields -join ";")
        PresentDx9Fields = ($presentDx9Fields -join ";")
        MissingDx9Fields = ($missingDx9Fields -join ";")
        D3D11Fields = ($D3D11Fields -join ";")
        ExactOrEquivalentRows = $exactRows.Count
        AccountedNormalizedRows = $reviewRows.Count
        FixRequiredRows = $fixRows.Count
        Dx9Source = $Dx9Source
        D3D11Source = $D3D11Source
        Notes = $Notes
    }
}

$rendererStateCoverageManifest = @(
    New-StateCoverageManifestRow -Category "draw-call" -Requirement "Primitive type, topology expansion, base/start, and source vertex/index counts are paired per draw." -Dx9Fields @("kind", "prim", "primCount", "indices", "base", "start", "sliceVerts", "sliceIndices") -D3D11Fields @("kind", "topo", "count", "base", "start") -Dx9Source "Direct3d9 draw inventory line" -D3D11Source "Direct3d11 draw inventory line" -Notes "DX9-only metadata is normalized; emulated primitive expansions are explicit review rows."
    New-StateCoverageManifestRow -Category "shader-binding" -Requirement "Static shader and fixed-function/pixel-program identity are paired per draw." -Dx9Fields @("shader", "pprogram") -D3D11Fields @("shader", "vprogram", "pprogram") -Dx9Source "Direct3d9 static shader/pass inventory" -D3D11Source "Direct3d11 active shader inventory" -Notes "D3D11 synthesized fixed-function program names are accounted as normalized rows."
    New-StateCoverageManifestRow -Category "blend-state" -Requirement "Alpha blend enable, source/destination blend, blend op, and color-write mask match when active." -Dx9Fields @("alphaBlend", "src", "dst", "op", "colorWrite") -D3D11Fields @("alphaBlend", "src", "dst", "op", "colorWrite") -Dx9Source "Direct3d9 render state cache" -D3D11Source "Direct3d11 blend-state cache" -Notes "Inactive blend factor differences are normalized only when blending is disabled."
    New-StateCoverageManifestRow -Category "alpha-test" -Requirement "Alpha-test enable, compare function, and reference value match after D3D9/D3D11 compare and ref normalization." -Dx9Fields @("alphaTest", "cmp", "alphaRef") -D3D11Fields @("alphaTest", "cmp", "ref") -Dx9Source "Direct3d9 render state cache" -D3D11Source "Direct3d11 pixel shader/discard constants" -Notes "D3D9 compare enum and alpha ref byte are normalized to D3D11 shader values."
    New-StateCoverageManifestRow -Category "depth-state" -Requirement "Depth test and depth-write state match per draw." -Dx9Fields @("depth", "write") -D3D11Fields @("depth", "write") -Dx9Source "Direct3d9 render state cache" -D3D11Source "Direct3d11 depth-stencil state" -Notes ""
    New-StateCoverageManifestRow -Category "fog-state" -Requirement "Fog enable, pass fog mode, and fog color match when fog participates in the draw." -Dx9Fields @("fog", "passFog", "fogColor") -D3D11Fields @("fog", "passFog", "fogColor", "fogColorPacked") -Dx9Source "Direct3d9 fog render states/pass state" -D3D11Source "Direct3d11 fog constants" -Notes "Disabled fog-color cache differences are normalized as inactive-fog-state."
    New-StateCoverageManifestRow -Category "stage-texture-binding" -Requirement "Active texture-stage native formats and D3D9 TEXCOORDINDEX bytes are paired for stages 0-2." -Dx9Fields @("s0fmt", "s0tci", "s1fmt", "s1tci", "s2fmt", "s2tci") -D3D11Fields @("s0fmt", "s0tci", "s1fmt", "s1tci", "s2fmt", "s2tci") -Dx9Source "Direct3d9 texture cache and texture-stage state cache" -D3D11Source "Direct3d11 stage texture descriptors/constants" -Notes "Disabled stage cache leftovers are normalized as inactive-stage-state."
    New-StateCoverageManifestRow -Category "stage-combiner-state" -Requirement "Active D3D9 texture-stage COLOROP/COLORARG/ALPHAOP/ALPHAARG/RESULTARG bytes are paired for stages 0-2." -Dx9Fields @("s0cop", "s0c0", "s0c1", "s0c2", "s0aop", "s0a0", "s0a1", "s0a2", "s0res", "s1cop", "s1c0", "s1c1", "s1c2", "s1aop", "s1a0", "s1a1", "s1a2", "s1res", "s2cop", "s2c0", "s2c1", "s2c2", "s2aop", "s2a0", "s2a1", "s2a2", "s2res") -D3D11Fields @("s0cop", "s0c0", "s0c1", "s0c2", "s0aop", "s0a0", "s0a1", "s0a2", "s0res", "s1cop", "s1c0", "s1c1", "s1c2", "s1aop", "s1a0", "s1a1", "s1a2", "s1res", "s2cop", "s2c0", "s2c1", "s2c2", "s2aop", "s2a0", "s2a1", "s2a2", "s2res") -Dx9Source "Direct3d9 texture-stage state cache after shader pass apply" -D3D11Source "Direct3d11 stage combiner constants mapped back to D3D9 enum bytes" -Notes "Primary ledger for fixed-function shell and sky shader color-combiner parity."
    New-StateCoverageManifestRow -Category "material-and-factor" -Requirement "Material ambient/diffuse/emissive/specular/specular power and texture factors are accounted per draw." -Dx9Fields @("matA", "matD", "matE", "matS", "specPower", "tf", "tf2") -D3D11Fields @("matA", "matD", "matE", "matS", "specPower", "tf", "tf2") -Dx9Source "Direct3d9 SetMaterial, D3DRS_TEXTUREFACTOR, specular power cache" -D3D11Source "Direct3d11 material constants" -Notes "Logged from the active material/factor state used by the draw."
    New-StateCoverageManifestRow -Category "lighting-state" -Requirement "Ambient/diffuse light colors, enabled light list, directional vectors, and light-scale behavior are accounted per draw." -Dx9Fields @("lightAmb", "lightDiff", "lightDir", "enabledLights") -D3D11Fields @("lightAmb", "lightDiff", "lightDir", "enabledLights") -Dx9Source "Direct3d9_LightManager and render states" -D3D11Source "Direct3d11 light constants" -Notes "Needed for shadows/shading/parity; currently not fully present in the DX9 ledger."
    New-StateCoverageManifestRow -Category "sampler-state" -Requirement "Sampler address/filter/mip/anisotropy state is accounted for every consumed texture stage." -Dx9Fields @("s0addr", "s0filter", "s0mip", "s0aniso", "s1addr", "s1filter", "s1mip", "s1aniso", "s2addr", "s2filter", "s2mip", "s2aniso") -D3D11Fields @("s0addr", "s0filter", "s0mip", "s0aniso", "s1addr", "s1filter", "s1mip", "s1aniso", "s2addr", "s2filter", "s2mip", "s2aniso") -Dx9Source "Direct3d9 sampler state cache" -D3D11Source "Direct3d11 sampler descriptors" -Notes "Missing from the current per-draw byte ledger."
    New-StateCoverageManifestRow -Category "texture-transform" -Requirement "Texture transform matrix, transform flags, projection flag, and scroll values are accounted per active stage." -Dx9Fields @("texTransformFlags", "texTransform", "stageScroll") -D3D11Fields @("texTransformFlags", "texTransform", "stageScroll") -Dx9Source "Direct3d9 texture transform SetTransform/TSS" -D3D11Source "Direct3d11 shader constants" -Notes "Important for sky/cloud/stage coordinate parity; coordgen is logged, but transform matrices are not fully in the ledger."
    New-StateCoverageManifestRow -Category "matrix-state" -Requirement "World, view, projection, and object-to-camera/projection matrices are accounted for comparable draws." -Dx9Fields @("worldMatrix", "viewMatrix", "projectionMatrix") -D3D11Fields @("worldMatrix", "viewMatrix", "projectionMatrix") -Dx9Source "Direct3d9 SetTransform caches" -D3D11Source "Direct3d11 transform constants" -Notes "Matrix values are needed for byte-for-byte camera/sky/lighting diagnosis, but are too large for the current compact draw line."
    New-StateCoverageManifestRow -Category "vertex-input-state" -Requirement "Vertex declaration, stream source, stride, offset, and index format are accounted per draw." -Dx9Fields @("vertexDecl", "stream0", "streamStride", "streamOffset", "indexFmt") -D3D11Fields @("inputLayout", "stream0", "streamStride", "streamOffset", "indexFmt") -Dx9Source "Direct3d9 vertex declaration/stream/index cache" -D3D11Source "Direct3d11 input layout/buffer bindings" -Notes "Needed to prove geometry parity beyond final draw counts."
    New-StateCoverageManifestRow -Category "render-target-and-color-space" -Requirement "Backbuffer format, render-target format, gamma/sRGB/brightness/contrast path, and clear color are accounted per capture." -Dx9Fields @("backbufferFmt", "renderTargetFmt", "gamma", "brightness", "contrast", "clearColor") -D3D11Fields @("backbufferFmt", "renderTargetFmt", "gamma", "brightness", "contrast", "clearColor") -Dx9Source "Direct3d9 device/backbuffer/gamma path" -D3D11Source "Direct3d11 swapchain/render-target/gamma path" -Notes "Needed for washed-out loading screens and global color parity; current draw ledger alone cannot prove this."
)
$rendererStateCoverageManifestPath = Join-Path $outputRoot "dx9-renderer-state-coverage-manifest.csv"
$rendererStateCoverageManifest | Export-Csv -LiteralPath $rendererStateCoverageManifestPath -NoTypeInformation

$rendererStateCoverageSummary = @($rendererStateCoverageManifest | Group-Object CoverageStatus | Sort-Object Name | ForEach-Object {
    [pscustomobject]@{
        CoverageStatus = $_.Name
        Count = $_.Count
    }
})
$rendererStateCoverageSummaryPath = Join-Path $outputRoot "dx9-renderer-state-coverage-summary.csv"
$rendererStateCoverageSummary | Export-Csv -LiteralPath $rendererStateCoverageSummaryPath -NoTypeInformation

$fieldAccountingPartitionRows = @(
    [pscustomobject]@{ Partition = "byte-exact"; Count = $fieldAccountingByteExactRows.Count; File = $fieldAccountingByteExactPath }
    [pscustomobject]@{ Partition = "value-equivalent"; Count = $fieldAccountingValueEquivalentRows.Count; File = $fieldAccountingValueEquivalentPath }
    [pscustomobject]@{ Partition = "accounted-normalized"; Count = $fieldAccountingNormalizedRows.Count; File = $fieldAccountingNormalizedPath }
    [pscustomobject]@{ Partition = "fix-required"; Count = $fieldAccountingFixRows.Count; File = $fieldAccountingFixPath }
)
$fieldAccountingPartitionPath = Join-Path $outputRoot "dx9-byte-accounting-partitions.csv"
$fieldAccountingPartitionRows | Export-Csv -LiteralPath $fieldAccountingPartitionPath -NoTypeInformation

$d3d11OnlyRows = foreach ($dx11 in $d3d11Rows) {
    if (!$d3d9ByKey.ContainsKey($dx11.OccurrenceKey) -and !$matchedD3d11Keys.ContainsKey($dx11.OccurrenceKey)) {
        [pscustomobject]@{
            D3D11Ordinal = $dx11.Ordinal
            D3D11Draw = $dx11.Draw
            Shader = $dx11.Shader
            ShaderOccurrence = $dx11.ShaderOccurrence
            OccurrenceKey = $dx11.OccurrenceKey
            Status = if (!$d3d9Counts.ContainsKey($dx11.Shader)) { "d3d11-only-shader" } else { "extra-d3d11-occurrence" }
            D3D11RawSha256 = $dx11.RawSha256
            D3D11RawLine = $dx11.RawLine
        }
    }
}
$d3d11OnlyRows | Export-Csv -LiteralPath (Join-Path $outputRoot "d3d11-only-map.csv") -NoTypeInformation

$mappedRows = @($mappingRows | Where-Object { $_.Status -eq "mapped" })
$aliasMappedRows = @($mappingRows | Where-Object { $_.Status -match "^mapped-via-alias" })
$foldedSpecularMappedRows = @($mappingRows | Where-Object { $_.Status -match "^mapped-via-folded-specular-pass" })
$mismatchedRows = @($mappingRows | Where-Object { $_.Status -eq "mapped-field-mismatch" })
$aliasMismatchedRows = @($mappingRows | Where-Object { $_.Status -eq "mapped-via-alias-field-mismatch" })
$foldedSpecularMismatchedRows = @($mappingRows | Where-Object { $_.Status -eq "mapped-via-folded-specular-pass-field-mismatch" })
$missingRows = @($mappingRows | Where-Object { $_.Status -match "^missing" })
$d3d9Truncated = ($d3d9Rows.Count -ge $InventoryLimit)
$d3d11Truncated = ($d3d11Rows.Count -ge $InventoryLimit)
$fieldMismatchRows = @($fieldAccountingRows | Where-Object { $_.AccountStatus -eq "mismatch" -or $_.AccountStatus -like "*mismatch" })
$fieldNoD3D11LoggedRows = @($fieldAccountingRows | Where-Object { $_.AccountStatus -eq "no-d3d11-logged-field" })

$summary = [pscustomobject]@{
    GeneratedAt = (Get-Date).ToString("o")
    CompareRoot = $compareRootPath
    OutputRoot = $outputRoot
    InventoryLimit = $InventoryLimit
    MinimumFrameRows = $MinimumFrameRows
    FrameMode = $FrameMode
    FrameSelectionApplied = $frameSelectionApplied
    SelectedFrame = $selectedFrame
    SelectedD3D9Frame = $selectedD3D9Frame
    SelectedD3D11Frame = $selectedD3D11Frame
    FrameOverlapScore = $frameOverlapScore
    D3D9WorldFrameGroups = $d3d9WorldFrameGroupCount
    D3D11WorldFrameGroups = $d3d11WorldFrameGroupCount
    D3D9InGameWorldFrameGroups = $d3d9InGameWorldFrameGroupCount
    D3D11InGameWorldFrameGroups = $d3d11InGameWorldFrameGroupCount
    FrameSelectionRequiredWorldRows = $frameSelectionRequiredWorldRows
    FrameSelectionWorldRowsSatisfied = $frameSelectionWorldRowsSatisfied
    FrameSelectionRequiredInGameWorldRows = $frameSelectionRequiredInGameWorldRows
    FrameSelectionInGameWorldRowsSatisfied = $frameSelectionInGameWorldRowsSatisfied
    SelectedD3D9WorldRows = $selectedD3D9WorldRows
    SelectedD3D11WorldRows = $selectedD3D11WorldRows
    SelectedD3D9InGameWorldRows = $selectedD3D9InGameWorldRows
    SelectedD3D11InGameWorldRows = $selectedD3D11InGameWorldRows
    SceneStateComparable = (!$frameSelectionRequiredWorldRows -or $frameSelectionWorldRowsSatisfied)
    InGameSceneStateComparable = (!$frameSelectionRequiredInGameWorldRows -or $frameSelectionInGameWorldRowsSatisfied)
    D3D9Dbwin = $d3d9Dbwin[0].FullName
    D3D11Dbwin = $d3d11Dbwin[0].FullName
    D3D9AllDrawRows = $allD3d9Rows.Count
    D3D11AllDrawRows = $allD3d11Rows.Count
    D3D9DrawRows = $d3d9Rows.Count
    D3D11DrawRows = $d3d11Rows.Count
    D3D9UniqueShaders = $d3d9Counts.Count
    D3D11UniqueShaders = $d3d11Counts.Count
    D3D9ProbablyTruncated = $d3d9Truncated
    D3D11ProbablyTruncated = $d3d11Truncated
    Dx9RowsMappedExactlyOnTrackedFields = $mappedRows.Count
    Dx9RowsMappedViaBackendAlias = $aliasMappedRows.Count
    Dx9RowsMappedViaFoldedSpecularPass = $foldedSpecularMappedRows.Count
    Dx9RowsMappedWithTrackedFieldMismatch = $mismatchedRows.Count
    Dx9RowsMappedViaBackendAliasWithTrackedFieldMismatch = $aliasMismatchedRows.Count
    Dx9RowsMappedViaFoldedSpecularPassWithTrackedFieldMismatch = $foldedSpecularMismatchedRows.Count
    Dx9RowsMissingFromD3D11 = $missingRows.Count
    D3D11RowsNotMappedFromD3D9 = @($d3d11OnlyRows).Count
    Dx9FieldAccountingRows = @($fieldAccountingRows).Count
    Dx9FieldAccountingByteExactRows = @($fieldAccountingRows | Where-Object { $_.AccountStatus -eq "byte-exact" }).Count
    Dx9FieldAccountingValueExactRows = @($fieldAccountingRows | Where-Object { $_.AccountStatus -eq "value-exact" }).Count
    Dx9FieldAccountingMismatchedRows = $fieldMismatchRows.Count
    Dx9FieldAccountingNoD3D11LoggedRows = $fieldNoD3D11LoggedRows.Count
    Dx9FieldAccountingFixRequiredRows = @($fieldAccountingFixRows).Count
    Dx9FieldAccountingReviewRequiredRows = @($fieldAccountingReviewRows).Count
    Dx9ByteAccountingFixQueueRows = @($fieldAccountingFixQueueRows).Count
    Dx9ByteAccountingReviewQueueRows = @($fieldAccountingReviewQueueRows).Count
    Dx9ByteAccountingAllRowsCategorized = (@($fieldAccountingRows).Count -eq (@($fieldAccountingByteExactRows).Count + @($fieldAccountingValueEquivalentRows).Count + @($fieldAccountingNormalizedRows).Count + @($fieldAccountingFixRows).Count))
    Dx9ByteAccountingAllRowsByteExact = (@($fieldAccountingRows).Count -eq @($fieldAccountingByteExactRows).Count)
    Dx9RendererStateCoverageRows = @($rendererStateCoverageManifest).Count
    Dx9RendererStateCoverageNeedsInstrumentationRows = @($rendererStateCoverageManifest | Where-Object { $_.CoverageStatus -eq "needs-ledger-instrumentation" -or $_.CoverageStatus -eq "partial-ledger-instrumentation" }).Count
    Dx9RendererStateCoverageFixRequiredRows = @($rendererStateCoverageManifest | Where-Object { $_.CoverageStatus -eq "fix-required" }).Count
    Dx9RendererStateCoverageSummary = @($rendererStateCoverageSummary)
    MissingShadersFromD3D11 = @($shaderSummary | Where-Object { $_.Status -eq "missing-from-d3d11" } | Select-Object -ExpandProperty Shader)
    UnaccountedMissingShadersFromD3D11 = @($missingRows | Select-Object -ExpandProperty Shader -Unique)
    D3D11OnlyShaders = @($shaderSummary | Where-Object { $_.Status -eq "d3d11-only" } | Select-Object -ExpandProperty Shader)
    CountMismatchShaders = @($shaderSummary | Where-Object { $_.Status -eq "count-mismatch" } | Select-Object -ExpandProperty Shader)
    Dx9ByteAccountingPartitions = @($fieldAccountingPartitionRows)
    Dx9ByteFixQueue = @($fieldAccountingFixQueueRows)
    Files = [pscustomobject]@{
        D3D9FullLedger = Join-Path $outputRoot "d3d9-ledger-all.csv"
        D3D11FullLedger = Join-Path $outputRoot "d3d11-ledger-all.csv"
        D3D9Ledger = Join-Path $outputRoot "d3d9-ledger.csv"
        D3D11Ledger = Join-Path $outputRoot "d3d11-ledger.csv"
        ShaderSummary = Join-Path $outputRoot "shader-summary.csv"
        Dx9ToDx11Map = Join-Path $outputRoot "dx9-to-dx11-map.csv"
        Dx9FieldAccounting = $fieldAccountingPath
        Dx9FieldAccountingStatusSummary = $fieldAccountingStatusSummaryPath
        Dx9FieldAccountingFieldSummary = $fieldAccountingFieldSummaryPath
        Dx9FieldAccountingActionSummary = $fieldAccountingActionSummaryPath
        Dx9RendererStateCoverageManifest = $rendererStateCoverageManifestPath
        Dx9RendererStateCoverageSummary = $rendererStateCoverageSummaryPath
        Dx9ByteAccountingPartitions = $fieldAccountingPartitionPath
        Dx9ByteExactList = $fieldAccountingByteExactPath
        Dx9ValueEquivalentByteList = $fieldAccountingValueEquivalentPath
        Dx9AccountedNormalizedByteList = $fieldAccountingNormalizedPath
        Dx9ReviewRequiredByteList = $fieldAccountingReviewPath
        Dx9UnaccountedByteList = $fieldAccountingFixPath
        Dx9ByteFixQueue = $fieldAccountingFixQueuePath
        Dx9ByteReviewQueue = $fieldAccountingReviewQueuePath
        D3D11OnlyMap = Join-Path $outputRoot "d3d11-only-map.csv"
    }
}

$summaryPath = Join-Path $outputRoot "renderer-inventory-summary.json"
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath
$summary | Format-List
