# Build on Windows with the existing Community shell CMake build and Kai layout.
[CmdletBinding()]
param(
    [string]$WorkDirectory = (Join-Path $PSScriptRoot '../../.build-rpc'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../../dist-rpc'),
    [string]$VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$pins = Get-Content (Join-Path $PSScriptRoot 'pins.json') -Raw | ConvertFrom-Json

function Invoke-Native([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Get-VerifiedArchive($Asset, [string]$Destination) {
    Invoke-WebRequest -Uri $Asset.url -OutFile $Destination -MaximumRetryCount 3 -RetryIntervalSec 5
    $actual = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Asset.sha256) { throw "Release checksum mismatch: $Destination" }
}
function Initialize-Checkout($Pin, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Invoke-Native git @('-C', $Destination, 'init', '--quiet')
    Invoke-Native git @('-C', $Destination, 'remote', 'add', 'origin', $Pin.repository)
    Invoke-Native git @('-C', $Destination, 'fetch', '--depth=1', 'origin', $Pin.commit)
    Invoke-Native git @('-C', $Destination, 'checkout', '--detach', 'FETCH_HEAD')
    $actual = & git -C $Destination rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $Pin.commit) { throw 'Source pin mismatch' }
}
function Find-AppRoot([string]$Directory) {
    $candidates = @(Get-ChildItem -LiteralPath $Directory -Filter stremio.exe -File -Recurse |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.DirectoryName 'portable_config') })
    if ($candidates.Count -ne 1) { throw "Expected one portable app in $Directory; found $($candidates.Count)" }
    return $candidates[0].DirectoryName
}

