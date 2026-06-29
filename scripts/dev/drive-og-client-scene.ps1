param(
    [Parameter(Mandatory=$true)]
    [int]$ProcessId,
    [Parameter(Mandatory=$true)]
    [string]$ProofDir,
    [ValidateSet("None", "LoginToCharacterSelect", "LoginEnterWorld", "ConfigAutoConnect", "ConfigAutoConnectActionChain", "ConfigAutoConnectEnterChain", "ConfigAutoConnectStateMachine")]
    [string]$SceneDriver = "LoginEnterWorld",
    [string]$LoginUser = "test",
    [string]$LoginPassword = "test",
    [int]$LoginDelaySeconds = 8,
    [int]$CharacterDelaySeconds = 8,
    [int]$GalaxyDelaySeconds = 8,
    [int]$WorldDelaySeconds = 15,
    [int]$WorldCaptureCount = 1,
    [int]$WorldCaptureIntervalSeconds = 10,
    [int]$StateMachineMaxActions = 4,
    [switch]$QuickSnap
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $ProofDir | Out-Null
$summaryPath = Join-Path $ProofDir "scene-driver.json"

function Add-NativeTypes {
    if ("SwgSceneDriver.NativeMethods" -as [type]) {
        try {
            [void][SwgSceneDriver.NativeMethods]::SetProcessDPIAware()
        }
        catch {
        }
        return
    }

    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace SwgSceneDriver
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

        public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

        [DllImport("user32.dll", CharSet=CharSet.Unicode)]
        public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

        [DllImport("user32.dll")]
        public static extern bool IsWindowVisible(IntPtr hWnd);

        [DllImport("user32.dll")]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

        [StructLayout(LayoutKind.Sequential)]
        public struct POINT
        {
            public int X;
            public int Y;
        }

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
        public static extern bool SetCursorPos(int X, int Y);

        [DllImport("user32.dll")]
        public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);

        [DllImport("user32.dll", CharSet=CharSet.Unicode)]
        public static extern bool PostMessage(IntPtr hWnd, uint Msg, UIntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern bool SetProcessDPIAware();
    }
}
"@

    try {
        [void][SwgSceneDriver.NativeMethods]::SetProcessDPIAware()
    }
    catch {
    }
}

function Get-ClientWindowHandle {
    param([int]$TargetProcessId)

    Add-NativeTypes
    $script:foundHandle = [IntPtr]::Zero
    $callback = [SwgSceneDriver.NativeMethods+EnumWindowsProc] {
        param([IntPtr]$hWnd, [IntPtr]$lParam)

        [uint32]$windowProcessId = 0
        [void][SwgSceneDriver.NativeMethods]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($windowProcessId -eq [uint32]$TargetProcessId) {
            $title = [System.Text.StringBuilder]::new(512)
            [void][SwgSceneDriver.NativeMethods]::GetWindowText($hWnd, $title, $title.Capacity)
            if ($title.ToString() -eq "Star Wars Galaxies" -and [SwgSceneDriver.NativeMethods]::IsWindowVisible($hWnd)) {
                $script:foundHandle = $hWnd
                return $false
            }
        }
        return $true
    }

    [void][SwgSceneDriver.NativeMethods]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:foundHandle
}

function Focus-ClientWindow {
    param([IntPtr]$Handle)

    $hwndTopmost = [IntPtr]::new(-1)
    $hwndNotTopmost = [IntPtr]::new(-2)
    $swRestore = 9
    $swpNoMove = 0x0002
    $swpNoSize = 0x0001
    $swpShowWindow = 0x0040

    [void][SwgSceneDriver.NativeMethods]::ShowWindowAsync($Handle, $swRestore)
    [void][SwgSceneDriver.NativeMethods]::SetWindowPos($Handle, $hwndTopmost, 0, 0, 0, 0, $swpNoMove -bor $swpNoSize -bor $swpShowWindow)
    [void][SwgSceneDriver.NativeMethods]::SetForegroundWindow($Handle)
    Start-Sleep -Milliseconds 350
    [void][SwgSceneDriver.NativeMethods]::SetWindowPos($Handle, $hwndNotTopmost, 0, 0, 0, 0, $swpNoMove -bor $swpNoSize)
}

