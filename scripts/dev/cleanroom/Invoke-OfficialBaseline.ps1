param(
    [string]$CleanroomRoot = "",
    [string]$LocalLoginConfig = "",
    [switch]$Reset,
    [switch]$ResetDownloads,
    [switch]$CleanDownloadsAfterSuccess,
    [switch]$KeepNestedArchive,
    [switch]$SkipLaunch
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptRoot "..\Resolve-NikamiWorkspace.ps1")
$repoRoot = (Resolve-Path (Join-Path $scriptRoot "..\..\..")).Path
$workspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $repoRoot
if ([string]::IsNullOrWhiteSpace($CleanroomRoot)) {
    $CleanroomRoot = Get-NikamiCleanroomRoot -WorkspaceRoot $workspaceRoot
}

$releaseParts = @(
    @{
        Name = "SWGSource.Client.v3.0.-.Split.7z.001"
        Url = "https://github.com/SWG-Source/releases/releases/download/swgsourceclientv3.0/SWGSource.Client.v3.0.-.Split.7z.001"
        Size = 2097152000
    },
    @{
        Name = "SWGSource.Client.v3.0.-.Split.7z.002"
        Url = "https://github.com/SWG-Source/releases/releases/download/swgsourceclientv3.0/SWGSource.Client.v3.0.-.Split.7z.002"
        Size = 2097152000
    },
    @{
        Name = "SWGSource.Client.v3.0.-.Split.7z.003"
        Url = "https://github.com/SWG-Source/releases/releases/download/swgsourceclientv3.0/SWGSource.Client.v3.0.-.Split.7z.003"
        Size = 2097152000
    },
    @{
        Name = "SWGSource.Client.v3.0.-.Split.7z.004"
        Url = "https://github.com/SWG-Source/releases/releases/download/swgsourceclientv3.0/SWGSource.Client.v3.0.-.Split.7z.004"
        Size = 1550775857
    }
)

$downloadRoot = Join-Path $CleanroomRoot "downloads\official-swgsource-client-v3.0"
$runtimeRoot = Join-Path $CleanroomRoot "runtime-official-baseline"
$manifestRoot = Join-Path $CleanroomRoot "manifests"
$logRoot = Join-Path $CleanroomRoot "logs"
$proofRoot = Join-Path $CleanroomRoot "proof"
$downloadManifestPath = Join-Path $manifestRoot "official-release-downloads.csv"

function Find-7Zip {
    foreach ($command in @("7z.exe", "7za.exe", "7zz.exe")) {
        $found = Get-Command $command -ErrorAction SilentlyContinue
        if ($found) { return $found.Source }
    }

    foreach ($path in @("C:\Program Files\7-Zip\7z.exe", "C:\Program Files (x86)\7-Zip\7z.exe")) {
        if (Test-Path -LiteralPath $path) { return $path }
    }

    throw "7-Zip was not found. Install 7-Zip, then rerun this script."
}

function Download-ReleasePart {
    param(
        [string]$Url,
        [string]$Target,
        [int64]$ExpectedSize
    )

    if ((Test-Path -LiteralPath $Target) -and ((Get-Item -LiteralPath $Target).Length -eq $ExpectedSize)) {
        Write-Output "Already downloaded with expected size: $(Split-Path -Leaf $Target)"
        return
    }

    $partial = "$Target.part"
    Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue

    Write-Output "Downloading: $(Split-Path -Leaf $Target)"
    $curl = Get-Command "curl.exe" -ErrorAction SilentlyContinue
    if (!$curl) { throw "curl.exe is required for the cleanroom official download." }

    & $curl.Source --location --fail --retry 5 --retry-delay 5 --output $partial $Url
    if ($LASTEXITCODE -ne 0) { throw "curl download failed for $Url" }

    $actualSize = (Get-Item -LiteralPath $partial).Length
    if ($actualSize -ne $ExpectedSize) {
        throw "Downloaded size mismatch for $(Split-Path -Leaf $Target): expected $ExpectedSize, got $actualSize"
    }

    Move-Item -LiteralPath $partial -Destination $Target -Force
}

function Test-ReleaseDownloadsReady {
    foreach ($part in $releaseParts) {
        $path = Join-Path $downloadRoot $part.Name
        if (!(Test-Path -LiteralPath $path)) { return $false }
        if ((Get-Item -LiteralPath $path).Length -ne $part.Size) { return $false }
    }
    return $true
}

