[CmdletBinding()]
param(
    [string]$DxcDirectory,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ([string]::IsNullOrWhiteSpace($DxcDirectory)) { $DxcDirectory = Join-Path $root '.ci-tools/dxc' }
$DxcDirectory = [IO.Path]::GetFullPath($DxcDirectory)
$setup = Join-Path $PSScriptRoot 'setup-windows-dxc.ps1'
$pinned = ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'dependencies.json')) | ConvertFrom-Json).dxc
$requiredFiles = @('bin/x64/dxc.exe', 'bin/x64/dxcompiler.dll', 'bin/x64/dxil.dll')
foreach ($relative in $requiredFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $DxcDirectory $relative) -PathType Leaf)) {
        throw "This test requires the real prepared DXC distribution: missing $relative in $DxcDirectory"
    }
}
$powershellExecutable = Join-Path $PSHOME 'powershell.exe'
if (!(Test-Path -LiteralPath $powershellExecutable)) { $powershellExecutable = Join-Path $PSHOME 'pwsh.exe' }
if (!(Test-Path -LiteralPath $powershellExecutable)) { throw 'Run this Windows regression under powershell.exe or pwsh.exe.' }
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('jengine-dxc-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $root 'build/windows-dxc-tests' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$sourcePrefix = $DxcDirectory.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
if ($OutputDirectory -eq $DxcDirectory -or $OutputDirectory.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Test logs must be outside the original DXC distribution.'
}
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$checks = New-Object 'Collections.Generic.List[object]'
$children = New-Object 'Collections.Generic.List[object]'
$failures = 0
$server = $null
$failure = $null
$sourceVersion = ''
$sourcePreserved = $false
$requests = 0

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

function Assert-Check([bool]$Condition, [string]$Name) {
    $checks.Add([ordered]@{ name = $Name; passed = $Condition })
    if ($Condition) { Write-Host "PASS $Name" }
    else { $script:failures++; Write-Host "FAIL $Name" }
}
function Get-Snapshot([string]$Directory) {
    $files = [ordered]@{}
    if (Test-Path -LiteralPath $Directory -PathType Container) {
        foreach ($file in (Get-ChildItem -LiteralPath $Directory -File -Recurse | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($Directory.TrimEnd([char[]]'\/').Length).TrimStart([char[]]'\/')
            $files[$relative] = [ordered]@{ length = $file.Length; modifiedUtcTicks = $file.LastWriteTimeUtc.Ticks;
                sha256 = (Get-Sha256 $file.FullName) }
        }
    }
    return ($files | ConvertTo-Json -Depth 4 -Compress)
}
function Write-Manifest([string]$Name, [string]$Commit, [string]$Hash, [string]$Url) {
    $path = Join-Path $fixtureRoot ($Name + '.json')
    $data = @{ dxc = @{ tag = $pinned.tag; commit = $Commit; windows_url = $Url; windows_sha256 = $Hash } }
    [IO.File]::WriteAllText($path, ($data | ConvertTo-Json -Depth 4), $utf8)
    return $path
}
function Start-Setup([string]$Cache, [string]$Manifest, [string]$Name) {
    # Arguments are filesystem paths (Windows forbids embedded quote characters).
    # Start-Process invokes PowerShell directly, not cmd.exe or another shell.
    $arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $setup +
        '" -Destination "' + $Cache + '" -DependencyManifest "' + $Manifest + '"'
    $stdout = Join-Path $OutputDirectory ($Name + '.stdout.log')
    $stderr = Join-Path $OutputDirectory ($Name + '.stderr.log')
    $process = Start-Process -FilePath $powershellExecutable -ArgumentList $arguments -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $process.Handle
    $record = @{ Process = $process; Stdout = $stdout; Stderr = $stderr; Name = $Name }
    $children.Add($record)
    return $record
}
function Wait-Setup($Record) {
    if (!$Record.Process.WaitForExit(90000)) {
        try { $Record.Process.Kill() } catch { }
        throw "DXC test child timed out: $($Record.Name)"
    }
    $Record.Process.WaitForExit()
    return $Record.Process.ExitCode
}
function Request-Count {
    $path = Join-Path $fixtureRoot 'requests'
    if (!(Test-Path -LiteralPath $path)) { return 0 }
    return [int]([IO.File]::ReadAllText($path))
}
function Set-ServerMode([string]$Mode) { [IO.File]::WriteAllText((Join-Path $fixtureRoot 'mode'), $Mode, $utf8) }
function Has-RealPayload([string]$Cache) {
    foreach ($relative in $requiredFiles) {
        $path = Join-Path $Cache $relative
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Sha256 $path) -ne
            (Get-Sha256 (Join-Path $DxcDirectory $relative))) { return $false }
    }
    return ([IO.File]::ReadAllText((Join-Path $Cache 'revision'))).Trim() -eq $pinned.commit
}

