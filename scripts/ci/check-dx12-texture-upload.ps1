param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$output = Join-Path $root "build/windows-dx12-texture-$Configuration"
$preset = "windows-$($Configuration.ToLowerInvariant())"
New-Item -ItemType Directory -Force $output | Out-Null
Push-Location $root
try {
    & cmake --preset windows-vs2026 2>&1 | Tee-Object "$output/configure.log"
    if ($LASTEXITCODE -ne 0) { throw 'DX12 texture test configuration failed' }
    & cmake --build --preset $preset --target jEngineDX12TextureUploadTests -- /nologo /verbosity:minimal 2>&1 | Tee-Object "$output/build.log"
    if ($LASTEXITCODE -ne 0) { throw 'DX12 texture test build failed' }
    & ctest --preset $preset -R '^dx12-texture-upload$' --verbose --output-junit "$output/tests.xml" 2>&1 | Tee-Object "$output/tests.log"
    if ($LASTEXITCODE -ne 0) { throw 'Real WARP texture upload/readback or debug-layer validation failed' }
} finally { Pop-Location }
