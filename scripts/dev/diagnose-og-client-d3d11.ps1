param(
    [string]$ClientToolsRoot = "",
    [string]$ClientDataRoot = "",
    [string]$ProofRoot = "",
    [ValidateSet("D3D9", "D3D11")]
    [string]$Backend = "D3D11",
    [ValidateSet("Win32", "x64")]
    [string]$Platform = "Win32",
    [string]$RunName = "",
    [ValidateSet("None", "LoginToCharacterSelect", "LoginEnterWorld", "ConfigAutoConnect")]
    [string]$SceneDriver = "None",
    [string]$AutoConnectUser = "test",
    [string]$AutoConnectPassword = "test",
    [string]$AutoConnectCluster = "swg",
    [string]$AutoConnectAvatar = "Cyruss",
    [int]$LoginDelaySeconds = 8,
    [int]$CharacterDelaySeconds = 8,
    [int]$WorldDelaySeconds = 15,
    [int]$TimeoutSeconds = 120,
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
    [bool]$PinEnvironmentTime = $true,
    [double]$EnvironmentNormalizedTime = 0.525,
    [switch]$QuickSnap,
    [switch]$BuildFirst,
    [switch]$CleanRuntime,
    [switch]$RendererInventory,
    [switch]$KeepRunning,
    [switch]$EnableVr,
    [double]$VrQuadDistanceMeters = 1.75,
    [double]$VrQuadYawDegrees = 0.0,
    [double]$VrQuadLateralOffsetMeters = 0.0,
    [double]$VrQuadVerticalOffsetMeters = 0.0,
    [double]$VrQuadWidthMeters = 1.65,
    [switch]$DisableVrFirstPerson
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

function Format-InvariantDouble {
    param([double]$Value)
    return [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:0.######}", $Value)
}

function Add-OverrideSetting {
    param(
        [string]$Path,
        [string]$Section,
        [string]$Key,
        [string]$Value
    )

    if (!(Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
        Set-Content -LiteralPath $Path -Value ""
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.AddRange([string[]](Get-Content -LiteralPath $Path))

    $sectionHeader = "[$Section]"
    $sectionIndex = -1
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        if ($lines[$i].Trim().Equals($sectionHeader, [System.StringComparison]::OrdinalIgnoreCase)) {
            $sectionIndex = $i
            break
        }
    }

    if ($sectionIndex -lt 0) {
        if ($lines.Count -gt 0 -and $lines[$lines.Count - 1].Trim().Length -ne 0) {
            $lines.Add("")
        }
        $lines.Add($sectionHeader)
        $lines.Add("$Key=$Value")
        Set-Content -LiteralPath $Path -Value $lines
        return
    }

    $insertIndex = $lines.Count
    $keyIndex = -1
    for ($i = $sectionIndex + 1; $i -lt $lines.Count; ++$i) {
        $trimmed = $lines[$i].Trim()
        if ($trimmed.StartsWith("[") -and $trimmed.EndsWith("]")) {
            $insertIndex = $i
            break
        }
        if ($trimmed -match ("^{0}\s*=" -f [regex]::Escape($Key))) {
            $keyIndex = $i
            break
        }
    }

    if ($keyIndex -ge 0) {
        $lines[$keyIndex] = "$Key=$Value"
    }
    else {
        $lines.Insert($insertIndex, "$Key=$Value")
    }

    Set-Content -LiteralPath $Path -Value $lines
}

function Copy-WithRetry {
    param(
        [string]$Source,
        [string]$Destination,
        [int]$Attempts = 30,
        [int]$DelayMilliseconds = 500
    )

    for ($attempt = 1; $attempt -le $Attempts; ++$attempt) {
        try {
            Copy-Item -LiteralPath $Source -Destination $Destination -Force
            return
        }
        catch {
            if ($attempt -eq $Attempts) {
                throw
            }
            Start-Sleep -Milliseconds $DelayMilliseconds
        }
    }
}

function Add-NativeTypes {
    if ("SwgDiag.NativeMethods" -as [type]) {
        return
    }

    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace SwgDiag
{
    public static class NativeMethods
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct RECT
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct POINT
        {
            public int X;
            public int Y;
        }

        public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

        [DllImport("user32.dll", CharSet=CharSet.Unicode)]
        public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

        [DllImport("user32.dll", CharSet=CharSet.Unicode)]
        public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

        [DllImport("user32.dll")]
        public static extern bool IsWindowVisible(IntPtr hWnd);

        [DllImport("user32.dll")]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);

        [DllImport("user32.dll")]
        public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

        [DllImport("user32.dll")]
        public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);

        [DllImport("user32.dll")]
        public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern IntPtr CreateEvent(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, string lpName);

        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern IntPtr CreateFileMapping(IntPtr hFile, IntPtr lpFileMappingAttributes, uint flProtect, uint dwMaximumSizeHigh, uint dwMaximumSizeLow, string lpName);

        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern IntPtr MapViewOfFile(IntPtr hFileMappingObject, uint dwDesiredAccess, uint dwFileOffsetHigh, uint dwFileOffsetLow, UIntPtr dwNumberOfBytesToMap);

        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern bool UnmapViewOfFile(IntPtr lpBaseAddress);

        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern bool SetEvent(IntPtr hEvent);

        [DllImport("user32.dll")]
        public static extern bool SetProcessDPIAware();
    }
}
"@

    try {
        [void][SwgDiag.NativeMethods]::SetProcessDPIAware()
    }
    catch {
    }
}

