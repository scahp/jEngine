[CmdletBinding()]
param(
    [string]$Destination,
    [string]$DependencyManifest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ([string]::IsNullOrWhiteSpace($Destination)) { $Destination = Join-Path $root '.ci-tools/dxc' }
if ([string]::IsNullOrWhiteSpace($DependencyManifest)) { $DependencyManifest = Join-Path $PSScriptRoot 'dependencies.json' }
$Destination = [IO.Path]::GetFullPath($Destination)
$DependencyManifest = [IO.Path]::GetFullPath($DependencyManifest)
$separator = [IO.Path]::DirectorySeparatorChar
$Destination = $Destination.TrimEnd([char[]]'\/')
if ([string]::IsNullOrWhiteSpace($Destination) -or $Destination -eq ([IO.Path]::GetPathRoot($Destination)).TrimEnd([char[]]'\/')) {
    throw 'DXC Destination must be a dedicated cache directory, not a filesystem root.'
}
$parent = [IO.Path]::GetDirectoryName($Destination)
$leaf = [IO.Path]::GetFileName($Destination)
$requiredFiles = @('bin/x64/dxc.exe', 'bin/x64/dxcompiler.dll', 'bin/x64/dxil.dll')
$lockTimeoutSeconds = 300
$utf8 = [Text.UTF8Encoding]::new($false)

function Get-Sha256([string]$Path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [IO.File]::OpenRead($Path)
        return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    }
    finally {
        if ($stream) { $stream.Dispose() }
        $algorithm.Dispose()
    }
}

function Get-DxcVersion([string]$Directory) {
    $process = New-Object Diagnostics.Process
    $process.StartInfo = New-Object Diagnostics.ProcessStartInfo
    $process.StartInfo.FileName = Join-Path $Directory 'bin/x64/dxc.exe'
    $process.StartInfo.Arguments = '--version'
    $process.StartInfo.WorkingDirectory = Join-Path $Directory 'bin/x64'
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    try {
        if (!$process.Start()) { throw 'DXC version process did not start.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(20000)) {
            try { $process.Kill() } catch { }
            throw 'DXC --version timed out after 20 seconds.'
        }
        $process.WaitForExit()
        $version = ($stdout.GetAwaiter().GetResult() + "`n" + $stderr.GetAwaiter().GetResult()).Trim()
        if ($process.ExitCode -ne 0 -or $version -notmatch '(?i)(dxc|dxcompiler|dxil)' -or $version -notmatch '\d+\.\d+') {
            throw "DXC --version failed (exit $($process.ExitCode)): $version"
        }
        return $version
    }
    finally { $process.Dispose() }
}

function Test-DxcCache([string]$Directory, [string]$Commit, [string]$ArchiveHash) {
    try {
        foreach ($relative in $requiredFiles) {
            if (!(Test-Path -LiteralPath (Join-Path $Directory $relative) -PathType Leaf)) {
                return @{ Valid = $false; Reason = "Missing $relative" }
            }
        }
        $revisionFile = Join-Path $Directory 'revision'
        if (!(Test-Path -LiteralPath $revisionFile -PathType Leaf) -or ([IO.File]::ReadAllText($revisionFile)).Trim() -ne $Commit) {
            return @{ Valid = $false; Reason = 'Pinned revision is missing or differs.' }
        }
        $receiptFile = Join-Path $Directory 'install-receipt.json'
        if (Test-Path -LiteralPath $receiptFile -PathType Leaf) {
            $receipt = [IO.File]::ReadAllText($receiptFile) | ConvertFrom-Json
            if ($receipt.commit -ne $Commit -or $receipt.archive_sha256 -ne $ArchiveHash) {
                return @{ Valid = $false; Reason = 'Install receipt differs from the pinned dependency.' }
            }
            foreach ($relative in $requiredFiles) {
                $property = $receipt.files.PSObject.Properties[$relative]
                if (!$property -or $property.Value -ne (Get-Sha256 (Join-Path $Directory $relative))) {
                    return @{ Valid = $false; Reason = "Cached bytes differ for $relative" }
                }
            }
        }
        # Legacy healthy caches have only the revision file. Accept them without
        # rewriting files or downloading solely to create the newer receipt.
        return @{ Valid = $true; Version = (Get-DxcVersion $Directory); Reason = '' }
    }
    catch { return @{ Valid = $false; Reason = $_.Exception.Message } }
}

