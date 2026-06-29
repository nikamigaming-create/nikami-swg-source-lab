param(
    [string]$Configuration = "Release",
    [string]$Target = "swg_modern_client",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$ClientRoot = Join-Path $Root "modern-client"
$BuildDir = Join-Path $ClientRoot "build"

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

if (!(Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt"))) {
    cmake -S $ClientRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# MSBuild can see both Path and PATH in this shell. Normalize before build.
$savedPath = $env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $savedPath

cmake --build $BuildDir --config $Configuration --target $Target -- /m
exit $LASTEXITCODE
