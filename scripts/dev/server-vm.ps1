param(
    [ValidateSet("status", "mount-d", "sync-d", "targets", "echoprops", "build-src", "build", "start-server", "stop-server", "start-vm", "stop-vm", "restart-vm", "shell")]
    [string]$Action = "status",
    [string]$VMName = "SWG-Source-Server",
    [string]$VBoxManage = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe",
    [string]$HostRoot = "",
    [string]$GuestRoot = "/mnt/swg-d",
    [string]$GuestBuildRoot = "/home/swg/swg-build-source-d",
    [string]$GuestRuntimeRoot = "/home/swg/swg-main",
    [string]$GuestUser = "swg",
    [string]$GuestPassword = "swg",
    [string]$RootPassword = "swg",
    [int]$BuildJobs = 6,
    [string]$ShellCommand = "pwd; ls -la | head"
)

$ErrorActionPreference = "Continue"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
$ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
if ([string]::IsNullOrWhiteSpace($HostRoot)) {
    $HostRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
}
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -Scope Global -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}
if ($env:SWG_DEV_VM_BOOTSTRAPPED -ne "1") {
    $env:SWG_DEV_VM_BOOTSTRAPPED = "1"
    if ($null -eq $env:SWG_DEV_VERBOSE_VM_SCRIPT) {
        $env:SWG_DEV_VERBOSE_VM_SCRIPT = "0"
    }
    $relaunchArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $PSCommandPath,
        "-Action", $Action,
        "-VMName", $VMName,
        "-VBoxManage", $VBoxManage,
        "-HostRoot", $HostRoot,
        "-GuestRoot", $GuestRoot,
        "-GuestBuildRoot", $GuestBuildRoot,
        "-GuestRuntimeRoot", $GuestRuntimeRoot,
        "-GuestUser", $GuestUser,
        "-GuestPassword", $GuestPassword,
        "-RootPassword", $RootPassword,
        "-BuildJobs", "$BuildJobs",
        "-ShellCommand", $ShellCommand
    )
    & powershell.exe @relaunchArgs
    exit $LASTEXITCODE
}

function Invoke-VBox {
    param(
        [string[]]$VBoxArgs,
        [switch]$IgnoreError
    )
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        & $VBoxManage list vms 1>$null 2>$null
        Start-Sleep -Milliseconds 250
        & $VBoxManage @VBoxArgs
        if ($LASTEXITCODE -eq 0 -or $IgnoreError) { return }
        Start-Sleep -Seconds 2
    }
    exit $LASTEXITCODE
}

function Invoke-Guest {
    param(
        [string]$Command,
        [string]$User = $GuestUser,
        [string]$Password = $GuestPassword
    )
    $guestArgs = @(
        "guestcontrol", $VMName, "run",
        "--username", $User,
        "--password", $Password,
        "--exe", "/bin/bash",
        "--wait-stdout",
        "--wait-stderr",
        "--",
        "-lc", $Command
    )
    if ($env:SWG_DEV_VERBOSE_VM_SCRIPT -eq "1") {
        Write-Output ("VBoxManage " + ($guestArgs -join " | "))
    }
    else {
        Write-Output ("guest: " + $Command)
    }
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        & $VBoxManage list vms 1>$null 2>$null
        Start-Sleep -Seconds 1
        & $VBoxManage @guestArgs
        if ($LASTEXITCODE -eq 0) { return }
        Start-Sleep -Seconds 2
    }
    exit $LASTEXITCODE
}

function Mount-D {
    Invoke-Guest -Command "mkdir -p $GuestRoot; mountpoint -q $GuestRoot || mount -t vboxsf swg-d $GuestRoot; ls -ld $GuestRoot" -User "root" -Password $RootPassword
}

function Sync-D {
    Mount-D
    $archiveDir = Join-Path $HostRoot "_archives"
    $archivePath = Join-Path $archiveDir "swg-build-source.tgz"
    New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
    if (Test-Path $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }

    $sourceRoot = Join-Path $HostRoot "swg-main"
    & tar -czf $archivePath -C $sourceRoot .gitignore .gitmodules build.properties build.xml exec.sh git_targets.xml README.md startServer.sh startWithLogging.sh src exe tools utils stationapi
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Invoke-Guest "set -e; rm -rf $GuestBuildRoot; mkdir -p $GuestBuildRoot; tar -xzf $GuestRoot/_archives/swg-build-source.tgz -C $GuestBuildRoot; cd $GuestBuildRoot; pwd; du -sh .; find src -type f | wc -l"
}

switch ($Action) {
    "status" {
        Invoke-VBox -VBoxArgs @("showvminfo", $VMName, "--machinereadable")
        Invoke-Guest "pgrep -af 'LoginServer|CentralServer|ConnectionServer|SwgDatabaseServer|SwgGameServer|TaskManager|stationchat' || true"
    }
    "start-vm" {
        Invoke-VBox -VBoxArgs @("startvm", $VMName, "--type", "headless")
    }
    "stop-vm" {
        Invoke-VBox -VBoxArgs @("controlvm", $VMName, "acpipowerbutton")
    }
    "restart-vm" {
        Invoke-VBox -VBoxArgs @("controlvm", $VMName, "reset")
    }
    "mount-d" {
        Mount-D
    }
    "sync-d" {
        Sync-D
    }
    "targets" {
        Sync-D
        Invoke-Guest "cd $GuestBuildRoot && ant -p"
    }
    "echoprops" {
        Sync-D
        Invoke-Guest "cd $GuestBuildRoot && ant echoprops"
    }
    "build-src" {
        Sync-D
        Invoke-Guest "cd $GuestBuildRoot && ant -Dbuild_jobs=$BuildJobs compile_src"
    }
    "build" {
        Sync-D
        Invoke-Guest "cd $GuestBuildRoot && ant compile"
    }
    "start-server" {
        Invoke-Guest "cd $GuestRuntimeRoot && ant start"
    }
    "stop-server" {
        Invoke-Guest "cd $GuestRuntimeRoot && ant stop"
    }
    "shell" {
        Sync-D
        Invoke-Guest "cd $GuestBuildRoot && $ShellCommand"
    }
}

exit 0
