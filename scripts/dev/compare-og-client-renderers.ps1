param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [string]$ProofRoot = "",
    [string]$SceneName = "loading",
    [ValidateSet("None", "LoginEnterWorld", "ConfigAutoConnect")]
    [string]$SceneDriver = "None",
    [string]$AutoConnectUser = "test",
    [string]$AutoConnectPassword = "test",
    [string]$AutoConnectCluster = "swg",
    [string]$AutoConnectAvatar = "Cyruss",
    [int]$LoginDelaySeconds = 8,
    [int]$CharacterDelaySeconds = 8,
    [int]$WorldDelaySeconds = 15,
    [int]$TimeoutSeconds = 30,
    [int]$InterRunDelaySeconds = 8,
    [int]$ScreenWidth = 1280,
    [int]$ScreenHeight = 960,
    [int]$D3D11AutocapturePresent = 30,
    [int]$RendererAutocaptureMax = 12,
    [int]$RendererAutocaptureInterval = 4,
    [int]$RendererAutocaptureWorldRows = 1600,
    [int]$RendererInventoryLimit = 50000,
    [int]$RendererInventoryStartFrame = 0,
    [int]$WorldCaptureCount = 1,
    [int]$WorldCaptureIntervalSeconds = 10,
    [ValidateSet("BestShaderOverlap", "LatestInGameWorld", "LastCommon", "All")]
    [string]$RendererInventoryFrameMode = "BestShaderOverlap",
    [bool]$PinEnvironmentTime = $true,
    [double]$EnvironmentNormalizedTime = 0.525,
    [switch]$QuickSnap,
    [switch]$BuildFirst,
    [switch]$CleanRuntime,
    [switch]$RendererInventory
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

if ($QuickSnap) {
    $LoginDelaySeconds = [Math]::Min($LoginDelaySeconds, 3)
    $CharacterDelaySeconds = [Math]::Min($CharacterDelaySeconds, 4)
    $WorldDelaySeconds = [Math]::Min($WorldDelaySeconds, 18)
    $WorldCaptureCount = [Math]::Max($WorldCaptureCount, 4)
    $WorldCaptureIntervalSeconds = [Math]::Min([Math]::Max($WorldCaptureIntervalSeconds, 8), 8)
    $RendererAutocaptureMax = [Math]::Min($RendererAutocaptureMax, 2)
    if ($TimeoutSeconds -gt 45) {
        $TimeoutSeconds = 45
    }
}

function Write-Step {
    param([string]$Message)
    Write-Output ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $Message)
}

function Copy-IfPresent {
    param(
        [string]$Source,
        [string]$Destination
    )

    if ($Source -and (Test-Path -LiteralPath $Source)) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
        return $Destination
    }

    return $null
}

function Get-CapturePresentNumber {
    param([string]$Path)

    if ($Path -and ([System.IO.Path]::GetFileName($Path) -match "present([0-9]+)\.bmp$")) {
        return [int]$matches[1]
    }

    return $null
}

