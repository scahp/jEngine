param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Clean,
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"

function Get-CMakeVersion([string]$CMakePath)
{
    $line = & $CMakePath --version | Select-Object -First 1
    if ($line -match "(\d+)\.(\d+)\.(\d+)") {
        return [Version]::new([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
    }
    throw "Failed to parse CMake version from '$line'."
}

function Resolve-CMakePath()
{
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($env:CMAKE_EXE -and (Test-Path $env:CMAKE_EXE)) {
        $candidates.Add((Resolve-Path $env:CMAKE_EXE).Path)
    }
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) {
        $candidates.Add($cmd.Source)
    }
    $candidates = $candidates | Select-Object -Unique
    if (-not $candidates -or $candidates.Count -eq 0) {
        throw "CMake was not found. Install CMake 3.25+ or set CMAKE_EXE."
    }

    $bestPath = $null
    $bestVersion = [Version]::new(0, 0, 0)
    foreach ($candidate in $candidates) {
        try {
            $version = Get-CMakeVersion -CMakePath $candidate
            if ($version -gt $bestVersion) {
                $bestVersion = $version
                $bestPath = $candidate
            }
        }
        catch {
        }
    }
    if (-not $bestPath) {
        throw "Failed to determine CMake version from candidates."
    }
    if ($bestVersion -lt [Version]::new(3, 25, 0)) {
        throw "CMake 3.25+ is required (found $bestVersion at '$bestPath')."
    }
    return $bestPath
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceDir = Join-Path $repoRoot "External\tracy\profiler"
$buildDir = Join-Path $sourceDir "build_vs2022"

if (-not (Test-Path $sourceDir)) {
    throw "Missing Tracy source: '$sourceDir'"
}

$cmakePath = Resolve-CMakePath

if ($Clean -or $Rebuild) {
    if (Test-Path $buildDir) {
        & $cmakePath --build $buildDir --config $Configuration --target clean
    }
    if ($Clean) {
        exit 0
    }
}

& $cmakePath -S $sourceDir -B $buildDir -G "Visual Studio 17 2022" -A x64
& $cmakePath --build $buildDir --config $Configuration --target tracy-profiler

$exePath = Join-Path $buildDir "$Configuration\tracy-profiler.exe"
if (-not (Test-Path $exePath)) {
    throw "Build finished but executable not found: $exePath"
}
Write-Host "Built: $exePath"