function Save-WindowScreenshot {
    param(
        [IntPtr]$Handle,
        [string]$Path
    )

    Add-Type -AssemblyName System.Drawing
    $rect = New-Object SwgSceneDriver.NativeMethods+RECT
    if (![SwgSceneDriver.NativeMethods]::GetWindowRect($Handle, [ref]$rect)) {
        return $false
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        return $false
    }

    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $hdc = $graphics.GetHdc()
        try {
            [void][SwgSceneDriver.NativeMethods]::PrintWindow($Handle, $hdc, 2)
        }
        finally {
            $graphics.ReleaseHdc($hdc)
        }

        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
        return $true
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Test-LikelyWorldScreenshot {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        return $false
    }

    Add-Type -AssemblyName System.Drawing
    $bitmap = $null
    try {
        $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
        $stepX = [Math]::Max(1, [int]($bitmap.Width / 80))
        $stepY = [Math]::Max(1, [int]($bitmap.Height / 60))
        [double]$sumR = 0
        [double]$sumG = 0
        [double]$sumB = 0
        [double]$bright = 0
        [double]$samples = 0

        for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
            for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sumR += $pixel.R
                $sumG += $pixel.G
                $sumB += $pixel.B
                if ((0.299 * $pixel.R + 0.587 * $pixel.G + 0.114 * $pixel.B) -gt 55) {
                    $bright += 1
                }
                $samples += 1
            }
        }

        if ($samples -le 0) {
            return $false
        }

        $meanR = $sumR / $samples
        $meanG = $sumG / $samples
        $meanB = $sumB / $samples
        $brightFraction = $bright / $samples
        $blueLoadingBias = ($meanB -gt ($meanR * 1.20)) -and ($meanB -gt ($meanG * 1.08))
        $blankWhiteBias = ($brightFraction -gt 0.92) -and ($meanR -gt 225) -and ($meanG -gt 225) -and ($meanB -gt 225)

        return ($brightFraction -gt 0.20) -and !$blueLoadingBias -and !$blankWhiteBias -and ($meanR -gt 45) -and ($meanG -gt 40)
    }
    catch {
        return $false
    }
    finally {
        if ($bitmap) {
            $bitmap.Dispose()
        }
    }
}