$sourceBefore = Get-Snapshot $DxcDirectory
try {
    # The fixture contains genuine already-verified Microsoft binaries, never a
    # substitute executable. It stays in Temp and is removed after the test.
    $payload = Join-Path $fixtureRoot 'payload'
    [IO.Directory]::CreateDirectory((Join-Path $payload 'bin/x64')) | Out-Null
    foreach ($relative in $requiredFiles) { Copy-Item -LiteralPath (Join-Path $DxcDirectory $relative) -Destination (Join-Path $payload $relative) }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = Join-Path $fixtureRoot 'fixture.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($payload, $archive, [IO.Compression.CompressionLevel]::Fastest, $false)
    $hash = (Get-Sha256 $archive)
    $server = Start-Job -ArgumentList $archive, $fixtureRoot -ScriptBlock {
        param($Archive, $Directory)
        $ErrorActionPreference = 'Stop'
        $encoding = [Text.UTF8Encoding]::new($false)
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        try {
            $listener.Start()
            [IO.File]::WriteAllText((Join-Path $Directory 'requests'), '0', $encoding)
            [IO.File]::WriteAllText((Join-Path $Directory 'mode'), 'normal', $encoding)
            $portTemporary = Join-Path $Directory 'port.tmp'
            [IO.File]::WriteAllText($portTemporary, [string]$listener.LocalEndpoint.Port, $encoding)
            [IO.File]::Move($portTemporary, (Join-Path $Directory 'port'))
            $bytes = [IO.File]::ReadAllBytes($Archive)
            $count = 0
            while (!(Test-Path -LiteralPath (Join-Path $Directory 'stop'))) {
                if (!$listener.Pending()) { Start-Sleep -Milliseconds 25; continue }
                $client = $listener.AcceptTcpClient()
                $stream = $null
                $reader = $null
                try {
                    $stream = $client.GetStream(); $stream.ReadTimeout = 5000; $stream.WriteTimeout = 10000
                    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 1024, $true)
                    $request = $reader.ReadLine()
                    do { $line = $reader.ReadLine() } while ($line -and $line.Length -gt 0)
                    if (!$request -or $request -notmatch '^GET /dxc.zip(?:\s|\?)') { throw 'Unexpected local fixture request.' }
                    $count++
                    [IO.File]::WriteAllText((Join-Path $Directory 'requests'), [string]$count, $encoding)
                    $mode = [IO.File]::ReadAllText((Join-Path $Directory 'mode')).Trim()
                    if ($mode -eq 'delay') { Start-Sleep -Milliseconds 1200 }
                    if ($mode -eq 'fail') {
                        $body = [Text.Encoding]::ASCII.GetBytes('Intentional fixture transport failure')
                        $header = "HTTP/1.1 503 Service Unavailable`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n"
                    }
                    else {
                        $body = $bytes
                        $header = "HTTP/1.1 200 OK`r`nContent-Type: application/zip`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n"
                    }
                    $headerBytes = [Text.Encoding]::ASCII.GetBytes($header)
                    $stream.Write($headerBytes, 0, $headerBytes.Length)
                    $stream.Write($body, 0, $body.Length); $stream.Flush()
                }
                catch { [IO.File]::AppendAllText((Join-Path $Directory 'server-errors.log'), $_.Exception.Message + "`n") }
                finally {
                    if ($reader) { $reader.Dispose() }
                    if ($stream) { $stream.Dispose() }
                    $client.Close()
                }
            }
        }
        finally { $listener.Stop() }
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (!(Test-Path -LiteralPath (Join-Path $fixtureRoot 'port'))) {
        if ($server.State -eq 'Failed' -or $timer.Elapsed.TotalSeconds -gt 20) { throw 'Loopback fixture server did not start.' }
        Start-Sleep -Milliseconds 50
    }
    $port = [IO.File]::ReadAllText((Join-Path $fixtureRoot 'port')).Trim()
    $url = "http://127.0.0.1:$port/dxc.zip"
    $manifest = Write-Manifest 'valid' $pinned.commit $hash $url
    $cache = Join-Path $fixtureRoot 'cold cache'
    $cold = Start-Setup $cache $manifest 'cold'
    $coldCode = Wait-Setup $cold
    Assert-Check ($coldCode -eq 0 -and (Has-RealPayload $cache) -and (Request-Count) -eq 1) 'cold install downloads real ZIP and publishes pinned executable plus both DLLs'
    if ($coldCode -ne 0) { throw "Cold install failed; see $($cold.Stderr)" }
    Assert-Check (Test-Path -LiteralPath (Join-Path $cache 'install-receipt.json')) 'cold install records verified archive and installed binary hashes'
    $sourceVersion = [IO.File]::ReadAllText($cold.Stdout).Trim()
    Assert-Check ($sourceVersion -match '(?i)(dxcompiler|dxc|dxil)' -and $sourceVersion -match '\d+\.\d+') 'actual copied DXC executes --version successfully'

    $beforeWarm = Get-Snapshot $cache; $beforeRequests = Request-Count
    Set-ServerMode 'fail'
    $warmCode = Wait-Setup (Start-Setup $cache $manifest 'warm-offline')
    Assert-Check ($warmCode -eq 0 -and (Request-Count) -eq $beforeRequests) 'healthy warm cache makes no HTTP request even when the server would fail'
    Assert-Check ((Get-Snapshot $cache) -eq $beforeWarm) 'warm validation preserves all cache bytes and modification times'

    Set-ServerMode 'normal'
    Remove-Item -LiteralPath (Join-Path $cache 'bin/x64/dxil.dll')
    $beforeRequests = Request-Count
    $repairCode = Wait-Setup (Start-Setup $cache $manifest 'missing-dll')
    Assert-Check ($repairCode -eq 0 -and (Has-RealPayload $cache) -and (Request-Count) -eq ($beforeRequests + 1)) 'a missing DXIL DLL is repaired from a fresh verified download'

    $damaged = Join-Path $cache 'bin/x64/dxil.dll'
    $damagedStream = [IO.File]::Open($damaged, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $damagedStream.WriteByte(0) } finally { $damagedStream.Dispose() }
    $beforeRequests = Request-Count
    $corruptCode = Wait-Setup (Start-Setup $cache $manifest 'changed-bytes')
    Assert-Check ($corruptCode -eq 0 -and (Has-RealPayload $cache) -and (Request-Count) -eq ($beforeRequests + 1)) 'changed DLL bytes are detected by the receipt and repaired'

    $beforeGood = Get-Snapshot $cache; $beforeRequests = Request-Count
    $badManifest = Write-Manifest 'wrong-hash' ('f' * 40) ('0' * 64) $url
    $badCode = Wait-Setup (Start-Setup $cache $badManifest 'wrong-hash')
    Assert-Check ($badCode -ne 0 -and (Request-Count) -eq ($beforeRequests + 1)) 'wrong archive SHA-256 fails instead of publishing downloaded bytes'
    Assert-Check ((Get-Snapshot $cache) -eq $beforeGood) 'failed replacement preserves the previous healthy cache byte-for-byte and mtime-for-mtime'
    $beforeRequests = Request-Count
    $retryCode = Wait-Setup (Start-Setup $cache $manifest 'retry-existing')
    Assert-Check ($retryCode -eq 0 -and (Request-Count) -eq $beforeRequests) 'retry after failed replacement immediately reuses the preserved healthy cache'

    $retryCache = Join-Path $fixtureRoot 'retry cache'
    Set-ServerMode 'fail'
    $beforeRequests = Request-Count
    $failedCode = Wait-Setup (Start-Setup $retryCache $manifest 'transport-failure')
    Assert-Check ($failedCode -ne 0 -and !(Test-Path -LiteralPath $retryCache) -and (Request-Count) -eq ($beforeRequests + 1)) 'real HTTP failure cannot leave a published partial cache'
    Set-ServerMode 'normal'
    $retryCode = Wait-Setup (Start-Setup $retryCache $manifest 'transport-retry')
    Assert-Check ($retryCode -eq 0 -and (Has-RealPayload $retryCache) -and (Request-Count) -eq ($beforeRequests + 2)) 'a later call retries a failed cold download and installs the real compiler'

    [IO.File]::WriteAllText((Join-Path $cache 'revision'), ('0' * 40), $utf8)
    $beforeRequests = Request-Count
    $revisionCode = Wait-Setup (Start-Setup $cache $manifest 'revision-repair')
    Assert-Check ($revisionCode -eq 0 -and (Has-RealPayload $cache) -and (Request-Count) -eq ($beforeRequests + 1)) 'stale revision marker forces pinned cache repair'

    Set-ServerMode 'delay'
    $parallelCache = Join-Path $fixtureRoot 'parallel cache'
    $beforeRequests = Request-Count
    $first = Start-Setup $parallelCache $manifest 'parallel-debug'
    $second = Start-Setup $parallelCache $manifest 'parallel-release'
    $firstCode = Wait-Setup $first; $secondCode = Wait-Setup $second
    Assert-Check ($firstCode -eq 0 -and $secondCode -eq 0 -and (Has-RealPayload $parallelCache)) 'simultaneous Debug and Release callers both receive a usable complete cache'
    Assert-Check ((Request-Count) -eq ($beforeRequests + 1)) 'exclusive install lock coalesces concurrent cold callers into one download'
    $leftovers = @(Get-ChildItem -LiteralPath $fixtureRoot -Force | Where-Object { $_.Name -match '^\..*\.(stage|download|previous)-' })
    Assert-Check ($leftovers.Count -eq 0) 'success and failure paths remove owned staging, download and backup artifacts'
}
catch { $failure = $_; $failures++; Write-Host ("FAIL DXC regression exception: " + $_.Exception.Message) }
finally {
    foreach ($child in $children) {
        try { if (!$child.Process.HasExited) { $child.Process.Kill(); $child.Process.WaitForExit() } } catch { }
        $child.Process.Dispose()
    }
    if ($server) {
        [IO.File]::WriteAllText((Join-Path $fixtureRoot 'stop'), 'stop', $utf8)
        try { Wait-Job -Job $server -Timeout 5 | Out-Null } catch { }
        if ($server.State -notin @('Completed', 'Failed', 'Stopped')) { Stop-Job -Job $server }
        Receive-Job -Job $server -ErrorAction SilentlyContinue | Out-Null
        Remove-Job -Job $server -Force
    }
    $requests = Request-Count
    $sourcePreserved = (Get-Snapshot $DxcDirectory) -eq $sourceBefore
    Assert-Check $sourcePreserved 'original prepared DXC distribution is unchanged'
    $summary = [ordered]@{ schemaVersion = 1; success = ($failures -eq 0); powershell = $PSVersionTable.PSVersion.ToString();
        sourceDirectory = $DxcDirectory; sourceVersion = $sourceVersion; sourcePreserved = $sourcePreserved;
        downloadRequests = $requests; checks = $checks.ToArray(); failure = $(if ($failure) { $failure.Exception.Message } else { '' }) }
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'summary.json'), ($summary | ConvertTo-Json -Depth 6), $utf8)
    # ZIP, real vendor copies, cache fixtures and server state never become CI artifacts.
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
}
if ($failures) { throw "DXC bootstrap regression failed $failures check(s); see $OutputDirectory" }
Write-Host "DXC bootstrap regression passed $($checks.Count) checks; HTTP downloads: $requests"