function Get-ProcessWindows {
    param([int]$ProcessId)

    Add-NativeTypes
    $windows = New-Object System.Collections.ArrayList
    $callback = [SwgDiag.NativeMethods+EnumWindowsProc] {
        param([IntPtr]$hWnd, [IntPtr]$lParam)

        [uint32]$windowProcessId = 0
        [void][SwgDiag.NativeMethods]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($windowProcessId -eq [uint32]$ProcessId) {
            $title = [System.Text.StringBuilder]::new(512)
            $class = [System.Text.StringBuilder]::new(256)
            [void][SwgDiag.NativeMethods]::GetWindowText($hWnd, $title, $title.Capacity)
            [void][SwgDiag.NativeMethods]::GetClassName($hWnd, $class, $class.Capacity)
            [void]$windows.Add([pscustomobject]@{
                Handle = ("0x{0:X}" -f $hWnd.ToInt64())
                Visible = [SwgDiag.NativeMethods]::IsWindowVisible($hWnd)
                Class = $class.ToString()
                Title = $title.ToString()
            })
        }
        return $true
    }

    [void][SwgDiag.NativeMethods]::EnumWindows($callback, [IntPtr]::Zero)
    return @($windows.ToArray())
}

function Save-WindowScreenshot {
    param(
        [string]$HandleText,
        [string]$OutputPath,
        [switch]$ClientArea
    )

    Add-NativeTypes

    $handleValue = 0
    if ($HandleText.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        $handleValue = [Convert]::ToInt64($HandleText.Substring(2), 16)
    }
    else {
        $handleValue = [Convert]::ToInt64($HandleText, 10)
    }

    $handle = [IntPtr]::new($handleValue)
    $hwndTopmost = [IntPtr]::new(-1)
    $hwndNotTopmost = [IntPtr]::new(-2)
    $swRestore = 9
    $swpNoMove = 0x0002
    $swpNoSize = 0x0001
    $swpShowWindow = 0x0040

    [void][SwgDiag.NativeMethods]::ShowWindowAsync($handle, $swRestore)
    [void][SwgDiag.NativeMethods]::SetWindowPos($handle, $hwndTopmost, 0, 0, 0, 0, $swpNoMove -bor $swpNoSize -bor $swpShowWindow)
    [void][SwgDiag.NativeMethods]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds 350

    $rect = New-Object SwgDiag.NativeMethods+RECT
    $captureX = 0
    $captureY = 0
    $printFlags = 2
    if ($ClientArea) {
        if (![SwgDiag.NativeMethods]::GetClientRect($handle, [ref]$rect)) {
            return $false
        }

        $point = New-Object SwgDiag.NativeMethods+POINT
        $point.X = 0
        $point.Y = 0
        if (![SwgDiag.NativeMethods]::ClientToScreen($handle, [ref]$point)) {
            return $false
        }

        $captureX = $point.X
        $captureY = $point.Y
        $printFlags = 1
    }
    else {
        if (![SwgDiag.NativeMethods]::GetWindowRect($handle, [ref]$rect)) {
            return $false
        }

        $captureX = $rect.Left
        $captureY = $rect.Top
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        return $false
    }

    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $hdc = $graphics.GetHdc()
        $printed = $false
        try {
            $printed = [SwgDiag.NativeMethods]::PrintWindow($handle, $hdc, $printFlags)
        }
        finally {
            $graphics.ReleaseHdc($hdc)
        }

        if (!$printed) {
            $graphics.CopyFromScreen($captureX, $captureY, 0, 0, [System.Drawing.Size]::new($width, $height))
        }
        $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
        return $true
    }
    finally {
        [void][SwgDiag.NativeMethods]::SetWindowPos($handle, $hwndNotTopmost, 0, 0, 0, 0, $swpNoMove -bor $swpNoSize)
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Invoke-GameScreenshot {
    param(
        [string]$HandleText,
        [string]$ScreenshotDirectory,
        [string]$OutputBase
    )

    Add-NativeTypes

    $handleValue = 0
    if ($HandleText.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        $handleValue = [Convert]::ToInt64($HandleText.Substring(2), 16)
    }
    else {
        $handleValue = [Convert]::ToInt64($HandleText, 10)
    }

    $handle = [IntPtr]::new($handleValue)
    $hwndTopmost = [IntPtr]::new(-1)
    $hwndNotTopmost = [IntPtr]::new(-2)
    $swRestore = 9
    $swpNoMove = 0x0002
    $swpNoSize = 0x0001
    $swpShowWindow = 0x0040
    $vkSnapshot = 0x2c
    $scanPrintScreen = 0x37
    $keyEventKeyUp = 0x0002
    $keyEventScanCode = 0x0008

    $before = Get-Date
    [void][SwgDiag.NativeMethods]::ShowWindowAsync($handle, $swRestore)
    [void][SwgDiag.NativeMethods]::SetWindowPos($handle, $hwndTopmost, 0, 0, 0, 0, $swpNoMove -bor $swpNoSize -bor $swpShowWindow)
    [void][SwgDiag.NativeMethods]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds 350

    [SwgDiag.NativeMethods]::keybd_event([byte]$vkSnapshot, [byte]$scanPrintScreen, $keyEventScanCode, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [SwgDiag.NativeMethods]::keybd_event([byte]$vkSnapshot, [byte]$scanPrintScreen, $keyEventScanCode -bor $keyEventKeyUp, [UIntPtr]::Zero)

    $created = $null
    for ($attempt = 0; $attempt -lt 40 -and !$created; ++$attempt) {
        Start-Sleep -Milliseconds 250
        if (Test-Path -LiteralPath $ScreenshotDirectory) {
            $created = Get-ChildItem -LiteralPath $ScreenshotDirectory -File |
                Where-Object { $_.Name -match '^screenShot[0-9]+\.(bmp|jpg|tga)$' -and $_.LastWriteTime -ge $before.AddSeconds(-2) } |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1
        }
    }

    [void][SwgDiag.NativeMethods]::SetWindowPos($handle, $hwndNotTopmost, 0, 0, 0, 0, $swpNoMove -bor $swpNoSize)

    if (!$created) {
        return $null
    }

    $destination = "$OutputBase$($created.Extension.ToLowerInvariant())"
    Copy-Item -LiteralPath $created.FullName -Destination $destination -Force
    return $destination
}

function Receive-DbwinForProcess {
    param(
        [int]$ProcessId,
        [datetime]$Until,
        [string]$OutputPath,
        [int]$StopWhenProcessId = 0
    )

    Add-NativeTypes

    $pageReadWrite = 0x04
    $fileMapRead = 0x0004
    $waitObject0 = 0
    $waitTimeout = 0x00000102
    $invalidHandle = [IntPtr]::new(-1)

    $bufferReady = [SwgDiag.NativeMethods]::CreateEvent([IntPtr]::Zero, $false, $false, "DBWIN_BUFFER_READY")
    $dataReady = [SwgDiag.NativeMethods]::CreateEvent([IntPtr]::Zero, $false, $false, "DBWIN_DATA_READY")
    $mapping = [SwgDiag.NativeMethods]::CreateFileMapping($invalidHandle, [IntPtr]::Zero, $pageReadWrite, 0, 4096, "DBWIN_BUFFER")
    $view = [SwgDiag.NativeMethods]::MapViewOfFile($mapping, $fileMapRead, 0, 0, [UIntPtr]::new(4096))

    if ($bufferReady -eq [IntPtr]::Zero -or $dataReady -eq [IntPtr]::Zero -or $mapping -eq [IntPtr]::Zero -or $view -eq [IntPtr]::Zero) {
        "DBWIN listener failed to initialize. Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" | Set-Content -LiteralPath $OutputPath
        return
    }

    try {
        [void][SwgDiag.NativeMethods]::SetEvent($bufferReady)
        while ((Get-Date) -lt $Until) {
            if ($StopWhenProcessId -gt 0 -and !(Get-Process -Id $StopWhenProcessId -ErrorAction SilentlyContinue)) {
                Add-Content -LiteralPath $OutputPath -Value "DBWIN quick snap stopping after scene driver exit"
                break
            }

            $wait = [SwgDiag.NativeMethods]::WaitForSingleObject($dataReady, 250)
            if ($wait -eq $waitObject0) {
                $debugProcessId = [Runtime.InteropServices.Marshal]::ReadInt32($view)
                $message = [Runtime.InteropServices.Marshal]::PtrToStringAnsi([IntPtr]::Add($view, 4))
                if ($debugProcessId -eq $ProcessId -and $message) {
                    Add-Content -LiteralPath $OutputPath -Value $message
                }
                [void][SwgDiag.NativeMethods]::SetEvent($bufferReady)
            }
            elseif ($wait -ne $waitTimeout) {
                Add-Content -LiteralPath $OutputPath -Value "DBWIN wait returned $wait"
            }
        }
    }
    finally {
        if ($view -ne [IntPtr]::Zero) { [void][SwgDiag.NativeMethods]::UnmapViewOfFile($view) }
        if ($mapping -ne [IntPtr]::Zero) { [void][SwgDiag.NativeMethods]::CloseHandle($mapping) }
        if ($dataReady -ne [IntPtr]::Zero) { [void][SwgDiag.NativeMethods]::CloseHandle($dataReady) }
        if ($bufferReady -ne [IntPtr]::Zero) { [void][SwgDiag.NativeMethods]::CloseHandle($bufferReady) }
    }
}

function Test-UiOrLoadingShader {
    param([string]$Shader)

    if (!$Shader) {
        return $true
    }

    return $Shader -match "^shader/(uicanvas|ui_|2d_|font|text)"
}

function Test-InGameWorldShader {
    param([string]$Shader)

    if (!$Shader) {
        return $false
    }

    return $Shader -match "^shader/(terrain_|skybox|stars|sun_|cloudtile|cels_moon|wter_|pt_|stco_|tato_|tatt_|thm_|mun_|radl_|glss_|metl_|all_|yavin_|ins_)"
}

function Get-RendererInventoryState {
    param(
        [string]$Backend,
        [string[]]$Lines
    )

    $backendLower = $Backend.ToLowerInvariant()
    $drawRows = 0
    $uiRows = 0
    $worldRows = 0
    $inGameWorldRows = 0
    $frames = New-Object System.Collections.Generic.SortedSet[int]
    $worldFrames = New-Object System.Collections.Generic.SortedSet[int]
    $inGameWorldFrames = New-Object System.Collections.Generic.SortedSet[int]
    $shaderCounts = @{}

    foreach ($line in $Lines) {
        if ($line -notmatch "renderer inventory $backendLower") {
            continue
        }

        $drawRows += 1
        $frame = $null
        $shader = ""
        if ($line -match "frame=([0-9]+)") {
            $frame = [int]$Matches[1]
            [void]$frames.Add($frame)
        }
        if ($line -match "shader=([^ ]+)") {
            $shader = $Matches[1]
        }

        if (!$shaderCounts.ContainsKey($shader)) {
            $shaderCounts[$shader] = 0
        }
        $shaderCounts[$shader] += 1

        if (Test-UiOrLoadingShader $shader) {
            $uiRows += 1
        }
        else {
            $worldRows += 1
            if ($null -ne $frame) {
                [void]$worldFrames.Add($frame)
            }
        }

        if (Test-InGameWorldShader $shader) {
            $inGameWorldRows += 1
            if ($null -ne $frame) {
                [void]$inGameWorldFrames.Add($frame)
            }
        }
    }

    $topShaders = @($shaderCounts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 12 | ForEach-Object {
        [pscustomobject]@{
            Shader = $_.Key
            Count = $_.Value
        }
    })

    return [pscustomobject]@{
        DrawRows = $drawRows
        UiOrLoadingRows = $uiRows
        WorldRows = $worldRows
        InGameWorldRows = $inGameWorldRows
        FrameCount = $frames.Count
        FirstFrame = if ($frames.Count -gt 0) { @($frames)[0] } else { $null }
        LastFrame = if ($frames.Count -gt 0) { @($frames)[$frames.Count - 1] } else { $null }
        WorldFrameCount = $worldFrames.Count
        FirstWorldFrame = if ($worldFrames.Count -gt 0) { @($worldFrames)[0] } else { $null }
        LastWorldFrame = if ($worldFrames.Count -gt 0) { @($worldFrames)[$worldFrames.Count - 1] } else { $null }
        InGameWorldFrameCount = $inGameWorldFrames.Count
        FirstInGameWorldFrame = if ($inGameWorldFrames.Count -gt 0) { @($inGameWorldFrames)[0] } else { $null }
        LastInGameWorldFrame = if ($inGameWorldFrames.Count -gt 0) { @($inGameWorldFrames)[$inGameWorldFrames.Count - 1] } else { $null }
        HasWorldRows = $worldRows -gt 0
        HasInGameWorldRows = $inGameWorldRows -gt 0
        TopShaders = $topShaders
    }
}

function Get-FogSourceState {
    param(
        [string]$Backend,
        [string[]]$Lines
    )

    $backendLower = $Backend.ToLowerInvariant()
    $fogRows = @($Lines | Where-Object { $_ -match "fog set $backendLower" })
    $enabledRows = @($fogRows | Where-Object { $_ -match "enabled=1" })
    $colors = @($fogRows | ForEach-Object {
        if ($_ -match "color=([^ ]+)") {
            $Matches[1]
        }
    } | Where-Object { $_ })
    $enabledColors = @($enabledRows | ForEach-Object {
        if ($_ -match "color=([^ ]+)") {
            $Matches[1]
        }
    } | Where-Object { $_ })

    return [pscustomobject]@{
        LoggedRows = $fogRows.Count
        EnabledRows = $enabledRows.Count
        UniqueColors = @($colors | Sort-Object -Unique)
        UniqueEnabledColors = @($enabledColors | Sort-Object -Unique)
        FirstRows = @($fogRows | Select-Object -First 8)
        LastRows = @($fogRows | Select-Object -Last 8)
    }
}

New-Item -ItemType Directory -Force -Path $ProofRoot | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backendLower = $Backend.ToLowerInvariant()
$runFolderName = if ($RunName.Length -gt 0) { $RunName } else { "og-$backendLower-diagnosis-$stamp" }
$runRoot = Join-Path $ProofRoot $runFolderName
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$summaryPath = Join-Path $runRoot "summary.json"
$dbwinPath = Join-Path $runRoot "dbwin.txt"
$windowsPath = Join-Path $runRoot "windows.json"
$platformLower = $Platform.ToLowerInvariant()
$modulesPath = Join-Path $runRoot ("modules-{0}.txt" -f $platformLower)
$tasklistPath = Join-Path $runRoot "tasklist-modules.txt"
$screenshotPath = Join-Path $runRoot "window.png"
$clientScreenshotPath = Join-Path $runRoot "client.png"
$gameScreenshotBase = Join-Path $runRoot "renderer-screenshot"
$backbufferCaptureBase = Join-Path $runRoot "$backendLower-backbuffer"
$backbufferCapturePath = "$backbufferCaptureBase.bmp"
$sceneDriverSummaryPath = Join-Path $runRoot "scene-driver.json"
$vrProofPath = Join-Path $runRoot "openxr-vr-bridge.jsonl"

Write-Step "Proof bundle: $runRoot"

if ($BuildFirst) {
    if ($Backend -ne "D3D11") {
        throw "-BuildFirst is only supported for the D3D11 backend"
    }

    Write-Step "Building Direct3d11 raster DLL"
    & (Join-Path $PSScriptRoot "build-og-client.ps1") -Target Direct3d11 -Platform $Platform -Configuration Release -LogPath (Join-Path $runRoot "build-direct3d11.log")
    if ($LASTEXITCODE -ne 0) { throw "Direct3d11 build failed with exit code $LASTEXITCODE" }
}

if ($CleanRuntime) {
    Write-Step "Cleaning OG runtime config"
    & (Join-Path $PSScriptRoot "clean-og-client-runtime.ps1") -ClientDataRoot $ClientDataRoot -ScreenWidth $ScreenWidth -ScreenHeight $ScreenHeight |
        Tee-Object -FilePath (Join-Path $runRoot "clean-runtime.txt")
}

$exePath = Join-Path $ClientDataRoot "SwgClient_r.exe"
$compilePlatform = if ($Platform -eq "x64") { "x64" } else { "win32" }
$builtExePath = Join-Path $ClientToolsRoot "src\compile\$compilePlatform\SwgClient\Release\SwgClient_r.exe"
$builtDpvsPath = Join-Path $ClientToolsRoot "src\compile\$compilePlatform\dpvs\Release\dpvs.dll"
$builtDllExportPath = Join-Path $ClientToolsRoot "src\compile\$compilePlatform\DllExport\Release\DllExport.dll"
$builtD3d11DllPath = Join-Path $ClientToolsRoot "src\compile\$compilePlatform\Direct3d11\Release\gl05_r.dll"
$builtD3d9DllPath = Join-Path $ClientToolsRoot "src\compile\$compilePlatform\Direct3d9\Release\gl05_r.dll"
$x64MilesRedistRoot = Join-Path $ClientToolsRoot "src\external\3rd\library\miles-7.2e\redist\win64"
$runtimeDllPath = Join-Path $ClientDataRoot "gl05_r.dll"
$legacyBackupPath = Join-Path $ClientDataRoot "gl05_r.dll.d3d9-backup"
$overridePath = Join-Path $ClientDataRoot "misc\override.cfg"
$sourceDllPath = if ($Backend -eq "D3D11") {
    $builtD3d11DllPath
}
elseif (Test-Path -LiteralPath $builtD3d9DllPath) {
    $builtD3d9DllPath
}
else {
    $legacyBackupPath
}

if (!(Test-Path -LiteralPath $builtExePath)) { throw "Missing built OG client exe: $builtExePath" }
if (!(Test-Path -LiteralPath $builtDpvsPath)) { throw "Missing built dpvs.dll: $builtDpvsPath" }
if (!(Test-Path -LiteralPath $builtDllExportPath)) { throw "Missing built DllExport.dll: $builtDllExportPath" }
if (!(Test-Path -LiteralPath $sourceDllPath)) { throw "Missing $Backend raster DLL source: $sourceDllPath" }

Copy-WithRetry -Source $builtExePath -Destination $exePath
Copy-WithRetry -Source $builtDpvsPath -Destination (Join-Path $ClientDataRoot "dpvs.dll")
Copy-WithRetry -Source $builtDllExportPath -Destination (Join-Path $ClientDataRoot "DllExport.dll")
Write-Step "Installed OG client exe and support DLLs into runtime"

if ($Platform -eq "x64") {
    $x64MilesDll = Join-Path $x64MilesRedistRoot "mss64.dll"
    $x64MilesPlugins = Join-Path $x64MilesRedistRoot "miles"
    $runtimeMilesPlugins = Join-Path $ClientDataRoot "miles"
    if ((Test-Path -LiteralPath $x64MilesDll) -and (Test-Path -LiteralPath $x64MilesPlugins)) {
        Copy-WithRetry -Source $x64MilesDll -Destination (Join-Path $ClientDataRoot "mss64.dll")
        New-Item -ItemType Directory -Force -Path $runtimeMilesPlugins | Out-Null
        Copy-Item -Path (Join-Path $x64MilesPlugins "*") -Destination $runtimeMilesPlugins -Force
        Write-Step "Installed x64 Miles runtime into runtime"
    }
    else {
        Write-Step "x64 Miles runtime not found in source tree; continuing with runtime-supplied files or no-op audio fallback"
    }
}

if ((Test-Path -LiteralPath $runtimeDllPath) -and !(Test-Path -LiteralPath $legacyBackupPath)) {
    Copy-WithRetry -Source $runtimeDllPath -Destination $legacyBackupPath
    Write-Step "Backed up runtime gl05_r.dll to $legacyBackupPath"
}

Copy-WithRetry -Source $sourceDllPath -Destination $runtimeDllPath
Write-Step "Installed $Backend gl05_r.dll into runtime"

Add-OverrideSetting -Path $overridePath -Section "ClientGraphics" -Key "windowed" -Value "true"
Add-OverrideSetting -Path $overridePath -Section "ClientGraphics" -Key "borderlessWindow" -Value "false"
Add-OverrideSetting -Path $overridePath -Section "ClientGraphics" -Key "rasterMajor" -Value "5"
Add-OverrideSetting -Path $overridePath -Section "ClientGraphics" -Key "screenWidth" -Value $ScreenWidth
Add-OverrideSetting -Path $overridePath -Section "ClientGraphics" -Key "screenHeight" -Value $ScreenHeight
Add-OverrideSetting -Path $overridePath -Section "ClientGraphics" -Key "screenShotFormat" -Value "1"
Add-OverrideSetting -Path $overridePath -Section "ClientGraphics" -Key "screenShotQuality" -Value "100"
Add-OverrideSetting -Path $overridePath -Section "ClientAudio" -Key "enabled" -Value "true"
Add-OverrideSetting -Path $overridePath -Section "ClientAudio" -Key "masterVolume" -Value "1.0"
Add-OverrideSetting -Path $overridePath -Section "ClientAudio" -Key "soundEffectVolume" -Value "1.0"
Add-OverrideSetting -Path $overridePath -Section "ClientAudio" -Key "backGroundMusicVolume" -Value "1.0"
Add-OverrideSetting -Path $overridePath -Section "ClientAudio" -Key "playerMusicVolume" -Value "1.0"
Add-OverrideSetting -Path $overridePath -Section "ClientAudio" -Key "userInterfaceVolume" -Value "1.0"
Add-OverrideSetting -Path $overridePath -Section "ClientAudio" -Key "ambientEffectVolume" -Value "1.0"
Add-OverrideSetting -Path $overridePath -Section "ClientAudio" -Key "soundProvider" -Value '"5.1 Speakers"'
Add-OverrideSetting -Path $overridePath -Section "Direct3d9" -Key "screenShotBackBuffer" -Value "true"
Add-OverrideSetting -Path $overridePath -Section "Direct3d11" -Key "disableVertexAndPixelShaders" -Value "true"
Add-OverrideSetting -Path $overridePath -Section "Direct3d11" -Key "shaderCapabilityOverride" -Value "0"
if ($PinEnvironmentTime) {
    Add-OverrideSetting -Path $overridePath -Section "ClientTerrain" -Key "disableTimeOfDay" -Value "true"
    Add-OverrideSetting -Path $overridePath -Section "ClientTerrain" -Key "useNormalizedTime" -Value "true"
    Add-OverrideSetting -Path $overridePath -Section "ClientTerrain" -Key "environmentNormalizedStartTime" -Value ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:0.000000}", $EnvironmentNormalizedTime))
}
if ($SceneDriver -eq "ConfigAutoConnect") {
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "loginServerAddress" -Value "127.0.0.1"
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "loginServerAddress0" -Value "127.0.0.1"
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "loginServerPort" -Value "44453"
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "loginServerPort0" -Value "44453"
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "loginClientID" -Value $AutoConnectUser
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "loginClientPassword" -Value $AutoConnectPassword
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "autoConnectToLoginServer" -Value "true"
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "centralServerName" -Value $AutoConnectCluster
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "autoConnectToCentralServer" -Value "true"
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "avatarName" -Value $AutoConnectAvatar
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "autoConnectToGameServer" -Value "true"
}
else {
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "autoConnectToLoginServer" -Value "false"
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "autoConnectToCentralServer" -Value "false"
    Add-OverrideSetting -Path $overridePath -Section "ClientGame" -Key "autoConnectToGameServer" -Value "false"
}
Add-OverrideSetting -Path $overridePath -Section "SharedDebug/InstallTimer" -Key "enabled" -Value "true"
Write-Step "Runtime override.cfg prepared for $Backend diagnosis"

$dllHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeDllPath).Hash
$process = $null
$sceneDriverProcess = $null
$start = Get-Date
$until = $start.AddSeconds($TimeoutSeconds)