$dependency = ([IO.File]::ReadAllText($DependencyManifest) | ConvertFrom-Json).dxc
$commit = [string]$dependency.commit
$archiveHash = ([string]$dependency.windows_sha256).ToLowerInvariant()
$uri = $null
if ($commit -notmatch '^[a-fA-F0-9]{40}$' -or $archiveHash -notmatch '^[a-f0-9]{64}$' -or
    ![Uri]::TryCreate([string]$dependency.windows_url, [UriKind]::Absolute, [ref]$uri) -or $uri.Scheme -notin @('https', 'http')) {
    throw 'Invalid pinned DXC dependency: expected commit, SHA-256 and an absolute HTTP(S) URL.'
}
[IO.Directory]::CreateDirectory($parent) | Out-Null
$lockPath = Join-Path $parent ($leaf + '.install.lock')
$lock = $null
$timer = [Diagnostics.Stopwatch]::StartNew()
while (!$lock) {
    try { $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
    catch [IO.IOException] {
        if ($timer.Elapsed.TotalSeconds -ge $lockTimeoutSeconds) {
            throw "Timed out after $lockTimeoutSeconds seconds waiting for the DXC install lock: $lockPath"
        }
        Start-Sleep -Milliseconds 100
    }
}
$staging = $null
$archive = $null
$backup = $null
$published = $false
$movedStaging = $false
try {
    if (Test-Path -LiteralPath $Destination) {
        $item = Get-Item -LiteralPath $Destination -Force
        if (!$item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'DXC Destination must be a real cache directory, not a file or reparse point.'
        }
    }
    $cache = Test-DxcCache $Destination $commit $archiveHash
    if ($cache.Valid) {
        Write-Output "DXC cache ready: $Destination"
        Write-Output $cache.Version
        return
    }
    Write-Output "Preparing pinned DXC ($($cache.Reason))"
    $identity = [Guid]::NewGuid().ToString('N')
    $staging = Join-Path $parent ('.' + $leaf + '.stage-' + $identity)
    $archive = Join-Path $parent ('.' + $leaf + '.download-' + $identity + '.zip')
    [IO.Directory]::CreateDirectory($staging) | Out-Null
    $oldProtocol = [Net.ServicePointManager]::SecurityProtocol
    try {
        # Process-only TLS compatibility for Windows PowerShell 5.1; restore it.
        [Net.ServicePointManager]::SecurityProtocol = $oldProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $uri.AbsoluteUri -OutFile $archive -TimeoutSec 120
    }
    finally { [Net.ServicePointManager]::SecurityProtocol = $oldProtocol }
    $digest = (Get-Sha256 $archive)
    if ($digest -ne $archiveHash) { throw "DXC archive SHA-256 mismatch: expected $archiveHash, received $digest" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $prefix = $staging.TrimEnd([char[]]'\/') + $separator
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\', '/')
            if ([IO.Path]::IsPathRooted($name) -or $name.Contains(':')) { throw 'DXC archive contains an unsafe rooted entry.' }
            $expanded = [IO.Path]::GetFullPath((Join-Path $staging $name))
            if (!$expanded.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'DXC archive entry escapes its staging directory.' }
        }
    }
    finally { $zip.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($archive, $staging)
    foreach ($relative in $requiredFiles) {
        if (!(Test-Path -LiteralPath (Join-Path $staging $relative) -PathType Leaf)) { throw "DXC archive lacks required $relative" }
    }
    $version = Get-DxcVersion $staging
    $fileHashes = [ordered]@{}
    foreach ($relative in $requiredFiles) {
        $fileHashes[$relative] = (Get-Sha256 (Join-Path $staging $relative))
    }
    [IO.File]::WriteAllText((Join-Path $staging 'revision'), $commit + "`n", $utf8)
    $receipt = [ordered]@{ commit = $commit; archive_sha256 = $archiveHash; files = $fileHashes }
    [IO.File]::WriteAllText((Join-Path $staging 'install-receipt.json'), ($receipt | ConvertTo-Json -Depth 4) + "`n", $utf8)
    $checked = Test-DxcCache $staging $commit $archiveHash
    if (!$checked.Valid) { throw "Staged DXC failed validation: $($checked.Reason)" }
    if ([IO.Directory]::Exists($Destination)) {
        $backup = Join-Path $parent ('.' + $leaf + '.previous-' + $identity)
        [IO.Directory]::Move($Destination, $backup)
    }
    try {
        [IO.Directory]::Move($staging, $Destination)
        $movedStaging = $true
        $staging = $null
        $checked = Test-DxcCache $Destination $commit $archiveHash
        if (!$checked.Valid) { throw "Published DXC failed validation: $($checked.Reason)" }
        $published = $true
    }
    catch {
        # A failed post-publication probe must not leave a cold partial cache.
        # Only remove Destination when this invocation actually published it.
        if ($movedStaging -and [IO.Directory]::Exists($Destination)) { [IO.Directory]::Delete($Destination, $true) }
        if ($backup -and [IO.Directory]::Exists($backup)) {
            [IO.Directory]::Move($backup, $Destination)
            $backup = $null
        }
        throw
    }
    Write-Output "DXC installed: $Destination"
    Write-Output $version
}
finally {
    # Only this invocation's staging/download paths may be removed. A backup is
    # discarded only after the replacement was completely validated.
    try {
        if ($archive -and [IO.File]::Exists($archive)) { [IO.File]::Delete($archive) }
        if ($staging -and [IO.Directory]::Exists($staging)) { [IO.Directory]::Delete($staging, $true) }
        if ($published -and $backup -and [IO.Directory]::Exists($backup)) { [IO.Directory]::Delete($backup, $true) }
    }
    finally { if ($lock) { $lock.Dispose() } }
}
