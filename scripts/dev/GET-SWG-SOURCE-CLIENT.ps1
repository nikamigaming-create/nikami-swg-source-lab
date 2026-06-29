param(
    [string]$DestinationRoot = "",
    [string]$DownloadRoot = "",
    [switch]$SkipDownload,
    [switch]$KeepDownloads,
    [switch]$KeepNestedArchive
)

$ErrorActionPreference = "Stop"

$OverlayRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceHelper = Join-Path $OverlayRoot "Resolve-NikamiWorkspace.ps1"
if (Test-Path -LiteralPath $workspaceHelper) {
    . $workspaceHelper
    $ClientToolsRoot = (Resolve-Path (Join-Path $OverlayRoot "..\..")).Path
    $WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
}
if ([string]::IsNullOrWhiteSpace($DestinationRoot)) {
    if ($WorkspaceRoot) {
        $DestinationRoot = Join-Path $WorkspaceRoot "runtime\client\SWGSource Client v3.0"
    }
    else {
        $DestinationRoot = Join-Path $OverlayRoot "SWGSource Client v3.0 - OG VR"
    }
}
if ([string]::IsNullOrWhiteSpace($DownloadRoot)) {
    if ($WorkspaceRoot) {
        $DownloadRoot = Join-Path $WorkspaceRoot "downloads\official-swgsource-client-v3.0"
    }
    else {
        $DownloadRoot = Join-Path $OverlayRoot "_swgsource_downloads"
    }
}

