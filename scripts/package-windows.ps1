[CmdletBinding()]
param(
    [string]$MSys2Root = "C:\msys64",
    [string]$BuildExe = "",
    [string]$DistDir = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (!$BuildExe) {
    $BuildExe = Join-Path $scriptRoot "..\build\snake_game.exe"
}
if (!$DistDir) {
    $DistDir = Join-Path $scriptRoot "..\dist\snake_game"
}

$repoRoot = (Resolve-Path (Join-Path $scriptRoot "..")).Path
$mingwRoot = Join-Path $MSys2Root "mingw64"
$bash = Join-Path $MSys2Root "usr\bin\bash.exe"
$buildScript = Join-Path $scriptRoot "build-windows.ps1"
$buildExePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildExe)
$distPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DistDir)
$msysHome = Join-Path $repoRoot ".tmp\msys-home"
New-Item -ItemType Directory -Force -Path $msysHome | Out-Null
$env:HOME = $msysHome

function Assert-InRepo {
    param([string]$Path)

    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetFullPath($repoRoot).TrimEnd("\") + "\"

    if (!$full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the repo: $full"
    }
}

function Convert-ToMsysPath {
    param([string]$WindowsPath)

    $full = [IO.Path]::GetFullPath($WindowsPath)
    if ($full -notmatch "^([A-Za-z]):\\(.*)$") {
        throw "Cannot convert path to MSYS format: $full"
    }

    $drive = $matches[1].ToLowerInvariant()
    $rest = $matches[2] -replace "\\", "/"
    return "/$drive/$rest"
}

function Convert-FromMingwPath {
    param([string]$MsysPath)

    $relative = $MsysPath.Substring("/mingw64/".Length) -replace "/", "\"
    return Join-Path $mingwRoot $relative
}

function Get-MingwDllDependencies {
    param([string[]]$RootFiles)

    if (!(Test-Path -LiteralPath $bash)) {
        throw "Could not find MSYS2 bash at $bash"
    }

    $scanned = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $deps = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue = [System.Collections.Generic.Queue[string]]::new()

    foreach ($file in $RootFiles) {
        if (Test-Path -LiteralPath $file) {
            $queue.Enqueue(([IO.Path]::GetFullPath($file)))
        }
    }

    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if (!$scanned.Add($current)) {
            continue
        }

        $msysPath = Convert-ToMsysPath $current
        $escapedPath = $msysPath -replace "'", "'\''"
        $lddCommand = "export PATH=/mingw64/bin:/usr/bin:`$PATH; ldd '$escapedPath'"
        $lddOutput = & $bash -lc $lddCommand

        foreach ($line in $lddOutput) {
            $depPath = $null
            if ($line -match "=>\s+(/mingw64/[^\s]+)\s+\(") {
                $depPath = $matches[1]
            }
            elseif ($line -match "^\s*(/mingw64/[^\s]+)\s+\(") {
                $depPath = $matches[1]
            }

            if (!$depPath -or $depPath -notmatch "\.dll$") {
                continue
            }

            $windowsDep = Convert-FromMingwPath $depPath
            if ($deps.Add($windowsDep)) {
                $queue.Enqueue($windowsDep)
            }
        }
    }

    return $deps | Sort-Object
}

function Copy-DirectoryIfExists {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Source) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
    }
}

Assert-InRepo $distPath

if (!$SkipBuild) {
    & $buildScript -MSys2Root $MSys2Root -Output $buildExePath
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed; package step stopped."
    }
}

if (!(Test-Path -LiteralPath $buildExePath)) {
    throw "Build executable does not exist: $buildExePath"
}

if (Test-Path -LiteralPath $distPath) {
    Remove-Item -LiteralPath $distPath -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $distPath | Out-Null
Copy-Item -LiteralPath $buildExePath -Destination (Join-Path $distPath "snake_game.exe") -Force
Copy-DirectoryIfExists -Source (Join-Path $repoRoot "media") -Destination (Join-Path $distPath "media")

$dynamicModuleRoots = @(
    (Join-Path $mingwRoot "lib\gdk-pixbuf-2.0"),
    (Join-Path $mingwRoot "lib\gio\modules"),
    (Join-Path $mingwRoot "lib\gtk-4.0")
)

$dependencyRoots = @($buildExePath)
foreach ($moduleRoot in $dynamicModuleRoots) {
    if (Test-Path -LiteralPath $moduleRoot) {
        $dependencyRoots += Get-ChildItem -LiteralPath $moduleRoot -Filter "*.dll" -Recurse | ForEach-Object { $_.FullName }
    }
}

$dlls = Get-MingwDllDependencies -RootFiles $dependencyRoots
foreach ($dll in $dlls) {
    Copy-Item -LiteralPath $dll -Destination (Join-Path $distPath (Split-Path -Leaf $dll)) -Force
}

$runtimeDirs = @(
    @{ Source = "etc\fonts"; Destination = "etc\fonts" },
    @{ Source = "lib\gdk-pixbuf-2.0"; Destination = "lib\gdk-pixbuf-2.0" },
    @{ Source = "lib\gio\modules"; Destination = "lib\gio\modules" },
    @{ Source = "lib\gtk-4.0"; Destination = "lib\gtk-4.0" },
    @{ Source = "share\fontconfig"; Destination = "share\fontconfig" },
    @{ Source = "share\glib-2.0\schemas"; Destination = "share\glib-2.0\schemas" },
    @{ Source = "share\icons\Adwaita"; Destination = "share\icons\Adwaita" },
    @{ Source = "share\icons\hicolor"; Destination = "share\icons\hicolor" },
    @{ Source = "share\themes"; Destination = "share\themes" }
)

foreach ($dir in $runtimeDirs) {
    Copy-DirectoryIfExists `
        -Source (Join-Path $mingwRoot $dir.Source) `
        -Destination (Join-Path $distPath $dir.Destination)
}

Set-Content -Path (Join-Path $distPath "run-snake-game.bat") -Encoding ASCII -Value @(
    "@echo off",
    "cd /d ""%~dp0""",
    "start """" ""%~dp0snake_game.exe"""
)

Set-Content -Path (Join-Path $distPath "README-RUN.txt") -Encoding ASCII -Value @(
    "Snake Game",
    "",
    "Run run-snake-game.bat or snake_game.exe from this folder.",
    "Keep the media folder and DLL files beside snake_game.exe.",
    "snake_game.db is created automatically in this folder to store the best score."
)

Write-Host "Packaged $distPath"
Write-Host "Copied $($dlls.Count) MinGW runtime DLLs."