Push-Location -LiteralPath $ClientDataRoot
try {
    Write-Step "Launching SwgClient_r.exe"
    $previousD3D9Autocapture = $env:SWG_D3D9_AUTOCAPTURE
    $previousD3D9AutocapturePresent = $env:SWG_D3D9_AUTOCAPTURE_PRESENT
    $previousD3D11Autocapture = $env:SWG_D3D11_AUTOCAPTURE
    $previousD3D11AutocapturePresent = $env:SWG_D3D11_AUTOCAPTURE_PRESENT
    $previousRendererAutocaptureMax = $env:SWG_RENDERER_AUTOCAPTURE_MAX
    $previousRendererAutocaptureInterval = $env:SWG_RENDERER_AUTOCAPTURE_INTERVAL
    $previousRendererAutocaptureWorldRows = $env:SWG_RENDERER_AUTOCAPTURE_WORLD_ROWS
    $previousRendererInventory = $env:SWG_RENDERER_INVENTORY
    $previousRendererInventoryLimit = $env:SWG_RENDERER_INVENTORY_LIMIT
    $previousRendererInventoryStartFrame = $env:SWG_RENDERER_INVENTORY_START_FRAME
    $previousD3D11Diagnostics = $env:SWG_D3D11_DIAGNOSTICS
    $previousOgVr = $env:SWG_OG_VR
    $previousD3D11Vr = $env:SWG_D3D11_VR
    $previousOgVrProof = $env:SWG_OG_VR_PROOF
    $previousVrQuadDistance = $env:SWG_OG_VR_QUAD_DISTANCE_METERS
    $previousVrQuadYaw = $env:SWG_OG_VR_QUAD_YAW_DEGREES
    $previousVrQuadLateral = $env:SWG_OG_VR_QUAD_LATERAL_OFFSET_METERS
    $previousVrQuadVertical = $env:SWG_OG_VR_QUAD_VERTICAL_OFFSET_METERS
    $previousVrQuadWidth = $env:SWG_OG_VR_QUAD_WIDTH_METERS
    $previousVrForceFirstPerson = $env:SWG_OG_VR_FORCE_FIRST_PERSON
    if ($Backend -eq "D3D9") {
        $env:SWG_D3D9_AUTOCAPTURE = $backbufferCaptureBase
        $env:SWG_D3D9_AUTOCAPTURE_PRESENT = "$D3D11AutocapturePresent"
    }
    elseif ($Backend -eq "D3D11") {
        $env:SWG_D3D11_AUTOCAPTURE = $backbufferCaptureBase
        $env:SWG_D3D11_AUTOCAPTURE_PRESENT = "$D3D11AutocapturePresent"
        $env:SWG_D3D11_DIAGNOSTICS = "1"
    }
    if ($RendererInventory) {
        $env:SWG_RENDERER_INVENTORY = "1"
        $env:SWG_RENDERER_INVENTORY_LIMIT = "$RendererInventoryLimit"
        $env:SWG_RENDERER_INVENTORY_START_FRAME = "$RendererInventoryStartFrame"
        $env:SWG_RENDERER_AUTOCAPTURE_MAX = "$RendererAutocaptureMax"
        $env:SWG_RENDERER_AUTOCAPTURE_INTERVAL = "$RendererAutocaptureInterval"
        $env:SWG_RENDERER_AUTOCAPTURE_WORLD_ROWS = "$RendererAutocaptureWorldRows"
    }
    if ($EnableVr) {
        $env:SWG_OG_VR = "1"
        $env:SWG_D3D11_VR = "1"
        $env:SWG_OG_VR_PROOF = $vrProofPath
        $env:SWG_OG_VR_QUAD_DISTANCE_METERS = Format-InvariantDouble $VrQuadDistanceMeters
        $env:SWG_OG_VR_QUAD_YAW_DEGREES = Format-InvariantDouble $VrQuadYawDegrees
        $env:SWG_OG_VR_QUAD_LATERAL_OFFSET_METERS = Format-InvariantDouble $VrQuadLateralOffsetMeters
        $env:SWG_OG_VR_QUAD_VERTICAL_OFFSET_METERS = Format-InvariantDouble $VrQuadVerticalOffsetMeters
        $env:SWG_OG_VR_QUAD_WIDTH_METERS = Format-InvariantDouble $VrQuadWidthMeters
        $env:SWG_OG_VR_FORCE_FIRST_PERSON = if ($DisableVrFirstPerson) { "0" } else { "1" }
    }
    $process = Start-Process -FilePath ".\SwgClient_r.exe" -PassThru
    if ($SceneDriver -eq "LoginToCharacterSelect" -or $SceneDriver -eq "LoginEnterWorld" -or $SceneDriver -eq "ConfigAutoConnect") {
        $sceneDriverScript = Join-Path $PSScriptRoot "drive-og-client-scene.ps1"
        if (!(Test-Path -LiteralPath $sceneDriverScript)) {
            throw "Missing scene driver script: $sceneDriverScript"
        }

        $sceneDriverArgs = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", $sceneDriverScript,
            "-ProcessId", "$($process.Id)",
            "-ProofDir", $runRoot,
            "-SceneDriver", $SceneDriver,
            "-LoginUser", $AutoConnectUser,
            "-LoginPassword", $AutoConnectPassword,
            "-LoginDelaySeconds", "$LoginDelaySeconds",
            "-CharacterDelaySeconds", "$CharacterDelaySeconds",
            "-WorldDelaySeconds", "$WorldDelaySeconds",
            "-WorldCaptureCount", "$WorldCaptureCount",
            "-WorldCaptureIntervalSeconds", "$WorldCaptureIntervalSeconds"
        )
        if ($QuickSnap) {
            $sceneDriverArgs += "-QuickSnap"
        }
        $sceneDriverProcess = Start-Process -FilePath "powershell.exe" -ArgumentList $sceneDriverArgs -WindowStyle Hidden -PassThru
        Write-Step "Started scene driver $SceneDriver pid=$($sceneDriverProcess.Id)"
    }
}
finally {
    if ($null -eq $previousD3D9Autocapture) {
        Remove-Item Env:SWG_D3D9_AUTOCAPTURE -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_D3D9_AUTOCAPTURE = $previousD3D9Autocapture
    }
    if ($null -eq $previousD3D9AutocapturePresent) {
        Remove-Item Env:SWG_D3D9_AUTOCAPTURE_PRESENT -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_D3D9_AUTOCAPTURE_PRESENT = $previousD3D9AutocapturePresent
    }
    if ($null -eq $previousD3D11Autocapture) {
        Remove-Item Env:SWG_D3D11_AUTOCAPTURE -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_D3D11_AUTOCAPTURE = $previousD3D11Autocapture
    }
    if ($null -eq $previousD3D11AutocapturePresent) {
        Remove-Item Env:SWG_D3D11_AUTOCAPTURE_PRESENT -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_D3D11_AUTOCAPTURE_PRESENT = $previousD3D11AutocapturePresent
    }
    if ($null -eq $previousRendererAutocaptureMax) {
        Remove-Item Env:SWG_RENDERER_AUTOCAPTURE_MAX -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_RENDERER_AUTOCAPTURE_MAX = $previousRendererAutocaptureMax
    }
    if ($null -eq $previousRendererAutocaptureInterval) {
        Remove-Item Env:SWG_RENDERER_AUTOCAPTURE_INTERVAL -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_RENDERER_AUTOCAPTURE_INTERVAL = $previousRendererAutocaptureInterval
    }
    if ($null -eq $previousRendererAutocaptureWorldRows) {
        Remove-Item Env:SWG_RENDERER_AUTOCAPTURE_WORLD_ROWS -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_RENDERER_AUTOCAPTURE_WORLD_ROWS = $previousRendererAutocaptureWorldRows
    }
    if ($null -eq $previousRendererInventory) {
        Remove-Item Env:SWG_RENDERER_INVENTORY -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_RENDERER_INVENTORY = $previousRendererInventory
    }
    if ($null -eq $previousRendererInventoryLimit) {
        Remove-Item Env:SWG_RENDERER_INVENTORY_LIMIT -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_RENDERER_INVENTORY_LIMIT = $previousRendererInventoryLimit
    }
    if ($null -eq $previousRendererInventoryStartFrame) {
        Remove-Item Env:SWG_RENDERER_INVENTORY_START_FRAME -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_RENDERER_INVENTORY_START_FRAME = $previousRendererInventoryStartFrame
    }
    if ($null -eq $previousD3D11Diagnostics) {
        Remove-Item Env:SWG_D3D11_DIAGNOSTICS -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_D3D11_DIAGNOSTICS = $previousD3D11Diagnostics
    }
    if ($null -eq $previousOgVr) {
        Remove-Item Env:SWG_OG_VR -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_OG_VR = $previousOgVr
    }
    if ($null -eq $previousD3D11Vr) {
        Remove-Item Env:SWG_D3D11_VR -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_D3D11_VR = $previousD3D11Vr
    }
    if ($null -eq $previousOgVrProof) {
        Remove-Item Env:SWG_OG_VR_PROOF -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_OG_VR_PROOF = $previousOgVrProof
    }
    if ($null -eq $previousVrQuadDistance) {
        Remove-Item Env:SWG_OG_VR_QUAD_DISTANCE_METERS -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_OG_VR_QUAD_DISTANCE_METERS = $previousVrQuadDistance
    }
    if ($null -eq $previousVrQuadYaw) {
        Remove-Item Env:SWG_OG_VR_QUAD_YAW_DEGREES -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_OG_VR_QUAD_YAW_DEGREES = $previousVrQuadYaw
    }
    if ($null -eq $previousVrQuadLateral) {
        Remove-Item Env:SWG_OG_VR_QUAD_LATERAL_OFFSET_METERS -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_OG_VR_QUAD_LATERAL_OFFSET_METERS = $previousVrQuadLateral
    }
    if ($null -eq $previousVrQuadVertical) {
        Remove-Item Env:SWG_OG_VR_QUAD_VERTICAL_OFFSET_METERS -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_OG_VR_QUAD_VERTICAL_OFFSET_METERS = $previousVrQuadVertical
    }
    if ($null -eq $previousVrQuadWidth) {
        Remove-Item Env:SWG_OG_VR_QUAD_WIDTH_METERS -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_OG_VR_QUAD_WIDTH_METERS = $previousVrQuadWidth
    }
    if ($null -eq $previousVrForceFirstPerson) {
        Remove-Item Env:SWG_OG_VR_FORCE_FIRST_PERSON -ErrorAction SilentlyContinue
    }
    else {
        $env:SWG_OG_VR_FORCE_FIRST_PERSON = $previousVrForceFirstPerson
    }
    Pop-Location
}