if (-not $IsWindows) { throw 'This build requires a Windows runner.' }
if (-not $VcpkgRoot) { $VcpkgRoot = 'C:\vcpkg' }
if (-not (Test-Path -LiteralPath (Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake'))) {
    throw 'The Windows build runner must provide vcpkg.'
}
foreach ($directory in @($WorkDirectory, $OutputDirectory)) {
    if (Test-Path -LiteralPath $directory) { throw "Use a fresh output directory: $directory" }
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
$work = (Resolve-Path $WorkDirectory).Path
$out = (Resolve-Path $OutputDirectory).Path
$sevenZip = (Get-Command 7z -ErrorAction Stop).Source
# A later upstream release needs new pins and another native identity check.
Invoke-Native git @('-C', $repo, 'diff', '--exit-code', $pins.kai_commit, '--', 'portable_config')
$customCommit = & git -C $repo rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify custom source commit' }

$kaiArchive = Join-Path $work 'kai-original.7z'
$nativeArchive = Join-Path $work 'community-original.7z'
Get-VerifiedArchive $pins.kai_portable $kaiArchive
Get-VerifiedArchive $pins.native_portable $nativeArchive
Invoke-Native $sevenZip @('x', $kaiArchive, "-o$(Join-Path $work 'kai-original')", '-y')
Invoke-Native $sevenZip @('x', $nativeArchive, "-o$(Join-Path $work 'community-original')", '-y')
$kaiRoot = Find-AppRoot (Join-Path $work 'kai-original')
$communityRoot = Find-AppRoot (Join-Path $work 'community-original')
$originalExeHash = (Get-FileHash (Join-Path $kaiRoot 'stremio.exe') -Algorithm SHA256).Hash
$communityExeHash = (Get-FileHash (Join-Path $communityRoot 'stremio.exe') -Algorithm SHA256).Hash
# The shipped EXEs differ in PE resources only. Require identical native code,
# data, imports and loader behavior before compiling the documented base.
Invoke-Native python @((Join-Path $PSScriptRoot 'inspect-native.py'), '--verify-resource-variant',
    (Join-Path $kaiRoot 'stremio.exe'), (Join-Path $communityRoot 'stremio.exe'))

$source = Join-Path $work 'native'
Initialize-Checkout $pins.native $source
Initialize-Checkout $pins.rpc (Join-Path $source 'deps/discord-rpc')
Initialize-Checkout $pins.rapidjson (Join-Path $source 'deps/discord-rpc/thirdparty/rapidjson')
Initialize-Checkout $pins.mpv (Join-Path $source 'deps/libmpv')
Invoke-Native python @((Join-Path $PSScriptRoot 'prepare.py'), '--source', $source)
Invoke-Native (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') @('-disableMetrics')
$vcpkgCommit = & git -C $VcpkgRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record vcpkg version' }
$build = Join-Path $work 'cmake-out'
Invoke-Native cmake @('-S', $source, '-B', $build, '-A', 'x64',
    "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake')",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
    '-DCLANG_FORMAT_SUFFIX=none', "-DKAI_RUNTIME_MPV=$(Join-Path $kaiRoot 'libmpv-2.dll')")
Invoke-Native cmake @('--build', $build, '--config', 'Release', '--target', 'stremio', 'kai_presence_tests', 'kai_rpc_reconnect_tests', '--parallel')
Invoke-Native ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--no-tests=error')
Invoke-Native python @((Join-Path $PSScriptRoot 'preserve-resources.py'),
    (Join-Path $kaiRoot 'stremio.exe'), (Join-Path $build 'Release/stremio.exe'))

# Keep every original runtime file, including Kai's MPV/SVP/Node/WebView2 files.
$packageName = 'Stremio-Kai-4.8.0-RPC-Portable-x64'
$package = Join-Path $out $packageName
Copy-Item -LiteralPath $kaiRoot -Destination $package -Recurse
# Apply the current GitHub configuration (the v4.8.0 hotfix) on the fresh package.
Copy-Item -Path (Join-Path $repo 'portable_config/*') -Destination (Join-Path $package 'portable_config') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $build 'Release/stremio.exe') -Destination (Join-Path $package 'stremio.exe') -Force
foreach ($relative in @('stremio.exe', 'stremio-runtime.exe', 'server.js', 'libmpv-2.dll',
                         'ffmpeg.exe', 'ffprobe.exe', 'portable_config/stremio-settings.ini',
                         'portable_config/EdgeWebView/msedgewebview2.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) {
        throw "Portable runtime is incomplete: missing $relative"
    }
}
# Verify that packaging preserved files outside the intentional EXE/config edits.
Get-ChildItem -LiteralPath $kaiRoot -File -Recurse | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($kaiRoot, $_.FullName)
    $repoConfig = Join-Path $repo $relative
    if ($relative -eq 'stremio.exe' -or (Test-Path -LiteralPath $repoConfig -PathType Leaf)) { return }
    $copy = Join-Path $package $relative
    if ((Get-FileHash -LiteralPath $_.FullName).Hash -ne (Get-FileHash -LiteralPath $copy).Hash) {
        throw "Unexpected runtime change: $relative"
    }
}

$notes = Join-Path $package 'CUSTOM-RPC-BUILD'
New-Item -ItemType Directory -Path $notes | Out-Null
Copy-Item (Join-Path $repo 'build/discord/README.md') (Join-Path $notes 'README.md')
Copy-Item (Join-Path $repo 'tests/DISCORD-DESKTOP-CHECKLIST.md') (Join-Path $notes 'DISCORD-DESKTOP-CHECKLIST.md')
Copy-Item (Join-Path $PSScriptRoot 'pins.json') $notes
Copy-Item (Join-Path $source 'deps/discord-rpc/LICENSE') (Join-Path $notes 'DISCORD-RPC-LICENSE.txt')
Copy-Item (Join-Path $source 'deps/discord-rpc/thirdparty/rapidjson/license.txt') (Join-Path $notes 'RAPIDJSON-LICENSE.txt')
@{
    kai_base_commit = $pins.kai_commit
    custom_commit = $customCommit
    native_source_commit = $pins.native.commit
    original_exe_sha256 = $originalExeHash.ToLowerInvariant()
    native_identity = 'Community 5.0.21 code/data match; all original Kai PE resources preserved'
    custom_exe_sha256 = (Get-FileHash (Join-Path $package 'stremio.exe')).Hash.ToLowerInvariant()
    vcpkg_commit = $vcpkgCommit
    built_at_utc = [DateTime]::UtcNow.ToString('o')
    rpc_payload_tests = 'passed on Windows'
    discord_desktop_acceptance = 'NOT TESTED: requires a signed-in Discord desktop client and another user'
} | ConvertTo-Json | Set-Content (Join-Path $notes 'build-manifest.json') -Encoding utf8

# Include the exact prepared native sources and the Kai build recipe alongside
# the portable. Runtime binaries remain in the original portable distribution.
$sourceBundle = Join-Path $work 'source-bundle'
New-Item -ItemType Directory -Path $sourceBundle | Out-Null
Copy-Item (Join-Path $repo 'LICENSE') (Join-Path $sourceBundle 'KAI-LICENSE.txt')
Invoke-Native git @('-C', $repo, 'archive', '--format=zip', "--output=$(Join-Path $sourceBundle 'kai-source-and-build-recipe.zip')", 'HEAD')
Push-Location $source
try {
    Invoke-Native $sevenZip @('a', '-tzip', (Join-Path $sourceBundle 'prepared-native-source.zip'),
        'src', 'CMakeLists.txt', 'stremio.rc', 'vcpkg.json', 'tests',
        'images', 'deps/discord-rpc', 'deps/libmpv/x86_64/include', 'deps/libmpv/x86_64/mpv.lib',
        '-xr!.git')
} finally { Pop-Location }
Copy-Item (Join-Path $notes 'build-manifest.json') $sourceBundle
Push-Location $sourceBundle
try { Invoke-Native $sevenZip @('a', '-tzip', (Join-Path $out 'Stremio-Kai-RPC-Sources.zip'), '*') }
finally { Pop-Location }
Push-Location $out
try { Invoke-Native $sevenZip @('a', '-tzip', '-mx=5', "$packageName.zip", $packageName) }
finally { Pop-Location }
Get-ChildItem -LiteralPath $out -Filter '*.zip' -File | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
} | Set-Content (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Portable build: $(Join-Path $out "$packageName.zip")"
Write-Host 'Discord desktop visual acceptance is still required; see the included checklist.'
