param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$SourceRoot = Join-Path $Root "modern-client\src"

$knobs = rg -n -o "SWG_[A-Z0-9_]+" $SourceRoot |
    ForEach-Object {
        if ($_ -match "^(?<file>.+):(?<line>\d+):(?<name>SWG_[A-Z0-9_]+)$") {
            [pscustomobject]@{
                File = $Matches["file"]
                Line = [int]$Matches["line"]
                Name = $Matches["name"]
            }
        }
    } |
    Sort-Object Name, File, Line -Unique

if ($OutputPath) {
    $knobs | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $OutputPath
}
else {
    $knobs | Format-Table -AutoSize
}