"PID=$($process.Id)" | Set-Content -LiteralPath $dbwinPath
$stopWhenProcessId = if ($QuickSnap -and $sceneDriverProcess) { $sceneDriverProcess.Id } else { 0 }
Receive-DbwinForProcess -ProcessId $process.Id -Until $until -OutputPath $dbwinPath -StopWhenProcessId $stopWhenProcessId

if ($sceneDriverProcess) {
    try {
        $sceneDriverWaitSeconds = if ($QuickSnap) { 2 } else { 10 }
        Wait-Process -Id $sceneDriverProcess.Id -Timeout $sceneDriverWaitSeconds -ErrorAction SilentlyContinue
    }
    catch {
    }
}

$exited = $false
try {
    $process.Refresh()
    $exited = $process.HasExited
}
catch {
    $exited = $true
}

$windows = if (!$exited) { Get-ProcessWindows -ProcessId $process.Id } else { @() }
$windows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $windowsPath
$visibleMainWindow = @($windows | Where-Object { $_.Visible -and $_.Title -eq "Star Wars Galaxies" } | Select-Object -First 1)
$screenshotSaved = $false
$clientScreenshotSaved = $false
$gameScreenshotPath = $null
if ($visibleMainWindow.Count -gt 0) {
    $screenshotSaved = Save-WindowScreenshot -HandleText $visibleMainWindow[0].Handle -OutputPath $screenshotPath
    $clientScreenshotSaved = Save-WindowScreenshot -HandleText $visibleMainWindow[0].Handle -OutputPath $clientScreenshotPath -ClientArea
    $gameScreenshotPath = Invoke-GameScreenshot -HandleText $visibleMainWindow[0].Handle -ScreenshotDirectory (Join-Path $ClientDataRoot "screenshots") -OutputBase $gameScreenshotBase
}