function Get-ClosestBackbufferCapture {
    param(
        [object]$Summary,
        [Nullable[int]]$SelectedFrame,
        [string]$FallbackImage
    )

    $series = @()
    if ($Summary -and ($Summary.PSObject.Properties.Name -contains "BackbufferCaptureSeries")) {
        $series = @($Summary.BackbufferCaptureSeries | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
    }

    if ($SelectedFrame -ne $null -and $series.Count -gt 0) {
        $best = $null
        foreach ($path in $series) {
            $present = Get-CapturePresentNumber -Path $path
            if ($present -eq $null) {
                continue
            }

            $delta = [Math]::Abs($present - [int]$SelectedFrame)
            if (!$best -or $delta -lt $best.Delta -or ($delta -eq $best.Delta -and $present -gt $best.Present)) {
                $best = [pscustomobject]@{
                    Path = $path
                    Present = $present
                    Delta = $delta
                }
            }
        }

        if ($best) {
            return $best.Path
        }
    }

    return $FallbackImage
}

function Test-BackbufferCaptureSeries {
    param([object]$Summary)

    if (!$Summary -or !($Summary.PSObject.Properties.Name -contains "BackbufferCaptureSeries")) {
        return $false
    }

    return @($Summary.BackbufferCaptureSeries | Where-Object { $_ -and (Test-Path -LiteralPath $_) }).Count -gt 0
}

function Get-LatestBackbufferCapture {
    param(
        [object]$Summary,
        [string]$FallbackImage
    )

    $series = @()
    if ($Summary -and ($Summary.PSObject.Properties.Name -contains "BackbufferCaptureSeries")) {
        $series = @($Summary.BackbufferCaptureSeries | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
    }

    $best = $null
    foreach ($path in $series) {
        $present = Get-CapturePresentNumber -Path $path
        if ($present -eq $null) {
            continue
        }

        if (!$best -or $present -gt $best.Present) {
            $best = [pscustomobject]@{
                Path = $path
                Present = $present
            }
        }
    }

    if ($best) {
        return $best.Path
    }

    if ($Summary -and ($Summary.PSObject.Properties.Name -contains "BackbufferCapturePath") -and $Summary.BackbufferCapturePath -and (Test-Path -LiteralPath $Summary.BackbufferCapturePath)) {
        return $Summary.BackbufferCapturePath
    }

    return $FallbackImage
}

$safeSceneName = $SceneName -replace "[^A-Za-z0-9._-]", "_"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$compareRoot = Join-Path $ProofRoot "og-render-compare-$safeSceneName-$stamp"
New-Item -ItemType Directory -Force -Path $compareRoot | Out-Null

$diagnoseScript = Join-Path $PSScriptRoot "diagnose-og-client-d3d11.ps1"
if (!(Test-Path -LiteralPath $diagnoseScript)) {
    throw "Missing diagnosis script: $diagnoseScript"
}

Write-Step "Compare proof bundle: $compareRoot"

$summaries = @()
foreach ($backend in @("D3D9", "D3D11")) {
    $backendLower = $backend.ToLowerInvariant()
    $runName = "$safeSceneName-$backendLower"
    Write-Step "Capturing $backend scene: $SceneName"

    $runParams = @{
        ClientToolsRoot = $ClientToolsRoot
        ClientDataRoot = $ClientDataRoot
        ProofRoot = $compareRoot
        Backend = $backend
        RunName = $runName
        SceneDriver = $SceneDriver
        AutoConnectUser = $AutoConnectUser
        AutoConnectPassword = $AutoConnectPassword
        AutoConnectCluster = $AutoConnectCluster
        AutoConnectAvatar = $AutoConnectAvatar
        LoginDelaySeconds = $LoginDelaySeconds
        CharacterDelaySeconds = $CharacterDelaySeconds
        WorldDelaySeconds = $WorldDelaySeconds
        TimeoutSeconds = $TimeoutSeconds
        ScreenWidth = $ScreenWidth
        ScreenHeight = $ScreenHeight
        D3D11AutocapturePresent = $D3D11AutocapturePresent
        RendererAutocaptureMax = $RendererAutocaptureMax
        RendererAutocaptureInterval = $RendererAutocaptureInterval
        RendererAutocaptureWorldRows = $RendererAutocaptureWorldRows
        RendererInventoryLimit = $RendererInventoryLimit
        RendererInventoryStartFrame = $RendererInventoryStartFrame
        WorldCaptureCount = $WorldCaptureCount
        WorldCaptureIntervalSeconds = $WorldCaptureIntervalSeconds
        PinEnvironmentTime = $PinEnvironmentTime
        EnvironmentNormalizedTime = $EnvironmentNormalizedTime
    }

    if ($RendererInventory) {
        $runParams.RendererInventory = $true
    }

    if ($QuickSnap) {
        $runParams.QuickSnap = $true
    }

    if ($CleanRuntime) {
        $runParams.CleanRuntime = $true
    }

    if ($BuildFirst -and $backend -eq "D3D11") {
        $runParams.BuildFirst = $true
    }

    & $diagnoseScript @runParams
    if ($LASTEXITCODE -ne 0) {
        $summaryPath = Join-Path (Join-Path $compareRoot $runName) "summary.json"
        $summary = $null
        if (Test-Path -LiteralPath $summaryPath) {
            $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
        }

        $hasUsableCapture = $summary -and (
            ($summary.ScreenshotPath -and (Test-Path -LiteralPath $summary.ScreenshotPath)) -or
            ($summary.ClientScreenshotPath -and (Test-Path -LiteralPath $summary.ClientScreenshotPath)) -or
            ($summary.BackbufferCapturePath -and (Test-Path -LiteralPath $summary.BackbufferCapturePath))
        )

        if (!$hasUsableCapture) {
            throw "$backend diagnosis failed with exit code $LASTEXITCODE"
        }

        Write-Step "$backend diagnosis returned exit code $LASTEXITCODE but produced a usable capture; continuing"
    }

    $summaryPath = Join-Path (Join-Path $compareRoot $runName) "summary.json"
    if (!(Test-Path -LiteralPath $summaryPath)) {
        throw "$backend diagnosis did not write summary: $summaryPath"
    }

    $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
    $summary | Add-Member -NotePropertyName SummaryPath -NotePropertyValue $summaryPath
    $summary | Add-Member -NotePropertyName DbwinPath -NotePropertyValue (Join-Path (Join-Path $compareRoot $runName) "dbwin.txt")
    $summary | Add-Member -NotePropertyName Modules32Path -NotePropertyValue (Join-Path (Join-Path $compareRoot $runName) "modules-32bit.txt")
    $summary | Add-Member -NotePropertyName WindowsPath -NotePropertyValue (Join-Path (Join-Path $compareRoot $runName) "windows.json")
    $summaries += $summary

    [void](Copy-IfPresent -Source $summary.ScreenshotPath -Destination (Join-Path $compareRoot "$backendLower-window.png"))
    [void](Copy-IfPresent -Source $summary.ClientScreenshotPath -Destination (Join-Path $compareRoot "$backendLower-client.png"))
    if ($summary.GameScreenshotPath) {
        $gameScreenshotExtension = [System.IO.Path]::GetExtension($summary.GameScreenshotPath)
        [void](Copy-IfPresent -Source $summary.GameScreenshotPath -Destination (Join-Path $compareRoot "$backendLower-renderer-screenshot$gameScreenshotExtension"))
    }
    [void](Copy-IfPresent -Source $summary.BackbufferCapturePath -Destination (Join-Path $compareRoot "$backendLower-backbuffer.bmp"))

    if ($backend -eq "D3D9" -and $InterRunDelaySeconds -gt 0) {
        Write-Step "Pausing $InterRunDelaySeconds seconds before D3D11 capture"
        Start-Sleep -Seconds $InterRunDelaySeconds
    }
}

$d3d9ClientImage = Join-Path $compareRoot "d3d9-client.png"
$d3d11ClientImage = Join-Path $compareRoot "d3d11-client.png"
$d3d9WindowImage = Join-Path $compareRoot "d3d9-window.png"
$d3d11WindowImage = Join-Path $compareRoot "d3d11-window.png"
$d3d9BackbufferImage = Join-Path $compareRoot "d3d9-backbuffer.bmp"
$d3d11BackbufferImage = Join-Path $compareRoot "d3d11-backbuffer.bmp"
$d3d9RendererImage = Get-ChildItem -LiteralPath $compareRoot -Filter "d3d9-renderer-screenshot.*" -File -ErrorAction SilentlyContinue | Select-Object -First 1
$d3d11RendererImage = Get-ChildItem -LiteralPath $compareRoot -Filter "d3d11-renderer-screenshot.*" -File -ErrorAction SilentlyContinue | Select-Object -First 1
if ($QuickSnap -and (Test-Path -LiteralPath $d3d9WindowImage) -and (Test-Path -LiteralPath $d3d11WindowImage)) {
    $referenceCompareImage = $d3d9WindowImage
    $candidateCompareImage = $d3d11WindowImage
}
elseif ((Test-Path -LiteralPath $d3d9BackbufferImage) -and (Test-Path -LiteralPath $d3d11BackbufferImage)) {
    $referenceCompareImage = $d3d9BackbufferImage
    $candidateCompareImage = $d3d11BackbufferImage
}
elseif ($d3d9RendererImage -and $d3d11RendererImage) {
    $referenceCompareImage = $d3d9RendererImage.FullName
    $candidateCompareImage = $d3d11RendererImage.FullName
}
elseif ((Test-Path -LiteralPath $d3d9ClientImage) -and (Test-Path -LiteralPath $d3d11ClientImage)) {
    $referenceCompareImage = $d3d9ClientImage
    $candidateCompareImage = $d3d11ClientImage
}
else {
    $referenceCompareImage = $d3d9WindowImage
    $candidateCompareImage = $d3d11WindowImage
}
$pixelDiffSummary = $null
$pixelDiffSummaryPath = $null
$windowPixelDiffSummary = $null
$windowPixelDiffSummaryPath = $null
$rendererInventorySummary = $null
$pixelDiffDirectory = Join-Path $compareRoot "pixel-diff"
$windowPixelDiffDirectory = Join-Path $compareRoot "window-diff"
$compareImagesScript = Join-Path $PSScriptRoot "Compare-RenderImages.ps1"

$compareSummary = [pscustomobject]@{
    StartedAt = (Get-Date).ToString("o")
    SceneName = $SceneName
    SceneDriver = $SceneDriver
    TimeoutSeconds = $TimeoutSeconds
    InterRunDelaySeconds = $InterRunDelaySeconds
    ScreenWidth = $ScreenWidth
    ScreenHeight = $ScreenHeight
    D3D11AutocapturePresent = $D3D11AutocapturePresent
    RendererAutocaptureMax = $RendererAutocaptureMax
    RendererAutocaptureInterval = $RendererAutocaptureInterval
    RendererAutocaptureWorldRows = $RendererAutocaptureWorldRows
    WorldCaptureCount = $WorldCaptureCount
    WorldCaptureIntervalSeconds = $WorldCaptureIntervalSeconds
    QuickSnap = [bool]$QuickSnap
    RendererInventory = [bool]$RendererInventory
    RendererInventoryLimit = $RendererInventoryLimit
    RendererInventoryStartFrame = $RendererInventoryStartFrame
    RendererInventoryFrameMode = $RendererInventoryFrameMode
    PinEnvironmentTime = $PinEnvironmentTime
    EnvironmentNormalizedTime = $EnvironmentNormalizedTime
    CompareRoot = $compareRoot
    Runs = $summaries
    D3D9WindowImage = $d3d9WindowImage
    D3D11WindowImage = $d3d11WindowImage
    D3D9ClientImage = $d3d9ClientImage
    D3D11ClientImage = $d3d11ClientImage
    D3D9RendererImage = (@($summaries | Where-Object { $_.Backend -eq "D3D9" } | Select-Object -First 1).GameScreenshotPath)
    D3D11RendererImage = (@($summaries | Where-Object { $_.Backend -eq "D3D11" } | Select-Object -First 1).GameScreenshotPath)
    D3D9BackbufferImage = $d3d9BackbufferImage
    D3D11BackbufferImage = $d3d11BackbufferImage
    PixelReferenceImage = $referenceCompareImage
    PixelCandidateImage = $candidateCompareImage
    PixelDiffSummaryPath = $pixelDiffSummaryPath
    PixelDiffSummary = $pixelDiffSummary
    PixelComparisonValidity = if ($pixelDiffSummary) { $pixelDiffSummary.comparisonValidity } else { $null }
    PixelStateComparison = if ($pixelDiffSummary) { $pixelDiffSummary.stateComparison } else { $null }
    WindowPixelDiffSummaryPath = $windowPixelDiffSummaryPath
    WindowPixelDiffSummary = $windowPixelDiffSummary
    WindowPixelComparisonValidity = if ($windowPixelDiffSummary) { $windowPixelDiffSummary.comparisonValidity } else { $null }
    WindowPixelStateComparison = if ($windowPixelDiffSummary) { $windowPixelDiffSummary.stateComparison } else { $null }
    RendererInventorySummary = $rendererInventorySummary
    SceneStateComparable = $null
}

$compareSummaryPath = Join-Path $compareRoot "compare-summary.json"
$compareSummary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $compareSummaryPath

if ($RendererInventory) {
    $inventoryScript = Join-Path $PSScriptRoot "analyze-renderer-inventory.ps1"
    if (Test-Path -LiteralPath $inventoryScript) {
        Write-Step "Generating renderer inventory ledger"
        & $inventoryScript -CompareRoot $compareRoot -InventoryLimit $RendererInventoryLimit -FrameMode $RendererInventoryFrameMode
        if ($LASTEXITCODE -ne 0) {
            throw "Renderer inventory analysis failed with exit code $LASTEXITCODE"
        }

        $rendererInventorySummaryPath = Join-Path (Join-Path $compareRoot "renderer-inventory") "renderer-inventory-summary.json"
        if (Test-Path -LiteralPath $rendererInventorySummaryPath) {
            $rendererInventorySummary = Get-Content -LiteralPath $rendererInventorySummaryPath -Raw | ConvertFrom-Json
            $compareSummary.RendererInventorySummary = $rendererInventorySummary
            $compareSummary.SceneStateComparable = $rendererInventorySummary.SceneStateComparable
            $compareSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $compareSummaryPath
        }
    }
}

$d3d9Summary = $summaries | Where-Object { $_.Backend -eq "D3D9" } | Select-Object -First 1
$d3d11Summary = $summaries | Where-Object { $_.Backend -eq "D3D11" } | Select-Object -First 1
if ($rendererInventorySummary) {
    $inGameComparable = $true
    if ($rendererInventorySummary.PSObject.Properties.Name -contains "InGameSceneStateComparable") {
        $inGameComparable = [bool]$rendererInventorySummary.InGameSceneStateComparable
    }

    if (!$rendererInventorySummary.SceneStateComparable) {
        $compareSummary.RendererInventorySummary = $rendererInventorySummary
        $compareSummary.SceneStateComparable = $rendererInventorySummary.SceneStateComparable
        $compareSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $compareSummaryPath
        throw "Renderer inventory selected non-comparable frames: D3D9 world rows=$($rendererInventorySummary.SelectedD3D9WorldRows), D3D11 world rows=$($rendererInventorySummary.SelectedD3D11WorldRows). Refusing pixel diff until both backends are in-world."
    }
    if (!$inGameComparable) {
        $compareSummary.RendererInventorySummary = $rendererInventorySummary
        $compareSummary.SceneStateComparable = $rendererInventorySummary.SceneStateComparable
        $compareSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $compareSummaryPath
        throw "Renderer inventory selected non-comparable in-game frames: D3D9 in-game rows=$($rendererInventorySummary.SelectedD3D9InGameWorldRows), D3D11 in-game rows=$($rendererInventorySummary.SelectedD3D11InGameWorldRows). Refusing pixel diff until both backends have terrain/sky/city rows."
    }

    if (!(Test-BackbufferCaptureSeries -Summary $d3d9Summary) -or !(Test-BackbufferCaptureSeries -Summary $d3d11Summary)) {
        $compareSummary.RendererInventorySummary = $rendererInventorySummary
        $compareSummary.SceneStateComparable = $rendererInventorySummary.SceneStateComparable
        $compareSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $compareSummaryPath
        throw "Renderer inventory requires pre-present backbuffer capture series for selected-frame pixel proof. Refusing to fall back to window/client screenshots."
    }

    if ($RendererInventoryFrameMode -eq "LatestInGameWorld") {
        $referenceCompareImage = Get-LatestBackbufferCapture -Summary $d3d9Summary -FallbackImage $referenceCompareImage
        $candidateCompareImage = Get-LatestBackbufferCapture -Summary $d3d11Summary -FallbackImage $candidateCompareImage
    }
    else {
        $referenceCompareImage = Get-ClosestBackbufferCapture -Summary $d3d9Summary -SelectedFrame $rendererInventorySummary.SelectedD3D9Frame -FallbackImage $referenceCompareImage
        $candidateCompareImage = Get-ClosestBackbufferCapture -Summary $d3d11Summary -SelectedFrame $rendererInventorySummary.SelectedD3D11Frame -FallbackImage $candidateCompareImage
    }
}

if ((Test-Path -LiteralPath $compareImagesScript) -and (Test-Path -LiteralPath $referenceCompareImage) -and (Test-Path -LiteralPath $candidateCompareImage)) {
    Write-Step "Generating pixel diff from $([System.IO.Path]::GetFileName($referenceCompareImage)) vs $([System.IO.Path]::GetFileName($candidateCompareImage))"
    & $compareImagesScript -ReferenceImage $referenceCompareImage -CandidateImage $candidateCompareImage -OutputDirectory $pixelDiffDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "Pixel diff failed with exit code $LASTEXITCODE"
    }

    $pixelDiffSummaryPath = Join-Path $pixelDiffDirectory "summary.json"
    if (Test-Path -LiteralPath $pixelDiffSummaryPath) {
        $pixelDiffSummary = Get-Content -LiteralPath $pixelDiffSummaryPath -Raw | ConvertFrom-Json
    }
}

if ((Test-Path -LiteralPath $compareImagesScript) -and (Test-Path -LiteralPath $d3d9WindowImage) -and (Test-Path -LiteralPath $d3d11WindowImage)) {
    $isWindowReferenceAlready = [string]::Equals($referenceCompareImage, $d3d9WindowImage, [System.StringComparison]::OrdinalIgnoreCase)
    $isWindowCandidateAlready = [string]::Equals($candidateCompareImage, $d3d11WindowImage, [System.StringComparison]::OrdinalIgnoreCase)
    if (!$isWindowReferenceAlready -or !$isWindowCandidateAlready) {
        Write-Step "Generating window fallback pixel diff from d3d9-window.png vs d3d11-window.png"
        & $compareImagesScript -ReferenceImage $d3d9WindowImage -CandidateImage $d3d11WindowImage -OutputDirectory $windowPixelDiffDirectory
        if ($LASTEXITCODE -ne 0) {
            throw "Window pixel diff failed with exit code $LASTEXITCODE"
        }

        $windowPixelDiffSummaryPath = Join-Path $windowPixelDiffDirectory "summary.json"
        if (Test-Path -LiteralPath $windowPixelDiffSummaryPath) {
            $windowPixelDiffSummary = Get-Content -LiteralPath $windowPixelDiffSummaryPath -Raw | ConvertFrom-Json
        }
    }
}

$compareSummary.PixelReferenceImage = $referenceCompareImage
$compareSummary.PixelCandidateImage = $candidateCompareImage
$compareSummary.PixelDiffSummaryPath = $pixelDiffSummaryPath
$compareSummary.PixelDiffSummary = $pixelDiffSummary
$compareSummary.PixelComparisonValidity = if ($pixelDiffSummary) { $pixelDiffSummary.comparisonValidity } else { $null }
$compareSummary.PixelStateComparison = if ($pixelDiffSummary) { $pixelDiffSummary.stateComparison } else { $null }
$compareSummary.WindowPixelDiffSummaryPath = $windowPixelDiffSummaryPath
$compareSummary.WindowPixelDiffSummary = $windowPixelDiffSummary
$compareSummary.WindowPixelComparisonValidity = if ($windowPixelDiffSummary) { $windowPixelDiffSummary.comparisonValidity } else { $null }
$compareSummary.WindowPixelStateComparison = if ($windowPixelDiffSummary) { $windowPixelDiffSummary.stateComparison } else { $null }
$compareSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $compareSummaryPath

$compareSummary | Format-List
Write-Step "Renderer comparison complete"
