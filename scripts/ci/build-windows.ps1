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
    # This standalone target has no engine/GPU dependencies. Catch a checked
    # STL assertion before spending a full engine build on the same runner.
    & cmake --build --preset $preset --target jEngineAdvancedShadowMathTests -- /nologo /verbosity:minimal /nodeReuse:false 2>&1 | Tee-Object "$out/advanced-shadow-math-build.log"
    if ($LASTEXITCODE -ne 0) { throw 'Standalone shadow math build failed' }
    & ctest --preset $preset -R '^advanced-shadow-math$' --output-junit "$out/advanced-shadow-math.xml" 2>&1 | Tee-Object "$out/advanced-shadow-math.log"
    if ($LASTEXITCODE -ne 0) { throw 'Standalone shadow math checks failed' }
    & cmake --build --preset $preset -- /nologo /verbosity:minimal /nodeReuse:false "/bl:$out/build.binlog" 2>&1 | Tee-Object "$out/build.log"
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
    & ctest --preset $preset --output-junit "$out/tests.xml" 2>&1 | Tee-Object "$out/tests.log"
    if ($LASTEXITCODE -ne 0) { throw 'CPU or shader checks failed' }
} finally { Pop-Location }