function Invoke-ClientAssetsUpdate {
    param([string]$Root)

    $updateBat = Get-ChildItem -LiteralPath $Root -Recurse -Filter "UpdateSwgClient.bat" |
        Where-Object { !$_.PSIsContainer } |
        Select-Object -First 1
    if (!$updateBat) { throw "UpdateSwgClient.bat not found under $Root" }

    $clientRoot = Split-Path -Parent $updateBat.FullName
    $git = Join-Path $clientRoot "PortableGit\bin\git.exe"
    if (!(Test-Path -LiteralPath $git)) {
        $gitCommand = Get-Command "git.exe" -ErrorAction SilentlyContinue
        if (!$gitCommand) { throw "Could not find PortableGit or git.exe to fetch SWG-Source/client-assets" }
        $git = $gitCommand.Source
    }

    Push-Location -LiteralPath $clientRoot
    try {
        if (Test-Path -LiteralPath ".git") {
            & $git pull
            if ($LASTEXITCODE -ne 0) { throw "git pull failed in $clientRoot" }
        }
        else {
            & $git init .
            if ($LASTEXITCODE -ne 0) { throw "git init failed in $clientRoot" }
            & $git config pull.rebase false
            if ($LASTEXITCODE -ne 0) { throw "git config failed in $clientRoot" }
            & $git remote add -f origin https://github.com/SWG-Source/client-assets.git
            if ($LASTEXITCODE -ne 0) { throw "git remote add/fetch failed in $clientRoot" }
            & $git checkout master
            if ($LASTEXITCODE -ne 0) { throw "git checkout master failed in $clientRoot" }
        }

        $clientAssetsHead = (& $git rev-parse HEAD).Trim()
        $clientAssetsRemote = (& $git remote get-url origin).Trim()
    }
    finally {
        Pop-Location
    }

    return [pscustomobject]@{
        ClientRoot = $clientRoot
        ClientAssetsRemote = $clientAssetsRemote
        ClientAssetsHead = $clientAssetsHead
    }
}

function Write-FileManifest {
    param([string]$Root, [string]$OutputPath)

    Get-ChildItem -LiteralPath $Root -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            [pscustomobject]@{
                RelativePath = $_.FullName.Substring($Root.Length + 1)
                Length = $_.Length
                LastWriteTimeUtc = $_.LastWriteTimeUtc.ToString("o")
                Sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        } |
        Export-Csv -LiteralPath $OutputPath -NoTypeInformation
}

