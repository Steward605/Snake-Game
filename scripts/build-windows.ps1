[CmdletBinding()]
param(
    [string]$MSys2Root = "C:\msys64",
    [string]$Source = "",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (!$Source) {
    $Source = Join-Path $scriptRoot "..\snake_game.c"
}
if (!$Output) {
    $Output = Join-Path $scriptRoot "..\build\snake_game.exe"
}

$mingwRoot = Join-Path $MSys2Root "mingw64"
$usrRoot = Join-Path $MSys2Root "usr"
$gcc = Join-Path $mingwRoot "bin\gcc.exe"
$pkgConfig = Join-Path $mingwRoot "bin\pkg-config.exe"
$sourcePath = (Resolve-Path $Source).Path
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
$outputDir = Split-Path -Parent $outputPath

if (!(Test-Path -LiteralPath $gcc)) {
    throw "Could not find MSYS2 MinGW64 gcc at $gcc"
}

if (!(Test-Path -LiteralPath $pkgConfig)) {
    throw "Could not find MSYS2 MinGW64 pkg-config at $pkgConfig"
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$env:Path = (Join-Path $mingwRoot "bin") + [IO.Path]::PathSeparator +
    (Join-Path $usrRoot "bin") + [IO.Path]::PathSeparator +
    $env:Path
$env:PKG_CONFIG_PATH = Join-Path $mingwRoot "lib\pkgconfig"
$env:TMP = [IO.Path]::GetTempPath().TrimEnd("\")
$env:TEMP = $env:TMP

$packages = @("gtk4", "sdl2", "SDL2_image", "SDL2_ttf", "sqlite3")
$pkgLine = & $pkgConfig --cflags --libs @packages
if ($LASTEXITCODE -ne 0) {
    throw "pkg-config could not find all required packages: $($packages -join ', ')"
}

$pkgArgs = $pkgLine -split "\s+" | Where-Object { $_ }

& $gcc -fdiagnostics-color=always $sourcePath -o $outputPath @pkgArgs
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "Built $outputPath"
