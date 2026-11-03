param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root 'build/windows-vs2026'
$preset = "windows-$($Configuration.ToLowerInvariant())"
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $root
try {
    & cmake --preset windows-vs2026 2>&1 | Tee-Object "$out/configure.log"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    & cmake --build --preset $preset -- /nologo /verbosity:minimal /nodeReuse:false "/bl:$out/build.binlog" 2>&1 | Tee-Object "$out/build.log"
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
    & ctest --preset $preset --output-junit "$out/tests.xml" 2>&1 | Tee-Object "$out/tests.log"
    if ($LASTEXITCODE -ne 0) { throw 'CPU or shader checks failed' }
} finally { Pop-Location }
