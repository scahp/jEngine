param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root "build\windows-$Configuration"
New-Item -ItemType Directory -Force "$out/bin", "$out/obj" | Out-Null
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ toolchain is unavailable' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
Push-Location "$root/jEngine"
try {
    & $msbuild jEngine.vcxproj /m:2 /nologo /verbosity:minimal /nodeReuse:false `
        "/p:Configuration=$Configuration" /p:Platform=x64 "/p:OutDir=$out\bin\" "/p:IntDir=$out\obj\" `
        "/bl:$out/build.binlog" /fl "/flp:logfile=$out/build.log;verbosity=normal"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)" }
} finally { Pop-Location }