$parts = @(
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

function Find-7Zip {
    $commands = @("7z.exe", "7za.exe", "7zz.exe")
    foreach ($command in $commands) {
        $found = Get-Command $command -ErrorAction SilentlyContinue
        if ($found) {
            return $found.Source
        }
    }

    $paths = @(
        "C:\Program Files\7-Zip\7z.exe",
        "C:\Program Files (x86)\7-Zip\7z.exe"
    )
    foreach ($path in $paths) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    throw "7-Zip was not found. Install 7-Zip, then rerun this script."
}

function Copy-OverlayFile {
    param([string]$RelativePath, [string]$ClientRoot)
    $source = Join-Path $OverlayRoot $RelativePath
    if (!(Test-Path -LiteralPath $source)) {
        throw "Overlay file missing: $source"
    }
    $target = Join-Path $ClientRoot $RelativePath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}

function Invoke-ClientAssetsUpdate {
    param([string]$Root)

    $updateBat = Get-ChildItem -LiteralPath $Root -Recurse -Filter "UpdateSwgClient.bat" |
        Where-Object { !$_.PSIsContainer } |
        Select-Object -First 1
    if (!$updateBat) {
        return $null
    }

    $assetRoot = Split-Path -Parent $updateBat.FullName
    $git = Join-Path $assetRoot "PortableGit\bin\git.exe"
    if (!(Test-Path -LiteralPath $git)) {
        $gitCommand = Get-Command "git.exe" -ErrorAction SilentlyContinue
        if (!$gitCommand) {
            throw "Could not find PortableGit or git.exe to fetch SWG-Source/client-assets"
        }
        $git = $gitCommand.Source
    }

    Write-Host "Fetching SWG-Source/client-assets into $assetRoot"
    Push-Location -LiteralPath $assetRoot
    try {
        if (Test-Path -LiteralPath ".git") {
            & $git pull
            if ($LASTEXITCODE -ne 0) {
                throw "git pull failed in $assetRoot"
            }
        }
        else {
            & $git init .
            if ($LASTEXITCODE -ne 0) { throw "git init failed in $assetRoot" }

            & $git config pull.rebase false
            if ($LASTEXITCODE -ne 0) { throw "git config failed in $assetRoot" }

            & $git remote add -f origin https://github.com/SWG-Source/client-assets.git
            if ($LASTEXITCODE -ne 0) { throw "git remote add/fetch failed in $assetRoot" }

            & $git checkout master
            if ($LASTEXITCODE -ne 0) { throw "git checkout master failed in $assetRoot" }
        }
    }
    finally {
        Pop-Location
    }

    return $assetRoot
}

function Download-File {
    param(
        [string]$Url,
        [string]$Target,
        [int64]$ExpectedSize
    )

    $partial = "$Target.part"
    Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue

    $curl = Get-Command "curl.exe" -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source --location --fail --retry 5 --retry-delay 5 --output $partial $Url
        if ($LASTEXITCODE -ne 0) {
            throw "curl download failed for $Url"
        }
    }
    else {
        $request = [System.Net.HttpWebRequest]::Create($Url)
        $request.AllowAutoRedirect = $true
        $response = $request.GetResponse()
        try {
            $inputStream = $response.GetResponseStream()
            $outputStream = [System.IO.File]::Create($partial)
            try {
                $buffer = New-Object byte[] (1024 * 1024)
                while (($read = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $outputStream.Write($buffer, 0, $read)
                }
            }
            finally {
                $outputStream.Dispose()
                $inputStream.Dispose()
            }
        }
        finally {
            $response.Dispose()
        }
    }

    $actualSize = (Get-Item -LiteralPath $partial).Length
    if ($actualSize -ne $ExpectedSize) {
        throw "Downloaded size mismatch for $(Split-Path -Leaf $Target): expected $ExpectedSize, got $actualSize"
    }

    Move-Item -LiteralPath $partial -Destination $Target -Force
}

New-Item -ItemType Directory -Force -Path $DownloadRoot | Out-Null

if (!$SkipDownload) {
    foreach ($part in $parts) {
        $target = Join-Path $DownloadRoot $part.Name
        if ((Test-Path -LiteralPath $target) -and ((Get-Item -LiteralPath $target).Length -eq $part.Size)) {
            Write-Output "Already downloaded: $($part.Name)"
            continue
        }

        Write-Output "Downloading $($part.Name)"
        Download-File -Url $part.Url -Target $target -ExpectedSize $part.Size
    }
}

$sevenZip = Find-7Zip
New-Item -ItemType Directory -Force -Path $DestinationRoot | Out-Null
$expandedNestedArchive = $null

$firstPart = Join-Path $DownloadRoot $parts[0].Name
if (!(Test-Path -LiteralPath $firstPart)) {
    throw "First split archive part not found: $firstPart"
}

Write-Output "Extracting official SWG Source Client v3.0 to $DestinationRoot"
& $sevenZip x $firstPart "-o$DestinationRoot" -y
if ($LASTEXITCODE -ne 0) {
    throw "7-Zip extraction failed"
}

$clientRoot = Get-ChildItem -LiteralPath $DestinationRoot -Recurse -Filter "SwgClient_r.exe" |
    Where-Object { !$_.PSIsContainer } |
    Select-Object -First 1 |
    ForEach-Object { Split-Path -Parent $_.FullName }

if (!$clientRoot) {
    $nestedArchive = Get-ChildItem -LiteralPath $DestinationRoot -Recurse -Filter "*.7z" |
        Where-Object { !$_.PSIsContainer } |
        Select-Object -First 1

    if ($nestedArchive) {
        Write-Output "Extracting nested client archive $($nestedArchive.FullName)"
        $expandedNestedArchive = $nestedArchive.FullName
        & $sevenZip x $nestedArchive.FullName "-o$DestinationRoot" -y
        if ($LASTEXITCODE -ne 0) {
            throw "Nested 7-Zip extraction failed"
        }

        $clientRoot = Get-ChildItem -LiteralPath $DestinationRoot -Recurse -Filter "SwgClient_r.exe" |
            Where-Object { !$_.PSIsContainer } |
            Select-Object -First 1 |
            ForEach-Object { Split-Path -Parent $_.FullName }
    }
}

if (!$clientRoot) {
    $updatedRoot = @(Invoke-ClientAssetsUpdate -Root $DestinationRoot) |
        Where-Object { ($_ -is [string]) -and (Test-Path -LiteralPath $_ -PathType Container) } |
        Select-Object -Last 1
    if ($updatedRoot) {
        $clientRoot = Get-ChildItem -LiteralPath $updatedRoot -Filter "SwgClient_r.exe" |
            Where-Object { !$_.PSIsContainer } |
            Select-Object -First 1 |
            ForEach-Object { Split-Path -Parent $_.FullName }
    }
}

if (!$clientRoot) {
    throw "Could not find SwgClient_r.exe after extraction under $DestinationRoot"
}

Write-Output "Applying OG VR/flat overlay to $clientRoot"
$overlayFiles = @(
    "SwgClient_r.exe",
    "gl05_r.dll",
    "DllExport.dll",
    "dpvs.dll",
    "START-OG-FLAT.cmd",
    "START-OG-FLAT.ps1",
    "START-OG-VR.cmd",
    "START-OG-VR.ps1",
    "README_BINARY_OOB.txt"
)

foreach ($file in $overlayFiles) {
    Copy-OverlayFile -RelativePath $file -ClientRoot $clientRoot
}

if (!$KeepNestedArchive -and $expandedNestedArchive -and (Test-Path -LiteralPath $expandedNestedArchive)) {
    Write-Output "Cleaning nested official archive: $expandedNestedArchive"
    Remove-Item -LiteralPath $expandedNestedArchive -Force
}

if (!$KeepDownloads -and !$SkipDownload -and (Test-Path -LiteralPath $DownloadRoot)) {
    Write-Output "Cleaning official split downloads: $DownloadRoot"
    Remove-Item -LiteralPath $DownloadRoot -Recurse -Force
}

Write-Output ""
Write-Output "Ready:"
Write-Output "  $clientRoot"
Write-Output "Run START-OG-FLAT.cmd or START-OG-VR.cmd from that folder."
