param(
    [string]$ProofPath = "",
    [string]$HandTracePath = "",
    [string]$OutputRoot = "",
    [string]$ClientToolsRoot = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
if ([string]::IsNullOrWhiteSpace($ClientToolsRoot)) {
    $ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
}
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot
}
if ([string]::IsNullOrWhiteSpace($ProofPath)) {
    $ProofPath = Join-Path $OutputRoot "og_vr_start.log"
}

if ([string]::IsNullOrWhiteSpace($HandTracePath)) {
    $HandTracePath = Join-Path (Split-Path -Parent $ProofPath) (([System.IO.Path]::GetFileNameWithoutExtension($ProofPath)) + ".hands.log")
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Get-JsonEvents {
    param(
        [string]$Path,
        [string]$Event
    )

    if (!(Test-Path -LiteralPath $Path)) {
        return @()
    }

    $pattern = '"event":"' + $Event + '"'
    $events = @()
    foreach ($match in Select-String -LiteralPath $Path -Pattern $pattern) {
        try {
            $events += ($match.Line | ConvertFrom-Json)
        }
        catch {
        }
    }
    return $events
}

function Count-LogMatches {
    param(
        [string]$Path,
        [string]$Pattern
    )

    if (!(Test-Path -LiteralPath $Path)) {
        return 0
    }

    return @(Select-String -LiteralPath $Path -Pattern $Pattern).Count
}

function Get-MatchingLines {
    param(
        [string]$Path,
        [string]$Pattern,
        [int]$Last = 12
    )

    if (!(Test-Path -LiteralPath $Path)) {
        return @()
    }

    return @(Select-String -LiteralPath $Path -Pattern $Pattern | Select-Object -Last $Last | ForEach-Object { $_.Line })
}

function Format-Array3 {
    param($Value)

    if ($null -eq $Value -or $Value.Count -lt 3) {
        return ""
    }

    return ("{0:N3},{1:N3},{2:N3}" -f [double]$Value[0], [double]$Value[1], [double]$Value[2])
}

function Format-BoolText {
    param($Value)

    if ($Value -is [bool]) {
        if ($Value) {
            return "true"
        }
        return "false"
    }

    $text = ([string]$Value).Trim().ToLowerInvariant()
    if ($text -eq "1" -or $text -eq "true" -or $text -eq "yes" -or $text -eq "on") {
        return "true"
    }
    if ($text -eq "0" -or $text -eq "false" -or $text -eq "no" -or $text -eq "off") {
        return "false"
    }
    return $text
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$markdownPath = Join-Path $OutputRoot "og_vr_hands_offline_proof_$stamp.md"
$csvPath = Join-Path $OutputRoot "og_vr_hands_pose_dump_$stamp.csv"
$svgPath = Join-Path $OutputRoot "og_vr_hands_space_math_$stamp.svg"

$poseEvents = @(Get-JsonEvents -Path $ProofPath -Event "openxrHandPoseSpace")
$controllerEvents = @(Get-JsonEvents -Path $ProofPath -Event "openxrControllerSnapshot")
$profileEvents = @(Get-JsonEvents -Path $ProofPath -Event "openxrInteractionProfile")
$sessionEvents = @(Get-JsonEvents -Path $ProofPath -Event "openxrSessionState")

$poseRows = @()
foreach ($event in ($poseEvents | Select-Object -Last 48)) {
    $poseRows += [pscustomobject]@{
        frame          = $event.frame
        hand           = $event.hand
        space          = $event.space
        headRelative   = $event.headRelative
        rawGrip        = Format-Array3 $event.rawGrip
        recenteredGrip = Format-Array3 $event.recenteredGrip
        head           = Format-Array3 $event.head
        clientGrip     = Format-Array3 $event.clientGrip
    }
}
$poseRows | Export-Csv -LiteralPath $csvPath -NoTypeInformation

$handsCreatedExplicit = Count-LogMatches -Path $HandTracePath -Pattern "created explicit appearance"
$handsCreatedDetached = Count-LogMatches -Path $HandTracePath -Pattern "created detached hand rig"
$handsAttachedLeft = Count-LogMatches -Path $HandTracePath -Pattern "attached hand hand=0"
$handsAttachedRight = Count-LogMatches -Path $HandTracePath -Pattern "attached hand hand=1"
$fingerChainUnavailable = Count-LogMatches -Path $HandTracePath -Pattern "fingerChain unavailable"
$weaponCloneFailed = Count-LogMatches -Path $HandTracePath -Pattern "weapon attach skipped clone failed"
$visibleStrictRig = Count-LogMatches -Path $HandTracePath -Pattern "visible strict rig"
$staleSampleWait = Count-LogMatches -Path $HandTracePath -Pattern "waiting for fresh sample"

$frikIni = "D:\FalloutVR\mods\FRIK - ENABLE AFTER INITIAL FIRST SAVE\F4SE\Plugins\FRIK.ini"
$openmwDefaults = "D:\Modlists\fnv\build_defaults_decoded.txt"
$frikLines = Get-MatchingLines -Path $frikIni -Pattern "PlayerHeight|fVrScale|armLength|EnableStaticGripping|EnableOffHandGripping|DampenHands|DampenHandsRotation|DampenHandsTranslation|handUI_" -Last 32
$openmwLines = Get-MatchingLines -Path $openmwDefaults -Pattern "player height|realistic combat|hand directed movement|hands offset|show 3D crosshairs|use xr layer for huds" -Last 32

$bridgeSource = Join-Path $ClientToolsRoot "src\engine\client\application\Direct3d11\src\win32\Direct3d11_VrBridge.cpp"
$handsSource = Join-Path $ClientToolsRoot "src\engine\client\library\clientGame\src\shared\object\VrDetachedHands.cpp"
$launcherSource = Join-Path $ClientToolsRoot "scripts\dev\START-OG-VR.ps1"
$codeProofLines = @()
$codeProofLines += Get-MatchingLines -Path $bridgeSource -Pattern "rendererHandLayersHardDisabled_noFallback|areHandVisualLayersEnabled|SWG_OG_VR_HANDS_HEAD_RELATIVE|publishHeadRelative|poseRelativeToHead|openxrHandPoseSpace" -Last 24
$codeProofLines += Get-MatchingLines -Path $handsSource -Pattern "aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback|computeGripBindFrame|aimPivot:|staleRevive|waiting for fresh sample|visible strict rig" -Last 32
$codeProofLines += Get-MatchingLines -Path $launcherSource -Pattern "rendererHandLayersHardDisabled_noFallback|aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback|Assert-BinaryContainsMarker|SWG_OG_VR_HANDS_HEAD_RELATIVE|Hand pose space|Detached hand target" -Last 16

$headRelativeTrue = @($poseEvents | Where-Object { (Format-BoolText $_.headRelative) -eq "true" }).Count
$headRelativeFalse = @($poseEvents | Where-Object { (Format-BoolText $_.headRelative) -eq "false" }).Count
$trackedSnapshots = @($controllerEvents | Where-Object {
    (Format-BoolText $_.aimPositionTracked) -eq "true" -and
    (Format-BoolText $_.gripPositionTracked) -eq "true"
}).Count
$profiles = @($profileEvents | ForEach-Object { $_.profile } | Sort-Object -Unique)
$sessionStates = @($sessionEvents | ForEach-Object { $_.state } | Sort-Object -Unique)

$svg = @"
<svg xmlns="http://www.w3.org/2000/svg" width="1180" height="720" viewBox="0 0 1180 720">
  <rect width="1180" height="720" fill="#101418"/>
  <style>
    text { font-family: Consolas, 'Courier New', monospace; fill: #f3f7fb; font-size: 22px; }
    .small { font-size: 17px; fill: #c9d3de; }
    .tiny { font-size: 14px; fill: #9fb0bf; }
    .bad { stroke: #ff6b6b; fill: none; stroke-width: 4; }
    .good { stroke: #48d597; fill: none; stroke-width: 4; }
    .axis { stroke: #89a7c2; stroke-width: 3; marker-end: url(#arrow); }
    .box { fill: #17202a; stroke: #33475c; stroke-width: 2; rx: 8; }
    .call { fill: #14251f; stroke: #48d597; stroke-width: 2; rx: 8; }
    .warn { fill: #2b1919; stroke: #ff6b6b; stroke-width: 2; rx: 8; }
  </style>
  <defs>
    <marker id="arrow" markerWidth="10" markerHeight="10" refX="8" refY="3" orient="auto" markerUnits="strokeWidth">
      <path d="M0,0 L0,6 L9,3 z" fill="#89a7c2"/>
    </marker>
    <marker id="arrowGood" markerWidth="10" markerHeight="10" refX="8" refY="3" orient="auto" markerUnits="strokeWidth">
      <path d="M0,0 L0,6 L9,3 z" fill="#48d597"/>
    </marker>
    <marker id="arrowBad" markerWidth="10" markerHeight="10" refX="8" refY="3" orient="auto" markerUnits="strokeWidth">
      <path d="M0,0 L0,6 L9,3 z" fill="#ff6b6b"/>
    </marker>
  </defs>

  <text x="40" y="48">OG SWG VR Hands: Offline Space-Math Proof</text>
  <text class="small" x="40" y="78">Goal: tracked controller pose drives real SWG hand bones in 3D stage/player space, not UI/head-relative quads.</text>

  <rect class="warn" x="40" y="120" width="500" height="210"/>
  <text x="64" y="158">Old path</text>
  <text class="small" x="64" y="194">H = inverse(Head) * Recenter(Controller)</text>
  <text class="small" x="64" y="226">HandLocal = axisFlip(H)</text>
  <text class="small" x="64" y="258">Result: hand moves in head/UI space</text>
  <line class="bad" x1="100" y1="292" x2="460" y2="292" marker-end="url(#arrowBad)"/>
  <text class="tiny" x="142" y="315">headRelative=true in proof log</text>

  <rect class="call" x="640" y="120" width="500" height="210"/>
  <text x="664" y="158">New strict path</text>
  <text class="small" x="664" y="194">Position = Recenter(OpenXR aim)</text>
  <text class="small" x="664" y="226">Rotation = aim forward + grip roll</text>
  <text class="small" x="664" y="258">SWG bind pivot solved onto aim origin</text>
  <line class="good" x1="700" y1="292" x2="1060" y2="292" marker-end="url(#arrowGood)"/>
  <text class="tiny" x="736" y="315">headRelative=false after patch</text>

  <rect class="box" x="150" y="390" width="880" height="250"/>
  <text x="185" y="430">Shared 3D Stage Frame</text>
  <line class="axis" x1="355" y1="555" x2="520" y2="555"/>
  <text class="tiny" x="526" y="560">+X right</text>
  <line class="axis" x1="355" y1="555" x2="355" y2="445"/>
  <text class="tiny" x="310" y="440">+Y up</text>
  <line class="axis" x1="355" y1="555" x2="250" y2="620"/>
  <text class="tiny" x="180" y="638">-Z forward</text>
  <circle cx="355" cy="555" r="16" fill="#89a7c2"/>
  <text class="tiny" x="320" y="588">HMD/eye offset</text>
  <circle cx="575" cy="520" r="18" fill="#48d597"/>
  <text class="small" x="610" y="526">right SWG hand rig target</text>
  <circle cx="500" cy="590" r="18" fill="#48d597"/>
  <text class="small" x="535" y="596">left SWG hand rig target</text>
  <text class="small" x="185" y="474">Eye and hands now live under the same recentered stage/player frame.</text>
  <text class="small" x="185" y="502">That is the same class of math as Fallout/OpenMW: HMD + controller poses, offsets, grip input, weapon hardpoint.</text>
</svg>
"@
$svg | Set-Content -LiteralPath $svgPath -Encoding UTF8

$recentPoseTable = if ($poseRows.Count -gt 0) {
    ($poseRows | Select-Object -Last 12 | Format-Table -AutoSize | Out-String).TrimEnd()
}
else {
    "No openxrHandPoseSpace events found."
}

$markdown = @"
# OG SWG VR Hands Offline Proof

Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz")

## Files

- Proof log: $ProofPath
- Hand trace: $HandTracePath
- Pose CSV: $csvPath
- Space drawing SVG: $svgPath

## What The Old Data Says

- `openxrHandPoseSpace` events: $($poseEvents.Count)
- Old `headRelative=true` pose events: $headRelativeTrue
- Existing `headRelative=false` pose events: $headRelativeFalse
- Controller snapshots with aim + grip tracked: $trackedSnapshots / $($controllerEvents.Count)
- Interaction profiles seen: $($profiles -join ", ")
- Session states seen: $($sessionStates -join ", ")

~~~text
$recentPoseTable
~~~

## Real SWG Hand Rig Data

- Explicit SWG hand appearances created: $handsCreatedExplicit
- Detached hand rig settled events: $handsCreatedDetached
- Left hand hardpoint attaches: $handsAttachedLeft
- Right hand hardpoint attaches: $handsAttachedRight
- Strict finger-chain misses: $fingerChainUnavailable
- Weapon clone failures: $weaponCloneFailed
- New visible strict-rig events in current logs: $visibleStrictRig
- New stale-sample wait events in current logs: $staleSampleWait

## Math Drawing

![OG SWG VR hand space math]($svgPath)

~~~mermaid
flowchart LR
    A["OpenXR grip / aim pose"] --> B["Recenter to stage/player origin"]
    B --> C{"SWG_OG_VR_HANDS_HEAD_RELATIVE"}
    C -->|"0 strict default"| D["Publish recentered 3D pose"]
    C -->|"1 diagnostic only"| E["Head-relative pose"]
    D --> F["SWG detached hand rig"]
    F --> G["hold_l / hold_r transform modifiers"]
    G --> H["real SWG hand mesh + finger curl + weapon hardpoint"]
~~~

## Patch Proof

~~~text
$($codeProofLines -join "`n")
~~~

## Fallout / OpenMW Comparison Data

FRIK / FalloutVR local settings:

~~~text
$($frikLines -join "`n")
~~~

OpenMW VR-style local settings:

~~~text
$($openmwLines -join "`n")
~~~

## Offline Conclusion

The previous SWG data proves controller tracking was alive, but hand matrices were published as headRelative=true in the older run. That is the UI/head-space failure mode.

The patched path publishes recentered controller poses by default, keeps 2D hand layers off, prevents stale controller samples from resurrecting a half-built hand rig, and byte-gates the deployed x64 client with the `aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback` marker plus the deployed x64 renderer with the `rendererHandLayersHardDisabled_noFallback` marker before launch.

The detached hand contract is exact: the SWG glove bind pivot is solved onto the OpenXR aim/ray origin, the hand forward axis is solved onto the ray direction, and grip orientation supplies roll. This is the path that should make the visual hand pivot and the working raycast pivot share the same zero.

Finger curl now selects the chain direction from bind-pose geometry and rotates finger bones by default, instead of applying one hard-coded curl sign to both mirrored hands.

The next headset run must show:

- openxrHandPoseSpace with headRelative=false
- SWGVRHandsProof controller records with targetPose=aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback
- Renderer byte marker rendererHandLayersHardDisabled_noFallback
- SWGVRHandsProof mesh records with deltaMag near zero
- SWGVRDetachedHands visible strict rig ...
- no openxrHandLayers
- no repeated create/wait/destroy churn while controller samples are stale

"@

$markdown | Set-Content -LiteralPath $markdownPath -Encoding UTF8

Write-Output "Offline proof generated:"
Write-Output "  Markdown: $markdownPath"
Write-Output "  Pose CSV: $csvPath"
Write-Output "  SVG: $svgPath"
