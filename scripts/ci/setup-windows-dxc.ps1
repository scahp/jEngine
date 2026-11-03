$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$dependency = (Get-Content "$root/scripts/ci/dependencies.json" -Raw | ConvertFrom-Json).dxc
$out = "$root/.ci-tools/dxc"
$cachedRevision = if (Test-Path "$out/revision") { (Get-Content "$out/revision" -Raw).Trim() } else { '' }
if (!(Test-Path "$out/bin/x64/dxc.exe") -or $cachedRevision -ne $dependency.commit) {
    New-Item -ItemType Directory -Force $out | Out-Null
    $archive = "$out/dxc.zip"
    Invoke-WebRequest $dependency.windows_url -OutFile $archive
    $digest = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($digest -ne $dependency.windows_sha256) { throw 'DXC archive SHA-256 mismatch' }
    Expand-Archive $archive -DestinationPath $out -Force
    Remove-Item $archive
    Set-Content "$out/revision" $dependency.commit
}
& "$out/bin/x64/dxc.exe" --version
if ($LASTEXITCODE -ne 0) { throw 'DXC cannot run' }