if ($Reset -and (Test-Path -LiteralPath $CleanroomRoot)) {
    $resolvedCleanroom = (Resolve-Path -LiteralPath $CleanroomRoot).Path
    if ($resolvedCleanroom -ne (Resolve-Path -LiteralPath $CleanroomRoot).Path) {
        throw "Refusing to reset unexpected cleanroom path: $resolvedCleanroom"
    }

    foreach ($target in @($runtimeRoot, $manifestRoot, $logRoot, $proofRoot)) {
        if (Test-Path -LiteralPath $target) {
            $resolvedTarget = (Resolve-Path -LiteralPath $target).Path
            if (!$resolvedTarget.StartsWith($resolvedCleanroom, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to reset outside cleanroom: $resolvedTarget"
            }
            Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
        }
    }
}

if ($ResetDownloads -and (Test-Path -LiteralPath $downloadRoot)) {
    $resolvedCleanroom = if (Test-Path -LiteralPath $CleanroomRoot) { (Resolve-Path -LiteralPath $CleanroomRoot).Path } else { $CleanroomRoot }
    $resolvedDownloads = (Resolve-Path -LiteralPath $downloadRoot).Path
    if (!$resolvedDownloads.StartsWith($resolvedCleanroom, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove downloads outside cleanroom: $resolvedDownloads"
    }
    Remove-Item -LiteralPath $resolvedDownloads -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $downloadRoot, $runtimeRoot, $manifestRoot, $logRoot, $proofRoot | Out-Null

$metadata = [ordered]@{
    StartedUtc = (Get-Date).ToUniversalTime().ToString("o")
    CleanroomRoot = $CleanroomRoot
    ReleaseTag = "swgsourceclientv3.0"
    ReleaseUrl = "https://github.com/SWG-Source/releases/releases/tag/swgsourceclientv3.0"
    ClientAssetsUrl = "https://github.com/SWG-Source/client-assets.git"
    OverlayApplied = $false
}

foreach ($part in $releaseParts) {
    Download-ReleasePart -Url $part.Url -Target (Join-Path $downloadRoot $part.Name) -ExpectedSize $part.Size
}

if (!(Test-ReleaseDownloadsReady)) {
    throw "Official release downloads are not complete after download step."
}

$downloadManifest = foreach ($part in $releaseParts) {
    $path = Join-Path $downloadRoot $part.Name
    [pscustomobject]@{
        Name = $part.Name
        Url = $part.Url
        Length = (Get-Item -LiteralPath $path).Length
        Sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
}
$downloadManifest | Export-Csv -LiteralPath $downloadManifestPath -NoTypeInformation

$sevenZip = Find-7Zip
$firstPart = Join-Path $downloadRoot $releaseParts[0].Name

Write-Output "Extracting official split archive into $runtimeRoot"
& $sevenZip x $firstPart "-o$runtimeRoot" -y
if ($LASTEXITCODE -ne 0) { throw "7-Zip split extraction failed" }

$nestedArchive = Get-ChildItem -LiteralPath $runtimeRoot -Recurse -Filter "*.7z" |
    Where-Object { !$_.PSIsContainer } |
    Select-Object -First 1
if (!$nestedArchive) { throw "Nested official client archive was not produced under $runtimeRoot" }

Write-Output "Extracting nested official client archive: $($nestedArchive.FullName)"
& $sevenZip x $nestedArchive.FullName "-o$runtimeRoot" -y
if ($LASTEXITCODE -ne 0) { throw "Nested 7-Zip extraction failed" }

$assetInfo = Invoke-ClientAssetsUpdate -Root $runtimeRoot
$clientRoot = $assetInfo.ClientRoot

if (![string]::IsNullOrWhiteSpace($LocalLoginConfig)) {
    if (!(Test-Path -LiteralPath $LocalLoginConfig)) { throw "Local login config not found: $LocalLoginConfig" }
    Copy-Item -LiteralPath $LocalLoginConfig -Destination (Join-Path $clientRoot "login.cfg") -Force
    $metadata.LocalLoginConfigApplied = $true
}
else {
    $metadata.LocalLoginConfigApplied = $false
}

$forbiddenOverlayFiles = @(
    "START-OG-FLAT.cmd",
    "START-OG-FLAT.ps1",
    "START-OG-VR.cmd",
    "START-OG-VR.ps1",
    "GET-SWG-SOURCE-CLIENT.ps1",
    "README_BINARY_OOB.txt",
    "og_flat.cfg",
    "og_vr.cfg"
)
$presentForbidden = foreach ($relative in $forbiddenOverlayFiles) {
    $path = Join-Path $clientRoot $relative
    if (Test-Path -LiteralPath $path) { $relative }
}
if ($presentForbidden) {
    throw "Official baseline contains OG overlay files unexpectedly: $($presentForbidden -join ', ')"
}

$metadata.ClientRoot = $clientRoot
$metadata.ClientAssetsRemote = $assetInfo.ClientAssetsRemote
$metadata.ClientAssetsHead = $assetInfo.ClientAssetsHead
$metadata.CompletedUtc = (Get-Date).ToUniversalTime().ToString("o")
$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $manifestRoot "official-baseline-metadata.json") -Encoding ASCII

Write-Output "Writing official baseline manifest"
Write-FileManifest -Root $clientRoot -OutputPath (Join-Path $manifestRoot "official-baseline-files.csv")

if (!$KeepNestedArchive -and (Test-Path -LiteralPath $nestedArchive.FullName)) {
    Remove-Item -LiteralPath $nestedArchive.FullName -Force
}
if ($CleanDownloadsAfterSuccess -and (Test-Path -LiteralPath $downloadRoot)) {
    Remove-Item -LiteralPath $downloadRoot -Recurse -Force
}

if (!$SkipLaunch) {
    $exe = Join-Path $clientRoot "SwgClient_r.exe"
    if (!(Test-Path -LiteralPath $exe)) { throw "Official SwgClient_r.exe missing after client-assets update: $exe" }
    Write-Output "Launching official baseline client from $clientRoot"
    Start-Process -FilePath $exe -WorkingDirectory $clientRoot
}

Write-Output ""
Write-Output "Official baseline ready:"
Write-Output "  $clientRoot"