function Get-ScreenshotAnalysis {
    param([string]$Path)

    $analysis = [ordered]@{
        Path = $Path
        Exists = $false
        Width = 0
        Height = 0
        Sha256 = $null
        MeanR = 0.0
        MeanG = 0.0
        MeanB = 0.0
        BrightFraction = 0.0
        CenterTealFraction = 0.0
        LeftWhiteFraction = 0.0
        RightPanelTealFraction = 0.0
        BottomButtonTealFraction = 0.0
        LikelyWorld = $false
        StateLabel = "missing"
    }

    if (!(Test-Path -LiteralPath $Path)) {
        return [pscustomobject]$analysis
    }

    Add-Type -AssemblyName System.Drawing
    $bitmap = $null
    try {
        $analysis.Exists = $true
        $analysis.Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
        $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
        $analysis.Width = $bitmap.Width
        $analysis.Height = $bitmap.Height

        $stepX = [Math]::Max(1, [int]($bitmap.Width / 96))
        $stepY = [Math]::Max(1, [int]($bitmap.Height / 72))
        [double]$sumR = 0
        [double]$sumG = 0
        [double]$sumB = 0
        [double]$bright = 0
        [double]$centerTeal = 0
        [double]$centerSamples = 0
        [double]$leftWhite = 0
        [double]$leftSamples = 0
        [double]$rightPanelTeal = 0
        [double]$rightPanelSamples = 0
        [double]$bottomButtonTeal = 0
        [double]$bottomButtonSamples = 0
        [double]$samples = 0

        $centerLeft = [int]($bitmap.Width * 0.25)
        $centerRight = [int]($bitmap.Width * 0.75)
        $centerTop = [int]($bitmap.Height * 0.22)
        $centerBottom = [int]($bitmap.Height * 0.68)
        $leftRight = [int]($bitmap.Width * 0.42)
        $leftTop = [int]($bitmap.Height * 0.10)
        $leftBottom = [int]($bitmap.Height * 0.88)
        $rightPanelLeft = [int]($bitmap.Width * 0.48)
        $rightPanelRight = [int]($bitmap.Width * 0.99)
        $rightPanelTop = [int]($bitmap.Height * 0.06)
        $rightPanelBottom = [int]($bitmap.Height * 0.55)
        $bottomButtonLeft = [int]($bitmap.Width * 0.88)
        $bottomButtonRight = [int]($bitmap.Width * 0.99)
        $bottomButtonTop = [int]($bitmap.Height * 0.91)
        $bottomButtonBottom = [int]($bitmap.Height * 0.995)

        for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
            for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
                $pixel = $bitmap.GetPixel($x, $y)
                $luma = 0.299 * $pixel.R + 0.587 * $pixel.G + 0.114 * $pixel.B
                $sumR += $pixel.R
                $sumG += $pixel.G
                $sumB += $pixel.B
                if ($luma -gt 55) {
                    $bright += 1
                }
                if ($x -ge $centerLeft -and $x -le $centerRight -and $y -ge $centerTop -and $y -le $centerBottom) {
                    $centerSamples += 1
                    if ($pixel.G -ge 55 -and $pixel.B -ge 55 -and $pixel.G -gt ($pixel.R * 1.20) -and $pixel.B -gt ($pixel.R * 1.15)) {
                        $centerTeal += 1
                    }
                }
                if ($x -le $leftRight -and $y -ge $leftTop -and $y -le $leftBottom) {
                    $leftSamples += 1
                    if ($pixel.R -gt 150 -and $pixel.G -gt 150 -and $pixel.B -gt 150) {
                        $leftWhite += 1
                    }
                }
                if ($x -ge $rightPanelLeft -and $x -le $rightPanelRight -and $y -ge $rightPanelTop -and $y -le $rightPanelBottom) {
                    $rightPanelSamples += 1
                    if ($pixel.G -ge 65 -and $pixel.B -ge 65 -and $pixel.G -gt ($pixel.R * 1.35) -and $pixel.B -gt ($pixel.R * 1.20)) {
                        $rightPanelTeal += 1
                    }
                }
                if ($x -ge $bottomButtonLeft -and $x -le $bottomButtonRight -and $y -ge $bottomButtonTop -and $y -le $bottomButtonBottom) {
                    $bottomButtonSamples += 1
                    if ($pixel.G -ge 45 -and $pixel.B -ge 45 -and $pixel.G -gt ($pixel.R * 1.15) -and $pixel.B -gt ($pixel.R * 1.05)) {
                        $bottomButtonTeal += 1
                    }
                }
                $samples += 1
            }
        }

        if ($samples -gt 0) {
            $analysis.MeanR = [Math]::Round($sumR / $samples, 3)
            $analysis.MeanG = [Math]::Round($sumG / $samples, 3)
            $analysis.MeanB = [Math]::Round($sumB / $samples, 3)
            $analysis.BrightFraction = [Math]::Round($bright / $samples, 5)
        }
        if ($centerSamples -gt 0) {
            $analysis.CenterTealFraction = [Math]::Round($centerTeal / $centerSamples, 5)
        }
        if ($leftSamples -gt 0) {
            $analysis.LeftWhiteFraction = [Math]::Round($leftWhite / $leftSamples, 5)
        }
        if ($rightPanelSamples -gt 0) {
            $analysis.RightPanelTealFraction = [Math]::Round($rightPanelTeal / $rightPanelSamples, 5)
        }
        if ($bottomButtonSamples -gt 0) {
            $analysis.BottomButtonTealFraction = [Math]::Round($bottomButtonTeal / $bottomButtonSamples, 5)
        }

        $analysis.LikelyWorld = Test-LikelyWorldScreenshot -Path $Path
        $characterPanelLike = (
            $analysis.RightPanelTealFraction -gt 0.20 -and
            $analysis.BottomButtonTealFraction -gt 0.04 -and
            $analysis.BrightFraction -gt 0.18 -and
            $analysis.BrightFraction -lt 0.75
        )
        if ($analysis.LikelyWorld) {
            $analysis.StateLabel = "world-like"
        }
        elseif ($analysis.BrightFraction -gt 0.95) {
            $analysis.StateLabel = "startup-blank-like"
        }
        elseif ($characterPanelLike) {
            $analysis.StateLabel = "character-selection-like"
        }
        elseif ($analysis.BrightFraction -gt 0.24 -and $analysis.MeanB -gt ($analysis.MeanR * 1.85) -and $analysis.MeanG -gt ($analysis.MeanR * 1.45)) {
            $analysis.StateLabel = "loading-screen-like"
        }
        elseif ($analysis.CenterTealFraction -gt 0.42 -and $analysis.BrightFraction -lt 0.34) {
            $analysis.StateLabel = "galaxy-selection-like"
        }
        elseif ($analysis.LeftWhiteFraction -gt 0.025 -and $analysis.BrightFraction -gt 0.08) {
            $analysis.StateLabel = "character-selection-like"
        }
        elseif ($analysis.CenterTealFraction -gt 0.12) {
            $analysis.StateLabel = "login-or-menu-like"
        }
        else {
            $analysis.StateLabel = "unknown"
        }

        return [pscustomobject]$analysis
    }
    catch {
        $analysis.StateLabel = "analysis-error"
        $analysis.Error = $_.Exception.Message
        return [pscustomobject]$analysis
    }
    finally {
        if ($bitmap) {
            $bitmap.Dispose()
        }
    }
}

