# test-login-and-game.ps1
# Automates SWG login and character selection to enter the world, taking screenshots at each phase.

param(
    [string]$ClientDataRoot = "",
    [string]$ProofDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
$ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if ([string]::IsNullOrWhiteSpace($ClientDataRoot)) {
    $ClientDataRoot = Get-NikamiRuntimeClientRoot -WorkspaceRoot $WorkspaceRoot
}
if ([string]::IsNullOrWhiteSpace($ProofDir)) {
    $ProofDir = Join-Path (Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot) "login-test"
}

# Create proof folder
if (!(Test-Path -Path $ProofDir)) {
    New-Item -ItemType Directory -Force -Path $ProofDir | Out-Null
}

# Clean old screenshots
Get-ChildItem -Path $ProofDir -Filter "*.png" -ErrorAction SilentlyContinue | Remove-Item -Force

# Load Win32 methods for SetForegroundWindow and GetWindowRect
$win32Source = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace SwgAutomation
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

        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        public static extern bool IsWindowVisible(IntPtr hWnd);

        [DllImport("user32.dll", CharSet=CharSet.Unicode)]
        public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

        public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    }
}
"@

Add-Type -TypeDefinition $win32Source

function Get-ClientWindowHandle {
    param([int]$ProcessId)
    $handle = [IntPtr]::Zero
    $callback = [SwgAutomation.NativeMethods+EnumWindowsProc] {
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        [uint32]$windowPid = 0
        [void][SwgAutomation.NativeMethods]::GetWindowThreadProcessId($hWnd, [ref]$windowPid)
        if ($windowPid -eq [uint32]$ProcessId) {
            $title = [System.Text.StringBuilder]::new(512)
            [void][SwgAutomation.NativeMethods]::GetWindowText($hWnd, $title, $title.Capacity)
            if ($title.ToString() -eq "Star Wars Galaxies" -and [SwgAutomation.NativeMethods]::IsWindowVisible($hWnd)) {
                $script:foundHandle = $hWnd
                return $false
            }
        }
        return $true
    }
    $script:foundHandle = [IntPtr]::Zero
    [void][SwgAutomation.NativeMethods]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:foundHandle
}

function Save-Screenshot {
    param(
        [IntPtr]$hWnd,
        [string]$Path
    )
    $rect = New-Object SwgAutomation.NativeMethods+RECT
    if (![SwgAutomation.NativeMethods]::GetWindowRect($hWnd, [ref]$rect)) {
        Write-Output "Failed to get window rect"
        return
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        Write-Output "Invalid window dimensions: ${width}x${height}"
        return
    }

    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, [System.Drawing.Size]::new($width, $height))
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Output "Saved screenshot to: $Path"
    }
    catch {
        Write-Output "Screenshot save failed: $_"
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$wshell = New-Object -ComObject Wscript.Shell

Write-Output "[1] Launching SwgClient_r.exe..."
$clientProcess = Start-Process -FilePath (Join-Path $ClientDataRoot "SwgClient_r.exe") -WorkingDirectory $ClientDataRoot -PassThru

try {
    # 1. Wait for Login Screen
    Write-Output "Waiting 8 seconds for login screen to load..."
    Start-Sleep -Seconds 8

    $hWnd = Get-ClientWindowHandle -ProcessId $clientProcess.Id
    if ($hWnd -eq [IntPtr]::Zero) {
        throw "Could not find SwgClient window handle"
    }

    [void][SwgAutomation.NativeMethods]::SetForegroundWindow($hWnd)
    Start-Sleep -Milliseconds 500
    Save-Screenshot -hWnd $hWnd -Path (Join-Path $ProofDir "01_login_screen.png")

    # Submit login (presumes test/test are filled)
    Write-Output "Sending ENTER to submit credentials..."
    [void][SwgAutomation.NativeMethods]::SetForegroundWindow($hWnd)
    $wshell.SendKeys("{ENTER}")

    # 2. Wait for Character Selection Screen
    Write-Output "Waiting 8 seconds for character selection screen..."
    Start-Sleep -Seconds 8

    [void][SwgAutomation.NativeMethods]::SetForegroundWindow($hWnd)
    Save-Screenshot -hWnd $hWnd -Path (Join-Path $ProofDir "02_character_selection.png")

    # Enter Game
    Write-Output "Sending ENTER to select character and enter the game..."
    [void][SwgAutomation.NativeMethods]::SetForegroundWindow($hWnd)
    $wshell.SendKeys("{ENTER}")

    # 3. Wait for In-Game loading and rendering
    Write-Output "Waiting 15 seconds to load into the world..."
    Start-Sleep -Seconds 15

    [void][SwgAutomation.NativeMethods]::SetForegroundWindow($hWnd)
    Save-Screenshot -hWnd $hWnd -Path (Join-Path $ProofDir "03_in_game_world.png")

}
finally {
    Write-Output "Stopping SwgClient_r.exe..."
    Stop-Process -Id $clientProcess.Id -Force -ErrorAction SilentlyContinue
}

Write-Output "Done! All phases tested."