if (!$exited) {
    try {
        $process.Refresh()
        $exited = $process.HasExited
    }
    catch {
        $exited = $true
    }
}

if (!$exited) {
    $modulePowerShell = if ($Platform -eq "Win32") {
        Join-Path $env:WINDIR "SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
    }
    else {
        Join-Path $env:WINDIR "System32\WindowsPowerShell\v1.0\powershell.exe"
    }
    if (Test-Path -LiteralPath $modulePowerShell) {
        & $modulePowerShell -NoProfile -ExecutionPolicy Bypass -Command "try { (Get-Process -Id $($process.Id)).Modules | Sort-Object ModuleName | ForEach-Object { '{0} | {1}' -f `$_.ModuleName, `$_.FileName } } catch { `$_.Exception.Message }" |
            Set-Content -LiteralPath $modulesPath
    }

    & tasklist.exe /fi "PID eq $($process.Id)" /m |
        Set-Content -LiteralPath $tasklistPath
}

if (!$KeepRunning -and !$exited) {
    Write-Step "Stopping client after diagnosis window"
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    try {
        Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    catch {
    }
}

$dbwinLines = if (Test-Path -LiteralPath $dbwinPath) { @(Get-Content -LiteralPath $dbwinPath) } else { @() }
$moduleLines = if (Test-Path -LiteralPath $modulesPath) { @(Get-Content -LiteralPath $modulesPath) } else { @() }
$tasklistLines = if (Test-Path -LiteralPath $tasklistPath) { @(Get-Content -LiteralPath $tasklistPath) } else { @() }
$glLoadedBy32BitModules = @($moduleLines | Where-Object { $_ -match "(^|\\|\\s)gl05_r\.dll" }).Count -gt 0
$glLoadedByTasklist = @($tasklistLines | Where-Object { $_ -match "gl05_r\.dll" }).Count -gt 0
$setupClientGraphicsSeen = @($dbwinLines | Where-Object { $_ -match "SetupClientGraphics::install|Graphics::install" }).Count -gt 0
$setupClientGameSeen = @($dbwinLines | Where-Object { $_ -match "SetupClientGame::install|ConfigClientGame::install|SetupSwgClientUserInterface|Cui" }).Count -gt 0
$rendererInventoryState = Get-RendererInventoryState -Backend $Backend -Lines $dbwinLines
$fogSourceState = Get-FogSourceState -Backend $Backend -Lines $dbwinLines

$summary = [pscustomobject]@{
    StartedAt = $start.ToString("o")
    Backend = $Backend
    TimeoutSeconds = $TimeoutSeconds
    ClientDataRoot = (Resolve-Path -LiteralPath $ClientDataRoot).Path
    RuntimeDll = $runtimeDllPath
    SourceDll = $sourceDllPath
    RuntimeDllSha256 = $dllHash
    ProcessId = $process.Id
    ExitedBeforeTimeout = $exited
    Platform = $Platform
    Gl05LoadedByModules = $glLoadedBy32BitModules
    Gl05LoadedByTasklist = $glLoadedByTasklist
    SetupClientGraphicsSeenInDebug = $setupClientGraphicsSeen
    LaterClientGameOrUiSeenInDebug = $setupClientGameSeen
    WindowCount = @($windows).Count
    VisibleWindowCount = @($windows | Where-Object { $_.Visible }).Count
    ScreenshotSaved = $screenshotSaved
    ScreenshotPath = if ($screenshotSaved) { $screenshotPath } else { $null }
    ClientScreenshotSaved = $clientScreenshotSaved
    ClientScreenshotPath = if ($clientScreenshotSaved) { $clientScreenshotPath } else { $null }
    GameScreenshotSaved = $null -ne $gameScreenshotPath
    GameScreenshotPath = $gameScreenshotPath
    SceneDriver = $SceneDriver
    SceneDriverSummaryPath = if (Test-Path -LiteralPath $sceneDriverSummaryPath) { $sceneDriverSummaryPath } else { $null }
    BackbufferCaptureSaved = Test-Path -LiteralPath $backbufferCapturePath
    BackbufferCapturePath = if (Test-Path -LiteralPath $backbufferCapturePath) { $backbufferCapturePath } else { $null }
    BackbufferCaptureSeries = @(Get-ChildItem -LiteralPath $runRoot -Filter "$backendLower-backbuffer-present*.bmp" -File -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { $_.FullName })
    RendererInventory = [bool]$RendererInventory
    RendererAutocaptureMax = $RendererAutocaptureMax
    RendererAutocaptureInterval = $RendererAutocaptureInterval
    RendererAutocaptureWorldRows = $RendererAutocaptureWorldRows
    WorldCaptureCount = $WorldCaptureCount
    WorldCaptureIntervalSeconds = $WorldCaptureIntervalSeconds
    RendererInventoryLimit = $RendererInventoryLimit
    RendererInventoryStartFrame = $RendererInventoryStartFrame
    VrEnabled = [bool]$EnableVr
    VrProofPath = if ($EnableVr) { $vrProofPath } else { $null }
    VrQuadDistanceMeters = $VrQuadDistanceMeters
    VrQuadYawDegrees = $VrQuadYawDegrees
    VrQuadLateralOffsetMeters = $VrQuadLateralOffsetMeters
    VrQuadVerticalOffsetMeters = $VrQuadVerticalOffsetMeters
    VrQuadWidthMeters = $VrQuadWidthMeters
    VrFirstPersonForced = [bool]($EnableVr -and !$DisableVrFirstPerson)
    PinEnvironmentTime = $PinEnvironmentTime
    EnvironmentNormalizedTime = $EnvironmentNormalizedTime
    RendererInventoryState = $rendererInventoryState
    FogSourceState = $fogSourceState
    LastDebugLines = @($dbwinLines | Select-Object -Last 20 | ForEach-Object { $_.ToString() })
    ProofRoot = $runRoot
}

$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath
$summary | Format-List
Write-Step "Diagnosis complete"