function Click-ClientPoint {
    param(
        [IntPtr]$Handle,
        [int]$X,
        [int]$Y
    )

    $clientRect = New-Object SwgSceneDriver.NativeMethods+RECT
    if (![SwgSceneDriver.NativeMethods]::GetClientRect($Handle, [ref]$clientRect)) {
        return $false
    }

    $clientWidth = [Math]::Max(1, $clientRect.Right - $clientRect.Left)
    $clientHeight = [Math]::Max(1, $clientRect.Bottom - $clientRect.Top)
    $clampedX = [Math]::Max(1, [Math]::Min($clientWidth - 1, $X))
    $clampedY = [Math]::Max(1, [Math]::Min($clientHeight - 1, $Y))
    $point = New-Object SwgSceneDriver.NativeMethods+POINT
    $point.X = $clampedX
    $point.Y = $clampedY
    if (![SwgSceneDriver.NativeMethods]::ClientToScreen($Handle, [ref]$point)) {
        return $false
    }

    $mouseEventLeftDown = 0x0002
    $mouseEventLeftUp = 0x0004
    [void][SwgSceneDriver.NativeMethods]::SetCursorPos($point.X, $point.Y)
    Start-Sleep -Milliseconds 100
    [SwgSceneDriver.NativeMethods]::mouse_event($mouseEventLeftDown, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [SwgSceneDriver.NativeMethods]::mouse_event($mouseEventLeftUp, 0, 0, 0, [UIntPtr]::Zero)

    $wmMouseMove = 0x0200
    $wmLButtonDown = 0x0201
    $wmLButtonUp = 0x0202
    $mkLButton = [UIntPtr]::new(0x0001)
    $noButton = [UIntPtr]::Zero
    $messageMaxX = [Math]::Max($clientWidth - 1, 1919)
    $messageMaxY = [Math]::Max($clientHeight - 1, 1079)
    $messageX = [Math]::Max(1, [Math]::Min($messageMaxX, $X))
    $messageY = [Math]::Max(1, [Math]::Min($messageMaxY, $Y))
    $packedPoint = [IntPtr]::new((($messageY -band 0xffff) -shl 16) -bor ($messageX -band 0xffff))
    [void][SwgSceneDriver.NativeMethods]::PostMessage($Handle, $wmMouseMove, $noButton, $packedPoint)
    Start-Sleep -Milliseconds 20
    $downOk = [SwgSceneDriver.NativeMethods]::PostMessage($Handle, $wmLButtonDown, $mkLButton, $packedPoint)
    Start-Sleep -Milliseconds 80
    $upOk = [SwgSceneDriver.NativeMethods]::PostMessage($Handle, $wmLButtonUp, $noButton, $packedPoint)
    return ($downOk -and $upOk)
}

function Send-ClientKey {
    param(
        [IntPtr]$Handle,
        [int]$VirtualKey
    )

    $wmKeyDown = 0x0100
    $wmKeyUp = 0x0101
    $keyParam = [UIntPtr]::new([uint32]$VirtualKey)
    $scanParam = [IntPtr]::new(1)
    $upParam = [IntPtr]::new(0xC0000001)
    $downOk = [SwgSceneDriver.NativeMethods]::PostMessage($Handle, $wmKeyDown, $keyParam, $scanParam)
    Start-Sleep -Milliseconds 80
    $upOk = [SwgSceneDriver.NativeMethods]::PostMessage($Handle, $wmKeyUp, $keyParam, $upParam)
    return ($downOk -and $upOk)
}

function Get-ClientSize {
    param([IntPtr]$Handle)

    $rect = New-Object SwgSceneDriver.NativeMethods+RECT
    if (-not [SwgSceneDriver.NativeMethods]::GetClientRect($Handle, [ref]$rect)) {
        return $null
    }

    return [pscustomobject]@{
        Width = [Math]::Max(1, $rect.Right - $rect.Left)
        Height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    }
}

function Click-ClientBottomRightLogin {
    param([IntPtr]$Handle)

    $size = Get-ClientSize -Handle $Handle
    if ($null -eq $size) {
        return $false
    }

    return (Click-ClientPoint -Handle $Handle -X ([Math]::Max(1, $size.Width - 95)) -Y ([Math]::Max(1, $size.Height - 35)))
}

function Click-ClientLoginUsername {
    param([IntPtr]$Handle)

    $rect = New-Object SwgSceneDriver.NativeMethods+RECT
    if (-not [SwgSceneDriver.NativeMethods]::GetClientRect($Handle, [ref]$rect)) {
        return $false
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    return (Click-ClientPoint -Handle $Handle -X ([Math]::Max(1, [int]($width * 0.50))) -Y ([Math]::Max(1, [int]($height * 0.44))))
}

function Wait-ClientWindow {
    param(
        [int]$TargetProcessId,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $handle = Get-ClientWindowHandle -TargetProcessId $TargetProcessId
        if ($handle -ne [IntPtr]::Zero) {
            return $handle
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)

    return [IntPtr]::Zero
}

$events = New-Object System.Collections.ArrayList
$startedAt = Get-Date
$wshell = New-Object -ComObject Wscript.Shell

function Capture-StateScreenshot {
    param(
        [IntPtr]$Handle,
        [string]$Name,
        [string]$EventName
    )

    Focus-ClientWindow -Handle $Handle
    $path = Join-Path $ProofDir $Name
    [void](Save-WindowScreenshot -Handle $Handle -Path $path)
    $analysis = Get-ScreenshotAnalysis -Path $path
    [void]$events.Add([pscustomobject]@{
        Time = (Get-Date).ToString("o")
        Event = $EventName
        Path = $path
        Analysis = $analysis
    })
    return $analysis
}

function Invoke-BottomRightAction {
    param(
        [IntPtr]$Handle,
        [string]$EventName,
        [string]$Target = "bottom-right-next"
    )

    Focus-ClientWindow -Handle $Handle
    $size = Get-ClientSize -Handle $Handle
    $x = if ($null -ne $size) { [Math]::Max(1, $size.Width - 95) } else { -1 }
    $y = if ($null -ne $size) { [Math]::Max(1, $size.Height - 35) } else { -1 }
    $ok = if ($null -ne $size) { Click-ClientPoint -Handle $Handle -X $x -Y $y } else { $false }
    [void]$events.Add([pscustomobject]@{
        Time = (Get-Date).ToString("o")
        Event = $EventName
        Target = $Target
        ClientX = $x
        ClientY = $y
        ClientWidth = if ($null -ne $size) { $size.Width } else { 0 }
        ClientHeight = if ($null -ne $size) { $size.Height } else { 0 }
        Submitted = $ok
    })
    return $ok
}

function Invoke-LoginSubmit {
    param(
        [IntPtr]$Handle,
        [string]$EventPrefix = "submit-login"
    )

    Focus-ClientWindow -Handle $Handle
    [void](Click-ClientLoginUsername -Handle $Handle)
    Start-Sleep -Milliseconds 100
    $wshell.SendKeys("^a")
    Start-Sleep -Milliseconds 100
    $wshell.SendKeys($LoginUser)
    Start-Sleep -Milliseconds 100
    $wshell.SendKeys("{TAB}")
    Start-Sleep -Milliseconds 100
    $wshell.SendKeys("^a")
    Start-Sleep -Milliseconds 100
    $wshell.SendKeys($LoginPassword)
    Start-Sleep -Milliseconds 100
    $ok = Click-ClientBottomRightLogin -Handle $Handle
    [void]$events.Add([pscustomobject]@{
        Time = (Get-Date).ToString("o")
        Event = "$EventPrefix-click"
        Target = "bottom-right-login"
        Submitted = $ok
    })
    return $ok
}

function Invoke-CharacterLoginSubmit {
    param(
        [IntPtr]$Handle,
        [string]$EventPrefix = "submit-character"
    )

    Focus-ClientWindow -Handle $Handle
    $size = Get-ClientSize -Handle $Handle
    if ($null -eq $size) {
        [void]$events.Add([pscustomobject]@{
            Time = (Get-Date).ToString("o")
            Event = "$EventPrefix-no-client-size"
            Submitted = $false
        })
        return $false
    }

    $rowX = [Math]::Max(1, [int]($size.Width * 0.73))
    $rowY = [Math]::Max(1, [int]($size.Height * 0.105))
    [void](Click-ClientPoint -Handle $Handle -X $rowX -Y $rowY)
    Start-Sleep -Milliseconds 120
    [void](Click-ClientPoint -Handle $Handle -X $rowX -Y $rowY)
    [void]$events.Add([pscustomobject]@{
        Time = (Get-Date).ToString("o")
        Event = "$EventPrefix-select-row"
        ClientX = $rowX
        ClientY = $rowY
        ClientWidth = $size.Width
        ClientHeight = $size.Height
        Submitted = $true
    })
    Start-Sleep -Milliseconds 300

    Focus-ClientWindow -Handle $Handle
    $enterOk = Send-ClientKey -Handle $Handle -VirtualKey 0x0D
    [void]$events.Add([pscustomobject]@{
        Time = (Get-Date).ToString("o")
        Event = "$EventPrefix-enter-window-message"
        Key = "ENTER"
        Submitted = $enterOk
    })
    Start-Sleep -Seconds 2
    $enterProbeName = "{0}-after-enter-window-message.png" -f $EventPrefix
    $enterProbeAnalysis = Capture-StateScreenshot -Handle $Handle -Name $enterProbeName -EventName "$EventPrefix-after-enter-window-message-probe"
    if ($enterProbeAnalysis.StateLabel -ne "character-selection-like") {
        [void]$events.Add([pscustomobject]@{
            Time = (Get-Date).ToString("o")
            Event = "$EventPrefix-enter-window-message-accepted"
            NextStateLabel = $enterProbeAnalysis.StateLabel
            Sha256 = $enterProbeAnalysis.Sha256
        })
        return $enterOk
    }

    $attempts = @(
        @{ Name = "bottom-right-login-physical-center"; X = [int]($size.Width - 92); Y = [int]($size.Height - 18) },
        @{ Name = "bottom-right-login-physical-text";   X = [int]($size.Width - 86); Y = [int]($size.Height - 28) },
        @{ Name = "bottom-right-login-virtual-center"; X = 1838; Y = 1054 },
        @{ Name = "bottom-right-login-virtual-text";   X = 1848; Y = 1050 },
        @{ Name = "bottom-right-login-virtual-left";   X = 1795; Y = 1050 },
        @{ Name = "bottom-right-login-virtual-right";  X = 1890; Y = 1050 },
        @{ Name = "bottom-right-login-visible-edge"; X = $size.Width - 8;   Y = $size.Height - 88 },
        @{ Name = "bottom-right-login-visible-edge-mid"; X = $size.Width - 20; Y = $size.Height - 78 },
        @{ Name = "bottom-right-login-visible-high"; X = $size.Width - 45;  Y = $size.Height - 88 },
        @{ Name = "bottom-right-login-visible-mid";  X = $size.Width - 80;  Y = $size.Height - 78 },
        @{ Name = "bottom-right-login-visible-left"; X = $size.Width - 130; Y = $size.Height - 78 },
        @{ Name = "bottom-right-login-low-right";    X = $size.Width - 32;  Y = $size.Height - 18 },
        @{ Name = "bottom-right-login-low-center";   X = $size.Width - 72;  Y = $size.Height - 18 },
        @{ Name = "bottom-right-login-low-left";     X = $size.Width - 118; Y = $size.Height - 18 },
        @{ Name = "bottom-right-login-lower-right";  X = $size.Width - 38;  Y = $size.Height - 28 },
        @{ Name = "bottom-right-login-lower-center"; X = $size.Width - 82;  Y = $size.Height - 28 },
        @{ Name = "bottom-right-login-lower-left";   X = $size.Width - 128; Y = $size.Height - 28 },
        @{ Name = "bottom-right-login-primary"; X = $size.Width - 95;  Y = $size.Height - 35 },
        @{ Name = "bottom-right-login-higher";  X = $size.Width - 95;  Y = $size.Height - 62 },
        @{ Name = "bottom-right-login-right";   X = $size.Width - 45;  Y = $size.Height - 62 },
        @{ Name = "bottom-right-login-center";  X = $size.Width - 58;  Y = $size.Height - 48 },
        @{ Name = "bottom-right-login-inner";   X = $size.Width - 135; Y = $size.Height - 35 }
    )

    $submitted = $false
    $index = 0
    foreach ($attempt in $attempts) {
        ++$index
        Focus-ClientWindow -Handle $Handle
        $x = [Math]::Max(1, [int]$attempt.X)
        $y = [Math]::Max(1, [int]$attempt.Y)
        $ok = Click-ClientPoint -Handle $Handle -X $x -Y $y
        $submitted = $submitted -or $ok
        [void]$events.Add([pscustomobject]@{
            Time = (Get-Date).ToString("o")
            Event = "$EventPrefix-click"
            Attempt = $index
            Target = $attempt.Name
            ClientX = $x
            ClientY = $y
            ClientWidth = $size.Width
            ClientHeight = $size.Height
            Submitted = $ok
        })
        if ($ok) {
            Start-Sleep -Seconds 2
            $probeName = "{0}-after-click-{1:00}.png" -f $EventPrefix, $index
            $probeAnalysis = Capture-StateScreenshot -Handle $Handle -Name $probeName -EventName "$EventPrefix-after-click-probe"
            if ($probeAnalysis.StateLabel -ne "character-selection-like") {
                [void]$events.Add([pscustomobject]@{
                    Time = (Get-Date).ToString("o")
                    Event = "$EventPrefix-click-accepted"
                    Attempt = $index
                    NextStateLabel = $probeAnalysis.StateLabel
                    Sha256 = $probeAnalysis.Sha256
                })
                break
            }
        }
        Start-Sleep -Milliseconds 450
    }

    Focus-ClientWindow -Handle $Handle
    $wshell.SendKeys("{ENTER}")
    [void]$events.Add([pscustomobject]@{
        Time = (Get-Date).ToString("o")
        Event = "$EventPrefix-enter-fallback"
        Key = "ENTER"
        Submitted = $true
    })
    Start-Sleep -Milliseconds 700
    return $submitted
}

function Capture-WorldSequence {
    param(
        [IntPtr]$Handle,
        [string]$Prefix,
        [string]$EventName
    )

    $count = [Math]::Max(1, $WorldCaptureCount)
    $interval = [Math]::Max(1, $WorldCaptureIntervalSeconds)
    for ($i = 1; $i -le $count; ++$i) {
        if ($i -gt 1) {
            Start-Sleep -Seconds $interval
        }

        Focus-ClientWindow -Handle $Handle
        $suffix = if ($count -gt 1) { "-{0:00}" -f $i } else { "" }
        $worldPath = Join-Path $ProofDir "$Prefix$suffix.png"
        [void](Save-WindowScreenshot -Handle $Handle -Path $worldPath)
        $likelyWorld = Test-LikelyWorldScreenshot -Path $worldPath
        [void]$events.Add([pscustomobject]@{
            Time = (Get-Date).ToString("o")
            Event = $EventName
            Path = $worldPath
            Index = $i
            Count = $count
            LikelyWorld = $likelyWorld
        })
        if ($QuickSnap -and $likelyWorld) {
            break
        }
    }
}

try {
    $handle = Wait-ClientWindow -TargetProcessId $ProcessId
    if ($handle -eq [IntPtr]::Zero) {
        throw "Could not find Star Wars Galaxies window for process $ProcessId"
    }

    [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "window-found"; Handle = ("0x{0:X}" -f $handle.ToInt64()) })

    if ($SceneDriver -eq "LoginToCharacterSelect" -or $SceneDriver -eq "LoginEnterWorld") {
        Start-Sleep -Seconds $LoginDelaySeconds
        Focus-ClientWindow -Handle $handle
        $loginPath = Join-Path $ProofDir "scene-01-login.png"
        [void](Save-WindowScreenshot -Handle $handle -Path $loginPath)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "capture-login"; Path = $loginPath })
        [void](Click-ClientLoginUsername -Handle $handle)
        Start-Sleep -Milliseconds 100
        $wshell.SendKeys("^a")
        Start-Sleep -Milliseconds 100
        $wshell.SendKeys($LoginUser)
        Start-Sleep -Milliseconds 100
        $wshell.SendKeys("{TAB}")
        Start-Sleep -Milliseconds 100
        $wshell.SendKeys("^a")
        Start-Sleep -Milliseconds 100
        $wshell.SendKeys($LoginPassword)
        Start-Sleep -Milliseconds 100
        [void](Click-ClientBottomRightLogin -Handle $handle)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "submit-login-click"; Target = "bottom-right-next" })

        Start-Sleep -Seconds $CharacterDelaySeconds
        Focus-ClientWindow -Handle $handle
        $characterPath = Join-Path $ProofDir "scene-02-character.png"
        [void](Save-WindowScreenshot -Handle $handle -Path $characterPath)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "capture-character"; Path = $characterPath })

        if ($SceneDriver -eq "LoginEnterWorld") {
            [void](Invoke-CharacterLoginSubmit -Handle $handle -EventPrefix "submit-character")

            Start-Sleep -Seconds 3
            $afterSubmitPath = Join-Path $ProofDir "scene-02-after-character-submit.png"
            [void](Save-WindowScreenshot -Handle $handle -Path $afterSubmitPath)
            $afterSubmitAnalysis = Get-ScreenshotAnalysis -Path $afterSubmitPath
            [void]$events.Add([pscustomobject]@{
                Time = (Get-Date).ToString("o")
                Event = "capture-after-character-submit"
                Path = $afterSubmitPath
                Analysis = $afterSubmitAnalysis
            })

            Start-Sleep -Seconds $WorldDelaySeconds
            Capture-WorldSequence -Handle $handle -Prefix "scene-03-world" -EventName "capture-world"
        }
        else {
            [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "stop-at-character-select" })
        }
    }
    elseif ($SceneDriver -eq "ConfigAutoConnect") {
        Start-Sleep -Seconds $CharacterDelaySeconds
        Focus-ClientWindow -Handle $handle
        $characterPath = Join-Path $ProofDir "scene-01-autoconnect-character.png"
        [void](Save-WindowScreenshot -Handle $handle -Path $characterPath)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "capture-autoconnect-character"; Path = $characterPath })
        [void](Click-ClientBottomRightLogin -Handle $handle)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "submit-autoconnect-character-click"; Target = "bottom-right-login" })

        Start-Sleep -Seconds $WorldDelaySeconds
        Capture-WorldSequence -Handle $handle -Prefix "scene-02-autoconnect-world" -EventName "capture-autoconnect-world"
    }
    elseif ($SceneDriver -eq "ConfigAutoConnectActionChain") {
        Start-Sleep -Seconds $GalaxyDelaySeconds
        Focus-ClientWindow -Handle $handle
        $initialPath = Join-Path $ProofDir "scene-01-autoconnect-initial.png"
        [void](Save-WindowScreenshot -Handle $handle -Path $initialPath)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "capture-autoconnect-initial"; Path = $initialPath })
        [void](Click-ClientBottomRightLogin -Handle $handle)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "submit-autoconnect-action-1"; Target = "bottom-right" })

        Start-Sleep -Seconds $CharacterDelaySeconds
        Focus-ClientWindow -Handle $handle
        $afterFirstPath = Join-Path $ProofDir "scene-02-autoconnect-after-action-1.png"
        [void](Save-WindowScreenshot -Handle $handle -Path $afterFirstPath)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "capture-autoconnect-after-action-1"; Path = $afterFirstPath })
        [void](Click-ClientBottomRightLogin -Handle $handle)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "submit-autoconnect-action-2"; Target = "bottom-right" })

        Start-Sleep -Seconds $WorldDelaySeconds
        Capture-WorldSequence -Handle $handle -Prefix "scene-03-autoconnect-world" -EventName "capture-autoconnect-world"
    }
    elseif ($SceneDriver -eq "ConfigAutoConnectEnterChain") {
        Start-Sleep -Seconds $GalaxyDelaySeconds
        Focus-ClientWindow -Handle $handle
        $initialPath = Join-Path $ProofDir "scene-01-enterchain-initial.png"
        [void](Save-WindowScreenshot -Handle $handle -Path $initialPath)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "capture-enterchain-initial"; Path = $initialPath })
        $wshell.SendKeys("{ENTER}")
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "submit-enterchain-action-1"; Key = "ENTER" })

        Start-Sleep -Seconds $CharacterDelaySeconds
        Focus-ClientWindow -Handle $handle
        $afterFirstPath = Join-Path $ProofDir "scene-02-enterchain-after-action-1.png"
        [void](Save-WindowScreenshot -Handle $handle -Path $afterFirstPath)
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "capture-enterchain-after-action-1"; Path = $afterFirstPath })
        $wshell.SendKeys("{ENTER}")
        [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "submit-enterchain-action-2"; Key = "ENTER" })

        Start-Sleep -Seconds $WorldDelaySeconds
        Capture-WorldSequence -Handle $handle -Prefix "scene-03-enterchain-world" -EventName "capture-enterchain-world"
    }
    elseif ($SceneDriver -eq "ConfigAutoConnectStateMachine") {
        Start-Sleep -Seconds $GalaxyDelaySeconds
        $previousHash = $null
        $maxActions = [Math]::Max(1, $StateMachineMaxActions)

        for ($actionIndex = 0; $actionIndex -le $maxActions; ++$actionIndex) {
            $statePath = "scene-state-{0:00}.png" -f $actionIndex
            $analysis = Capture-StateScreenshot -Handle $handle -Name $statePath -EventName "state-machine-capture"
            $unchanged = ($previousHash -and $analysis.Sha256 -eq $previousHash)
            [void]$events.Add([pscustomobject]@{
                Time = (Get-Date).ToString("o")
                Event = "state-machine-decision"
                ActionIndex = $actionIndex
                StateLabel = $analysis.StateLabel
                Sha256 = $analysis.Sha256
                UnchangedFromPrevious = [bool]$unchanged
                CenterTealFraction = $analysis.CenterTealFraction
                LeftWhiteFraction = $analysis.LeftWhiteFraction
                RightPanelTealFraction = $analysis.RightPanelTealFraction
                BottomButtonTealFraction = $analysis.BottomButtonTealFraction
                BrightFraction = $analysis.BrightFraction
                LikelyWorld = $analysis.LikelyWorld
            })

            if ($analysis.LikelyWorld) {
                break
            }
            if ($actionIndex -ge $maxActions) {
                break
            }

            $stalledCharacterGate = (
                $analysis.StateLabel -eq "loading-screen-like" -and
                $analysis.CenterTealFraction -gt 0.22 -and
                $analysis.BrightFraction -gt 0.36 -and
                $actionIndex -gt 0
            )

            $loginProgressLike = (
                $analysis.StateLabel -eq "unknown" -and
                $analysis.BrightFraction -lt 0.18 -and
                $analysis.CenterTealFraction -lt 0.02 -and
                $analysis.LeftWhiteFraction -lt 0.02
            )

            $stalledStartupBlank = ($analysis.StateLabel -eq "startup-blank-like" -and $actionIndex -gt 0)

            if (($analysis.StateLabel -eq "startup-blank-like" -and !$stalledStartupBlank) -or ($analysis.StateLabel -eq "loading-screen-like" -and !$stalledCharacterGate) -or $loginProgressLike) {
                [void]$events.Add([pscustomobject]@{
                    Time = (Get-Date).ToString("o")
                    Event = "state-machine-wait-loading"
                    ActionIndex = $actionIndex
                    DelaySeconds = ([Math]::Max(8, $WorldDelaySeconds))
                })
                $previousHash = $analysis.Sha256
                Start-Sleep -Seconds ([Math]::Max(8, $WorldDelaySeconds))
                continue
            }

            if ($analysis.StateLabel -eq "character-selection-like" -or $analysis.StateLabel -eq "unknown" -or $stalledCharacterGate -or $stalledStartupBlank) {
                [void](Invoke-CharacterLoginSubmit -Handle $handle -EventPrefix ("state-machine-character-submit-{0}" -f ($actionIndex + 1)))
            }
            elseif ($analysis.StateLabel -eq "login-or-menu-like") {
                [void](Invoke-LoginSubmit -Handle $handle -EventPrefix ("state-machine-login-submit-{0}" -f ($actionIndex + 1)))
            }
            else {
                [void](Invoke-BottomRightAction -Handle $handle -EventName "state-machine-submit" -Target ("bottom-right-advance-{0}" -f ($actionIndex + 1)))
            }
            $previousHash = $analysis.Sha256
            Start-Sleep -Seconds ([Math]::Max(3, $CharacterDelaySeconds))
        }

        Capture-WorldSequence -Handle $handle -Prefix "scene-state-final-world-check" -EventName "state-machine-final-world-check"
    }
}
catch {
    [void]$events.Add([pscustomobject]@{ Time = (Get-Date).ToString("o"); Event = "error"; Message = $_.Exception.Message })
}
finally {
    [pscustomobject]@{
        StartedAt = $startedAt.ToString("o")
        FinishedAt = (Get-Date).ToString("o")
        ProcessId = $ProcessId
        SceneDriver = $SceneDriver
        Events = @($events.ToArray())
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath
}
