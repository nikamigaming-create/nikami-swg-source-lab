param(
    [string[]]$Roots = @(),
    [string]$OutputPath = "",
    [switch]$IncludeQuarantine
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "Resolve-NikamiWorkspace.ps1")
$ClientToolsRoot = Get-NikamiClientToolsRoot -ScriptDir $ScriptDir
$WorkspaceRoot = Get-NikamiWorkspaceRoot -ClientToolsRoot $ClientToolsRoot
if (!$Roots -or $Roots.Count -eq 0) {
    $Roots = @(
        (Get-NikamiRuntimeClientRoot -WorkspaceRoot $WorkspaceRoot),
        $ClientToolsRoot,
        (Join-Path $WorkspaceRoot "cleanroom"),
        (Join-Path $WorkspaceRoot "artifacts")
    )
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path (Get-NikamiProofRoot -WorkspaceRoot $WorkspaceRoot) "og_client_file_audit.json"
}
$names = @(
    "SwgClient_r.exe",
    "SwgClient.exe",
    "gl05_r.dll",
    "gl06_r.dll",
    "gl07_r.dll",
    "gl08_r.dll",
    "dpvs.dll",
    "DllExport.dll",
    "DebugWindow.dll",
    "d3d9.dll"
)

$results = foreach ($root in $Roots) {
    if (!(Test-Path -LiteralPath $root)) { continue }
    foreach ($name in $names) {
        Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name -ErrorAction SilentlyContinue | Where-Object {
            $IncludeQuarantine -or (
                $_.FullName.IndexOf("\_quarantine_dx11\", [System.StringComparison]::OrdinalIgnoreCase) -lt 0 -and
                $_.FullName.IndexOf("\_clean-backups\", [System.StringComparison]::OrdinalIgnoreCase) -lt 0
            )
        } | ForEach-Object {
            $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
            [pscustomobject]@{
                Name = $_.Name
                FullName = $_.FullName
                Root = $root
                Length = $_.Length
                LastWriteTime = $_.LastWriteTime
                SHA256 = $hash.Hash
            }
        }
    }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$results | Sort-Object Name, SHA256, FullName | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $OutputPath
$results | Sort-Object Name, SHA256, FullName
