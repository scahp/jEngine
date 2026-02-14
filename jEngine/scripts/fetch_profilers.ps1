param(
    [string]$Root = "."
)

$ErrorActionPreference = "Stop"

$externalDir = Join-Path $Root "jEngine/External"
if (-not (Test-Path $externalDir)) {
    throw "External directory not found: $externalDir"
}

Push-Location $externalDir
try {
    if (-not (Test-Path "tracy")) {
        git clone --depth 1 https://github.com/wolfpld/tracy.git tracy
    } else {
        Write-Host "tracy already exists, skipping."
    }

    if (-not (Test-Path "optick")) {
        git clone --depth 1 https://github.com/bombomby/optick.git optick
    } else {
        Write-Host "optick already exists, skipping."
    }
}
finally {
    Pop-Location
}

Write-Host "Profiler dependencies are ready."
