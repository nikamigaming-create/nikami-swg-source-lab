param(
    [string]$OutputPath = "",
    [switch]$IncludeServerdata
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$files = @()
$files += Get-ChildItem -LiteralPath (Join-Path $Root "exe") -Recurse -File -Include *.cfg,*.ini -ErrorAction SilentlyContinue
if ($IncludeServerdata) {
    $files += Get-ChildItem -LiteralPath (Join-Path $Root "serverdata") -Recurse -File -Include *.cfg,*.ini -ErrorAction SilentlyContinue
}
$buildProps = Join-Path $Root "build.properties"
if (Test-Path -LiteralPath $buildProps) {
    $files += Get-Item -LiteralPath $buildProps
}

$rows = foreach ($file in $files) {
    $section = ""
    $lineNo = 0
    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        ++$lineNo
        $trimmed = $line.Trim()
        if ($trimmed -match "^\[(.+)\]$") {
            $section = $Matches[1]
            continue
        }
        if ($trimmed -match "^(?<key>[A-Za-z0-9_.-]+)\s*=\s*(?<value>.*)$") {
            [pscustomobject]@{
                File = $file.FullName
                Line = $lineNo
                Section = $section
                Key = $Matches["key"]
                Value = $Matches["value"]
            }
        }
    }
}

$rows = $rows | Sort-Object File, Section, Key, Line -Unique
if ($OutputPath) {
    $rows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $OutputPath
}
else {
    $rows | Format-Table -AutoSize
}
