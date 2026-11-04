param(
    [Parameter(Mandatory = $true)][ValidateSet('Native', 'CMake')][string]$EntryPoint,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
function Get-FileSha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$managed = Join-Path $root '.ci-tools/dxc'
$backup = Join-Path $root ('.ci-tools/dxc-before-bootstrap-' + [Guid]::NewGuid().ToString('N'))
$output = Join-Path $root "build/windows-dxc-bootstrap-$Configuration"
New-Item -ItemType Directory -Force $output | Out-Null
$success = $false
$hadCache = Test-Path $managed
if ($hadCache) { Move-Item -LiteralPath $managed -Destination $backup }
Push-Location $root
try {
    if ($EntryPoint -eq 'Native') {
        $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (!$vs) { throw 'Visual Studio C++ toolchain is unavailable' }
        $command = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
        $arguments = @("$root/jEngine/jEngine.vcxproj", '/t:VerifyPinnedShaderCompiler',
            "/p:Configuration=$Configuration", '/p:Platform=x64', '/nologo', '/verbosity:minimal')
        & $command @arguments /p:JEngineAutoSetupDxc=false *> "$output/opt-out.log"
        if ($LASTEXITCODE -eq 0 -or (Test-Path $managed)) {
            throw 'Native opt-out must fail with missing DXC without creating a managed cache'
        }
        Write-Host 'PASS native opt-out preserves the missing-cache state'
    } else {
        $command = 'cmake'
        $arguments = @('--preset', 'windows-vs2026')
    }
    # Call the actual build entry point, never the installer directly.
    & $command @arguments 2>&1 | Tee-Object "$output/cold.log"
    if ($LASTEXITCODE -ne 0) { throw "$EntryPoint cold bootstrap failed" }
    $dependency = (Get-Content "$root/scripts/ci/dependencies.json" -Raw | ConvertFrom-Json).dxc
    if ((Get-Content "$managed/revision" -Raw).Trim() -ne $dependency.commit) {
        throw 'Automatic setup selected an unexpected DXC revision'
    }
    $files = @('dxc.exe', 'dxcompiler.dll', 'dxil.dll')
    $before = @{}
    foreach ($name in $files) {
        $file = Get-Item "$managed/bin/x64/$name"
        $before[$name] = @($file.LastWriteTimeUtc.Ticks, (Get-FileSha256 $file.FullName))
    }
    & $command @arguments 2>&1 | Tee-Object "$output/warm.log"
    if ($LASTEXITCODE -ne 0) { throw "$EntryPoint warm bootstrap failed" }
    foreach ($name in $files) {
        $file = Get-Item "$managed/bin/x64/$name"
        if ($file.LastWriteTimeUtc.Ticks -ne $before[$name][0] -or
            (Get-FileSha256 $file.FullName) -ne $before[$name][1]) {
            throw "Warm $EntryPoint setup unexpectedly replaced $name"
        }
    }
    @{ entryPoint = $EntryPoint; configuration = $Configuration; cold = 'passed';
       warm = 'passed'; revision = $dependency.commit; fileHashes = $before } |
        ConvertTo-Json -Depth 5 | Set-Content "$output/verification.json" -Encoding UTF8
    $success = $true
    Write-Host "PASS $EntryPoint cold automatic setup and unchanged warm cache"
} finally {
    Pop-Location
    if ($success) {
        if (Test-Path $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
    } else {
        # Restore the caller's original managed cache after a failed probe.
        if (Test-Path $managed) { Remove-Item -LiteralPath $managed -Recurse -Force }
        if ($hadCache -and (Test-Path $backup)) { Move-Item -LiteralPath $backup -Destination $managed }
    }
}
