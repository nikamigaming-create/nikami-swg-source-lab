param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [string]$ProofRoot = "",
    [string]$ProofPath = "",
    [string]$HandTracePath = "",
    [string]$ClientLogPath = "",
    [int]$DurationSeconds = 30,
    [switch]$Launch,
    [switch]$NoDeploy,
    [switch]$StopAfter,
    [switch]$AnalyzeOnly
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

$checks = New-Object System.Collections.Generic.List[object]

function Add-Check([string]$Name, [bool]$Pass, [string]$Detail) {
    $checks.Add([PSCustomObject]@{
        Name = $Name
        Pass = $Pass
        Detail = $Detail
    }) | Out-Null
}

function Get-PeMachine([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        return "<missing>"
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40) {
        return "<invalid>"
    }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or ($peOffset + 6) -ge $bytes.Length) {
        return "<invalid>"
    }

    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    switch ($machine) {
        0x8664 { return "AMD64" }
        0x14c { return "I386" }
        default { return ("0x{0:X4}" -f $machine) }
    }
}

function Read-TextOrEmpty([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        return ""
    }

    return [System.IO.File]::ReadAllText($Path)
}

function Test-Regex([string]$Text, [string]$Pattern) {
    return [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
}

function Test-BinaryContainsMarker([string]$Path, [string]$Marker) {
    if (!(Test-Path -LiteralPath $Path)) {
        return $false
    }

    return (Select-String -Path $Path -Pattern $Marker -SimpleMatch -Quiet)
}

function Get-Sha256OrEmpty([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        return ""
    }

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-VectorFromLine([string]$Line, [string]$Name) {
    $pattern = [regex]::Escape($Name) + '=\((-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)\)'
    $match = [regex]::Match($Line, $pattern)
    if (!$match.Success) {
        return $null
    }

    return @(
        [double]$match.Groups[1].Value,
        [double]$match.Groups[2].Value,
        [double]$match.Groups[3].Value
    )
}

function Get-VectorFromLineAny([string]$Line, [string[]]$Names) {
    foreach ($name in $Names) {
        $value = Get-VectorFromLine $Line $name
        if ($null -ne $value) {
            return $value
        }
    }

    return $null
}

function Get-VectorDistance($A, $B) {
    if ($null -eq $A -or $null -eq $B -or $A.Count -lt 3 -or $B.Count -lt 3) {
        return [double]::PositiveInfinity
    }

    $dx = [double]$A[0] - [double]$B[0]
    $dy = [double]$A[1] - [double]$B[1]
    $dz = [double]$A[2] - [double]$B[2]
    return [Math]::Sqrt($dx * $dx + $dy * $dy + $dz * $dz)
}

function Get-HandControllerProofMetrics([string]$Text, [string]$Marker) {
    $aimErrors = New-Object System.Collections.Generic.List[double]
    $forwardErrors = New-Object System.Collections.Generic.List[double]
    $hands = @{}
    $linePattern = 'SWGVRHandsProof kind=controller[^\r\n]*targetPose=' + [regex]::Escape($Marker) + '[^\r\n]*'

    foreach ($match in [regex]::Matches($Text, $linePattern)) {
        $line = $match.Value
        $handMatch = [regex]::Match($line, 'hand=(\d+)')
        if ($handMatch.Success) {
            $hands[$handMatch.Groups[1].Value] = $true
        }

        $aimPosition = Get-VectorFromLineAny $line @("aim_p", "rayOrigin_p")
        $targetPosition = Get-VectorFromLineAny $line @("target_p", "handPivot_p")
        $aimErrors.Add((Get-VectorDistance $targetPosition $aimPosition)) | Out-Null

        $forwardScalar = [regex]::Match($line, 'handForwardMinusRayForwardMag=([0-9.]+)')
        if ($forwardScalar.Success) {
            $forwardErrors.Add([double]$forwardScalar.Groups[1].Value) | Out-Null
        } else {
            $rayForward = Get-VectorFromLineAny $line @("aim_f", "rayForward_p")
            $handForward = Get-VectorFromLine $line "handForward_p"
            $forwardErrors.Add((Get-VectorDistance $handForward $rayForward)) | Out-Null
        }
    }

    $maxAim = if ($aimErrors.Count -gt 0) { ($aimErrors | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
    $maxForward = if ($forwardErrors.Count -gt 0) { ($forwardErrors | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
    return [PSCustomObject]@{
        Count = $aimErrors.Count
        HandCount = $hands.Count
        MaxAimError = [double]$maxAim
        MaxForwardError = [double]$maxForward
    }
}

function Get-ControllerPoseSpaceMetrics([string]$Text, [string]$PoseSpace) {
    $magnitudes = New-Object System.Collections.Generic.List[double]
    $hands = @{}
    $linePattern = 'SWGVRHandsProof kind=controller[^\r\n]*poseSpace=' + [regex]::Escape($PoseSpace) + '[^\r\n]*'

    foreach ($match in [regex]::Matches($Text, $linePattern)) {
        $line = $match.Value
        $handMatch = [regex]::Match($line, 'hand=(\d+)')
        if ($handMatch.Success) {
            $hands[$handMatch.Groups[1].Value] = $true
        }

        $pivot = Get-VectorFromLineAny $line @("handPivot_p", "rayOrigin_p")
        if ($null -ne $pivot -and $pivot.Count -ge 3) {
            $x = [double]$pivot[0]
            $y = [double]$pivot[1]
            $z = [double]$pivot[2]
            $magnitudes.Add([Math]::Sqrt($x * $x + $y * $y + $z * $z)) | Out-Null
        }
    }

    $maxMagnitude = if ($magnitudes.Count -gt 0) { ($magnitudes | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
    return [PSCustomObject]@{
        Count = $magnitudes.Count
        HandCount = $hands.Count
        MaxMagnitude = [double]$maxMagnitude
    }
}

function Get-HandMeshDeltaMagnitudes([string]$Text) {
    $values = New-Object System.Collections.Generic.List[double]
    foreach ($match in [regex]::Matches($Text, 'SWGVRHandsProof kind=mesh[^\r\n]*deltaMag=([0-9.]+)')) {
        $parsed = 0.0
        if ([double]::TryParse($match.Groups[1].Value, [ref]$parsed)) {
            $values.Add($parsed) | Out-Null
        }
    }
    return @($values)
}

function Get-ProofScalarMagnitudes([string]$Text, [string]$Kind, [string]$ScalarName) {
    $values = New-Object System.Collections.Generic.List[double]
    $pattern = 'SWGVRHandsProof kind=' + [regex]::Escape($Kind) + '[^\r\n]*' + [regex]::Escape($ScalarName) + '=([0-9.]+)'
    foreach ($match in [regex]::Matches($Text, $pattern)) {
        $parsed = 0.0
        if ([double]::TryParse($match.Groups[1].Value, [ref]$parsed)) {
            $values.Add($parsed) | Out-Null
        }
    }
    return @($values)
}

function Get-LatestHardpointPivotMetrics([string]$Text) {
    $latest = @{}
    foreach ($match in [regex]::Matches($Text, 'SWGVRHandsProof kind=hardpointPivot[^\r\n]*')) {
        $line = $match.Value
        $handMatch = [regex]::Match($line, 'hand=(\d+)')
        $magMatch = [regex]::Match($line, 'hardpointMinusTargetMag=([0-9.]+)')
        $predictedMatch = [regex]::Match($line, 'predictedHardpointMinusTargetMag=([0-9.]+)')
        if (!$handMatch.Success -or !$magMatch.Success) {
            continue
        }

        $parsed = 0.0
        if (![double]::TryParse($magMatch.Groups[1].Value, [ref]$parsed)) {
            continue
        }
        $predicted = $parsed
        if ($predictedMatch.Success) {
            $parsedPredicted = 0.0
            if ([double]::TryParse($predictedMatch.Groups[1].Value, [ref]$parsedPredicted)) {
                $predicted = [double]$parsedPredicted
            }
        }

        $latest[$handMatch.Groups[1].Value] = [PSCustomObject]@{
            Magnitude = [double]$parsed
            EffectiveMagnitude = [double]$predicted
            Found = [regex]::IsMatch($line, 'found=true')
            CorrectionEnabled = [regex]::IsMatch($line, 'correctionEnabled=true')
            ReadyAfterCorrection = [regex]::IsMatch($line, 'readyAfterCorrection=true')
            Line = $line
        }
    }

    $maxMagnitude = [double]::PositiveInfinity
    $maxEffectiveMagnitude = [double]::PositiveInfinity
    if ($latest.Count -gt 0) {
        $maxMagnitude = (($latest.Values | ForEach-Object { $_.Magnitude }) | Measure-Object -Maximum).Maximum
        $maxEffectiveMagnitude = (($latest.Values | ForEach-Object { $_.EffectiveMagnitude }) | Measure-Object -Maximum).Maximum
    }
    $allFound = $latest.Count -gt 0 -and @(($latest.Values | Where-Object { !$_.Found })).Count -eq 0
    $allCorrectionEnabled = $latest.Count -gt 0 -and @(($latest.Values | Where-Object { !$_.CorrectionEnabled })).Count -eq 0
    $allReadyAfterCorrection = $latest.Count -gt 0 -and @(($latest.Values | Where-Object { !$_.ReadyAfterCorrection })).Count -eq 0
    return [PSCustomObject]@{
        HandCount = $latest.Count
        MaxLatestMagnitude = [double]$maxMagnitude
        MaxLatestEffectiveMagnitude = [double]$maxEffectiveMagnitude
        AllFound = $allFound
        AllCorrectionEnabled = $allCorrectionEnabled
        AllReadyAfterCorrection = $allReadyAfterCorrection
    }
}

function Get-VisibleAfterBothValidMilliseconds([string]$Text) {
    $values = New-Object System.Collections.Generic.List[double]
    foreach ($match in [regex]::Matches($Text, 'SWGVRDetachedHands visible strict rig[^\r\n]*visibleAfterBothValidMs=(\d+)')) {
        $parsed = 0.0
        if ([double]::TryParse($match.Groups[1].Value, [ref]$parsed)) {
            $values.Add($parsed) | Out-Null
        }
    }
    return @($values)
}

function Get-MenuVisibleAfterBothValidMilliseconds([string]$Text) {
    $values = New-Object System.Collections.Generic.List[double]
    $patterns = @(
        'SWGVRDetachedHands visible menu default rig[^\r\n]*visibleAfterBothValidMs=(\d+)',
        'SWGVRDetachedHands visible strict rig[^\r\n]*mode=menuCreature[^\r\n]*visibleAfterBothValidMs=(\d+)'
    )
    foreach ($pattern in $patterns) {
        foreach ($match in [regex]::Matches($Text, $pattern)) {
            $parsed = 0.0
            if ([double]::TryParse($match.Groups[1].Value, [ref]$parsed)) {
                $values.Add($parsed) | Out-Null
            }
        }
    }
    return @($values)
}

$runtimeExe = Join-Path $ClientDataRoot "SwgClient_r.exe"
$runtimeRenderer = Join-Path $ClientDataRoot "gl05_r.dll"
$builtExe = Join-Path $ClientToolsRoot "src\compile\x64\SwgClient\Release\SwgClient_r.exe"
$builtRenderer = Join-Path $ClientToolsRoot "src\compile\x64\Direct3d11\Release\gl05_r.dll"
$handBuildMarker = "aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback"
$rendererBuildMarker = "rendererHandLayersHardDisabled_noFallback"
if ([string]::IsNullOrWhiteSpace($ProofPath)) {
    $ProofPath = Join-Path $ProofRoot "og_vr_start.log"
}
if ([string]::IsNullOrWhiteSpace($HandTracePath)) {
    $HandTracePath = Join-Path $ProofRoot "og_vr_hands_trace.log"
}
if ([string]::IsNullOrWhiteSpace($ClientLogPath)) {
    $ClientLogPath = Join-Path $ProofRoot "og_vr_client.log"
}

New-Item -ItemType Directory -Force -Path $ProofRoot | Out-Null

$startTime = Get-Date

if ($Launch -and !$AnalyzeOnly) {
    $launcher = Join-Path $ClientToolsRoot "scripts\dev\START-OG-VR.ps1"
    if (!(Test-Path -LiteralPath $launcher)) {
        throw "OG VR launcher not found: $launcher"
    }

    $launcherArgs = @("-EnableLogs")
    if ($NoDeploy) {
        $launcherArgs += "-NoDeploy"
    }

    & $launcher @launcherArgs
    Start-Sleep -Seconds $DurationSeconds
}

if ($AnalyzeOnly) {
    $analysisAnchors = @()
    if (Test-Path -LiteralPath $ProofPath) {
        $analysisAnchors += (Get-Item -LiteralPath $ProofPath).LastWriteTime
    }
    if (Test-Path -LiteralPath $HandTracePath) {
        $analysisAnchors += (Get-Item -LiteralPath $HandTracePath).LastWriteTime
    }
    if ($analysisAnchors.Count -gt 0) {
        $startTime = ($analysisAnchors | Sort-Object | Select-Object -First 1).AddMinutes(-1)
    }
}

$runtimeExeMachine = Get-PeMachine $runtimeExe
$runtimeRendererMachine = Get-PeMachine $runtimeRenderer
$builtExeMachine = Get-PeMachine $builtExe
$builtRendererMachine = Get-PeMachine $builtRenderer

Add-Check "runtime exe is x64" ($runtimeExeMachine -eq "AMD64") "SwgClient_r.exe machine=$runtimeExeMachine"
Add-Check "runtime renderer is x64" ($runtimeRendererMachine -eq "AMD64") "gl05_r.dll machine=$runtimeRendererMachine"
Add-Check "built exe is x64" ($builtExeMachine -eq "AMD64") "built SwgClient_r.exe machine=$builtExeMachine"
Add-Check "built renderer is x64" ($builtRendererMachine -eq "AMD64") "built gl05_r.dll machine=$builtRendererMachine"
Add-Check "runtime hand marker is current" (Test-BinaryContainsMarker $runtimeExe $handBuildMarker) "runtime SwgClient_r.exe must contain $handBuildMarker"
Add-Check "built hand marker is current" (Test-BinaryContainsMarker $builtExe $handBuildMarker) "built SwgClient_r.exe must contain $handBuildMarker"
Add-Check "runtime renderer marker is current" (Test-BinaryContainsMarker $runtimeRenderer $rendererBuildMarker) "runtime gl05_r.dll must contain $rendererBuildMarker"
Add-Check "built renderer marker is current" (Test-BinaryContainsMarker $builtRenderer $rendererBuildMarker) "built gl05_r.dll must contain $rendererBuildMarker"
$builtExeHash = Get-Sha256OrEmpty $builtExe
$runtimeExeHash = Get-Sha256OrEmpty $runtimeExe
$builtRendererHash = Get-Sha256OrEmpty $builtRenderer
$runtimeRendererHash = Get-Sha256OrEmpty $runtimeRenderer
Add-Check "runtime exe matches built exe" ($builtExeHash.Length -gt 0 -and $builtExeHash -eq $runtimeExeHash) "built=$builtExeHash runtime=$runtimeExeHash"
Add-Check "runtime renderer matches built renderer" ($builtRendererHash.Length -gt 0 -and $builtRendererHash -eq $runtimeRendererHash) "built=$builtRendererHash runtime=$runtimeRendererHash"

$processes = @(Get-Process -Name SwgClient_r -ErrorAction SilentlyContinue)
if ($Launch -and !$AnalyzeOnly) {
    Add-Check "client remains alive after launch window" ($processes.Count -gt 0) "running SwgClient_r count=$($processes.Count)"
}

$freshCrashFiles = @(Get-ChildItem -LiteralPath $ClientDataRoot -ErrorAction SilentlyContinue |
    Where-Object {
        $_.LastWriteTime -ge $startTime -and
        ($_.Name -like "SwgClient_r.exe-*.txt" -or $_.Name -like "SwgClient_r.exe-*.mdmp")
    })
Add-Check "no fresh crash dump/report" ($freshCrashFiles.Count -eq 0) (($freshCrashFiles | Select-Object -ExpandProperty Name) -join ", ")

$proofText = Read-TextOrEmpty $ProofPath
$handText = Read-TextOrEmpty $HandTracePath
$clientText = Read-TextOrEmpty $ClientLogPath
$crashText = ""
foreach ($crashFile in $freshCrashFiles) {
    if ($crashFile.Name -like "*.txt") {
        $crashText += "`n" + (Read-TextOrEmpty $crashFile.FullName)
    }
}
$allLogText = $proofText + "`n" + $handText + "`n" + $clientText + "`n" + $crashText

Add-Check "proof log exists" ($proofText.Length -gt 0) $ProofPath
Add-Check "hand trace exists" ($handText.Length -gt 0) $HandTracePath
Add-Check "OpenXR probe ready" (Test-Regex $proofText '"event":"openxrProbe","ready":true') "requires OpenXR probe ready=true"
Add-Check "OpenXR session begins" (Test-Regex $proofText '"event":"openxrBeginSession","ready":true') "requires begin session"
Add-Check "projection stereo scale matches native OpenXR overlays" (Test-Regex $proofText '"event":"openxrEyePoseScale"[^\r\n]*"stereoScale":1\.000') "requires stereoScale=1.000 so scene hands and native aim-ray overlays share eye geometry"
Add-Check "menu hands submit in stereo projection" (Test-Regex $proofText '"event":"openxrProjectionSubmit"[^\r\n]*"menuQuadLayers":1[^\r\n]*"alphaProjection":true') "requires menu backbuffer quad plus transparent stereo hand projection"
Add-Check "controller poses become tracked" (Test-Regex $proofText '"gripPositionTracked":true') "requires at least one tracked grip pose"
Add-Check "OpenXR hand poses publish world-space" (Test-Regex $proofText '"event":"openxrHandPoseSpace"[^\r\n]*"headRelative":false') "requires OpenXR hand pose events with headRelative=false"
Add-Check "no head-relative hand pose publish" (!(Test-Regex $proofText '"event":"openxrHandPoseSpace"[^\r\n]*"headRelative":true')) "blocks head-locked hand behavior"
Add-Check "no 2D hand layer submit" (!(Test-Regex $proofText '"event":"openxrHandLayers"')) "blocks mirrored-hand-atlas fallback"
Add-Check "hand target contract is aim pivot plus aim-forward rotation" (Test-Regex $handText 'targetPose=aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback') "requires current detached-hand target contract in hand trace"
Add-Check "menu hand renderer actually draws" (Test-Regex $handText 'SWGVRDetachedHands rendered menu stereo hands over backbuffer quad') "requires private stereo menu hand renderer to render at least once"
$menuLocalPoseMetrics = Get-ControllerPoseSpaceMetrics $handText "openxrLocal"
Add-Check "menu controller proof stays in OpenXR-local meter space" ($menuLocalPoseMetrics.HandCount -eq 2 -and $menuLocalPoseMetrics.MaxMagnitude -le 5.0) ("openxrLocalProofCount={0} hands={1} maxPivotDistanceFromOrigin={2:N3}m" -f $menuLocalPoseMetrics.Count, $menuLocalPoseMetrics.HandCount, $menuLocalPoseMetrics.MaxMagnitude)
Add-Check "no 2D/default overlay hand fallback" (!(Test-Regex $allLogText 'openxrHandLayers|mirrored-hand-atlas|2D hand layer')) "blocks non-scene fallback hand rendering"
Add-Check "no stale pose freeze fallback" (!(Test-Regex $handText 'holding stale pose')) "stale OpenXR poses must expire, not freeze hidden fallback hands"
$controllerMetrics = Get-HandControllerProofMetrics $handText $handBuildMarker
Add-Check "controller proof covers both hands" ($controllerMetrics.HandCount -eq 2) ("controllerProofCount={0} hands={1}" -f $controllerMetrics.Count, $controllerMetrics.HandCount)
Add-Check "hand target position equals aim pivot" ($controllerMetrics.Count -gt 0 -and $controllerMetrics.MaxAimError -le 0.005) ("maxAimTargetError={0:N6}m" -f $controllerMetrics.MaxAimError)
$raySharedPivotDeltas = @(Get-ProofScalarMagnitudes $handText "controller" "rayMinusSharedPivotMag")
$raySharedPivotDeltaMax = if ($raySharedPivotDeltas.Count -gt 0) { ($raySharedPivotDeltas | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
Add-Check "ray origin is the shared pivot" ($raySharedPivotDeltas.Count -gt 0 -and $raySharedPivotDeltaMax -le 0.001) ("raySharedPivotProofCount={0} maxRayMinusSharedPivot={1:N6}m" -f $raySharedPivotDeltas.Count, $raySharedPivotDeltaMax)
$softwareGripSharedPivotDeltas = @(Get-ProofScalarMagnitudes $handText "controller" "softwareGripMinusSharedPivotMag")
$softwareGripSharedPivotDeltaMax = if ($softwareGripSharedPivotDeltas.Count -gt 0) { ($softwareGripSharedPivotDeltas | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
Add-Check "software grip pivot is the shared pivot" ($softwareGripSharedPivotDeltas.Count -gt 0 -and $softwareGripSharedPivotDeltaMax -le 0.001) ("softwareGripProofCount={0} maxSoftwareGripMinusSharedPivot={1:N6}m" -f $softwareGripSharedPivotDeltas.Count, $softwareGripSharedPivotDeltaMax)
$handSharedPivotDeltas = @(Get-ProofScalarMagnitudes $handText "controller" "handMinusSharedPivotMag")
$handSharedPivotDeltaMax = if ($handSharedPivotDeltas.Count -gt 0) { ($handSharedPivotDeltas | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
Add-Check "hand pivot is the shared pivot" ($handSharedPivotDeltas.Count -gt 0 -and $handSharedPivotDeltaMax -le 0.005) ("handSharedPivotProofCount={0} maxHandMinusSharedPivot={1:N6}m" -f $handSharedPivotDeltas.Count, $handSharedPivotDeltaMax)
$controllerPivotDeltas = @(Get-ProofScalarMagnitudes $handText "controller" "handMinusRayOriginMag")
if ($controllerPivotDeltas.Count -eq 0) {
    $controllerPivotDeltas = @(Get-ProofScalarMagnitudes $handText "controller" "targetMinusAimMag")
}
$controllerPivotDeltaMax = if ($controllerPivotDeltas.Count -gt 0) { ($controllerPivotDeltas | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
Add-Check "controller hand-minus-ray-origin pivot delta is zero" ($controllerPivotDeltas.Count -gt 0 -and $controllerPivotDeltaMax -le 0.005) ("controllerPivotProofCount={0} maxHandMinusRayOrigin={1:N6}m" -f $controllerPivotDeltas.Count, $controllerPivotDeltaMax)
Add-Check "hand finger-forward equals aim ray forward" ($controllerMetrics.Count -gt 0 -and $controllerMetrics.MaxForwardError -le 0.005) ("maxHandForwardMinusRayForward={0:N6}" -f $controllerMetrics.MaxForwardError)
$leftHandAttached = Test-Regex $handText 'SWGVRDetachedHands attached hand(?: hand)?=0'
$rightHandAttached = Test-Regex $handText 'SWGVRDetachedHands attached hand(?: hand)?=1'
$missingHandTransformSpam = Test-Regex $handText 'no supported hand/wrist joint found'
Add-Check "character/world hand rig attaches left" $leftHandAttached "requires left SWG hand transform modifier"
Add-Check "character/world hand rig attaches right" $rightHandAttached "requires right SWG hand transform modifier"
Add-Check "no unresolved missing hand transform spam" (!$missingHandTransformSpam -or ($leftHandAttached -and $rightHandAttached)) "missingSpam=$missingHandTransformSpam leftAttached=$leftHandAttached rightAttached=$rightHandAttached"
$meshDeltas = @(Get-HandMeshDeltaMagnitudes $handText)
$meshDeltaMax = if ($meshDeltas.Count -gt 0) { ($meshDeltas | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
Add-Check "mesh bind pivot solves onto target" ($meshDeltas.Count -gt 0 -and $meshDeltaMax -le 0.005) ("meshProofCount={0} maxDelta={1:N6}m" -f $meshDeltas.Count, $meshDeltaMax)
$hardpointMetrics = Get-LatestHardpointPivotMetrics $handText
$renderedSharedPivotDeltas = @(Get-ProofScalarMagnitudes $handText "hardpointPivot" "renderedHardpointMinusSharedPivotMag")
$renderedSharedPivotDeltaMax = if ($renderedSharedPivotDeltas.Count -gt 0) { ($renderedSharedPivotDeltas | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
Add-Check "rendered hardpoint is the shared pivot" ($renderedSharedPivotDeltas.Count -gt 0 -and $renderedSharedPivotDeltaMax -le 0.010) ("renderedSharedPivotProofCount={0} maxRenderedHardpointMinusSharedPivot={1:N6}m" -f $renderedSharedPivotDeltas.Count, $renderedSharedPivotDeltaMax)
Add-Check "rendered hardpoint pivot corrects onto target for both hands" ($hardpointMetrics.HandCount -eq 2 -and $hardpointMetrics.AllFound -and $hardpointMetrics.AllCorrectionEnabled -and $hardpointMetrics.AllReadyAfterCorrection -and $hardpointMetrics.MaxLatestMagnitude -le 0.010) ("hardpointHands={0} allFound={1} correctionEnabled={2} readyAfterCorrection={3} maxMeasured={4:N6}m maxPredicted={5:N6}m" -f $hardpointMetrics.HandCount, $hardpointMetrics.AllFound, $hardpointMetrics.AllCorrectionEnabled, $hardpointMetrics.AllReadyAfterCorrection, $hardpointMetrics.MaxLatestMagnitude, $hardpointMetrics.MaxLatestEffectiveMagnitude)
$visibleAfterValues = @(Get-VisibleAfterBothValidMilliseconds $handText)
$visibleAfterMax = if ($visibleAfterValues.Count -gt 0) { ($visibleAfterValues | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
Add-Check "strict hand rig becomes visible promptly after both poses" ($visibleAfterValues.Count -gt 0 -and $visibleAfterMax -le 3000.0) ("visibleEvents={0} maxVisibleAfterBothValidMs={1:N0}" -f $visibleAfterValues.Count, $visibleAfterMax)
$menuVisibleAfterValues = @(Get-MenuVisibleAfterBothValidMilliseconds $handText)
$menuVisibleAfterMax = if ($menuVisibleAfterValues.Count -gt 0) { ($menuVisibleAfterValues | Measure-Object -Maximum).Maximum } else { [double]::PositiveInfinity }
Add-Check "menu/default 3D hand rig becomes visible promptly" ($menuVisibleAfterValues.Count -gt 0 -and $menuVisibleAfterMax -le 3000.0) ("menuVisibleEvents={0} maxMenuVisibleAfterBothValidMs={1:N0}" -f $menuVisibleAfterValues.Count, $menuVisibleAfterMax)
Add-Check "no fatal asset load" (!(Test-Regex $allLogText 'FATAL|open ''.*'' not found|open .* not found')) "blocks world-entry crash"

if ($StopAfter -and $processes.Count -gt 0) {
    $processes | Stop-Process -Force
}

Write-Output "=== OG VR Hands Verification ==="
foreach ($check in $checks) {
    $status = if ($check.Pass) { "PASS" } else { "FAIL" }
    Write-Output ("[{0}] {1} - {2}" -f $status, $check.Name, $check.Detail)
}

$failed = @($checks | Where-Object { !$_.Pass })
if ($failed.Count -gt 0) {
    Write-Output ""
    Write-Output "FAILED CHECKS:"
    $failed | ForEach-Object { Write-Output (" - {0}: {1}" -f $_.Name, $_.Detail) }
    exit 1
}

Write-Output ""
Write-Output "All OG VR hand gates passed."
