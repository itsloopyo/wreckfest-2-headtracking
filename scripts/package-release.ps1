#!/usr/bin/env pwsh
#Requires -Version 5.1
# Build the release ZIP from whatever is committed under vendor/ and freshly
# built under build/Release. Never touches the network, and never refreshes
# vendor/ - that is `pixi run update-deps`, a deliberate dev action with a
# commit attached.
#
#   Wreckfest2HeadTracking-v{version}-installer.zip  (GitHub, install.cmd)
#
# The ZIP carries LICENSE, THIRD-PARTY-NOTICES.md and licenses/ - the mod is MIT
# and the components linked into the .asi need their notices beside the binary.
#
# There is deliberately NO -nexus.zip stage here, and no Nexus page. This mod's
# payload is a proxy DLL (version.dll) plus Wreckfest2HeadTracking.asi, both
# beside Wreckfest2.exe at the game root. A mod manager deploys into one
# fixed subtree under the game folder, so nothing it installs reaches the root,
# and a Nexus archive would install "successfully" while the loader never
# loads. Installer-only is the correct shape - do not helpfully add a Nexus
# stage back. scripts/release-nightly.ps1 passes -NoNexusZip for the same
# reason.

[CmdletBinding()]
param(
    # Passed through to Copy-SharedBundle in CI, where the submodule is
    # already synced by the workflow checkout.
    [switch]$NoRefresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$buildDir   = Join-Path $projectDir 'build/Release'
$releaseDir = Join-Path $projectDir 'release'

$asiName = 'Wreckfest2HeadTracking.asi'
$asi = Join-Path $buildDir $asiName
if (-not (Test-Path $asi)) { throw "Built .asi not found at $asi. Run 'pixi run build' first." }

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# pixi.toml is the canonical version; release.ps1 mirrors it into
# CMakeLists.txt, install.cmd and launcher-manifest.json.
$pixiContent = Get-Content -Raw (Join-Path $projectDir 'pixi.toml')
if ($pixiContent -notmatch '(?m)^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"') {
    throw 'Could not read version from pixi.toml'
}
$version = $Matches[1]

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

$stage = Join-Path $env:TEMP "wf-ht-stage-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage | Out-Null

# Everything from here to the archive is inside a try/finally, because every
# step between is a `throw` waiting for a missing vendored file, a vendored DLL
# whose hash does not match the one recorded beside it, or a missing licence -
# and each of those used to leave four megabytes of staging behind in TEMP, once
# per attempt, for the user to find later or not at all.
try {

# Launcher manifest at the ZIP root, stamped with the real release version
# (the committed copy stays at 0.0.0).
$manifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $manifestPath)) { throw "launcher-manifest.json not found at $manifestPath" }
$manifest = Get-Content -Raw $manifestPath | ConvertFrom-Json
$manifest.mod_info.version = $version
# UTF-8 with no BOM, written through .NET rather than Set-Content: Windows
# PowerShell's -Encoding UTF8 emits a BOM, and RFC 8259 says a JSON document
# must not be sent with one. This file is the contract between the package and
# the launcher, and a parser entitled to reject the BOM would reject the whole
# release rather than one field.
[System.IO.File]::WriteAllText(
    (Join-Path $stage 'launcher-manifest.json'),
    ($manifest | ConvertTo-Json -Depth 10),
    (New-Object System.Text.UTF8Encoding $false))

$plugins = New-Item -ItemType Directory -Path (Join-Path $stage 'plugins')
Copy-Item -Force $asi (Join-Path $plugins.FullName $asiName)

$vendorSrc = Join-Path $projectDir 'vendor/ultimate-asi-loader'
foreach ($required in @('dinput8.dll', 'README.md', 'LICENSE')) {
    if (-not (Test-Path (Join-Path $vendorSrc $required))) {
        throw "vendor/ultimate-asi-loader/$required missing. Run 'pixi run update-deps' and commit."
    }
}

# The vendored loader is a third-party binary this release ships to users, and
# until now the only thing checked about it was that a file of that name
# existed. update-deps records the hash it fetched; this is where that record is
# worth anything. A mismatch means the bytes on disk are not the bytes anyone
# reviewed - a bad merge, a corrupted checkout, or something worse - and the
# release stops rather than signing off on them.
$vendorDll = Join-Path $vendorSrc 'dinput8.dll'
$vendorReadme = Get-Content -Raw (Join-Path $vendorSrc 'README.md')
if ($vendorReadme -notmatch '(?m)^\s*-\s*dinput8\.dll SHA-256:\s*`([0-9a-fA-F]{64})`') {
    throw "vendor/ultimate-asi-loader/README.md records no dinput8.dll SHA-256. Run 'pixi run update-deps' and commit."
}
$expectedDllSha = $Matches[1].ToLowerInvariant()
# Hashed through .NET rather than Get-FileHash. In Windows PowerShell 5.1 that
# cmdlet is a function defined in Microsoft.PowerShell.Utility's script module,
# not part of the engine, and on the CI runner it came back CommandNotFound
# while every binary cmdlet in this script resolved normally. A release gate
# must not depend on module autoloading in whatever shell pixi hands it.
$actualDllSha = [System.BitConverter]::ToString(
    [System.Security.Cryptography.SHA256]::Create().ComputeHash(
        [System.IO.File]::ReadAllBytes($vendorDll))).Replace('-', '').ToLowerInvariant()
if ($actualDllSha -ne $expectedDllSha) {
    throw ("vendor/ultimate-asi-loader/dinput8.dll does not match the SHA-256 recorded in its " +
           "README (expected $expectedDllSha, got $actualDllSha). Refusing to package an " +
           "unverified loader binary. Re-run 'pixi run update-deps' and review the diff.")
}
Write-Host "  vendored loader verified (sha256=$($actualDllSha.Substring(0, 12))...)" -ForegroundColor DarkGray
$vendorDst = New-Item -ItemType Directory -Path (Join-Path $stage 'vendor/ultimate-asi-loader') -Force
Copy-Item -Force (Join-Path $vendorSrc '*') $vendorDst.FullName -Recurse

Copy-Item -Force (Join-Path $projectDir 'scripts/install.cmd') $stage
Copy-Item -Force (Join-Path $projectDir 'scripts/uninstall.cmd') $stage
Copy-SharedBundle -StagingDir $stage -NoRefresh:$NoRefresh

foreach ($f in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -Force (Join-Path $projectDir $f) $stage
}

# Every component compiled into a binary this ZIP ships, with its notice as its
# own file. MinHook (BSD-2-Clause, whose licence file also carries the separate
# copyright for the Hacker Disassembler Engine it embeds) and cameraunlock-core
# (MIT) are compiled into the .asi. injector (zlib) and miniz (MIT) are compiled
# into the vendored dinput8.dll by the loader's Ultimate-ASI-Loader-x64 target,
# so shipping that binary redistributes them too - easy to miss, because neither
# appears anywhere in this repository's own sources. Their texts come from
# cameraunlock-core/legal/, the canonical copy THIRD-PARTY-NOTICES.md is written
# from, so the shipped file and the notice cannot drift apart.
$licenseDst = New-Item -ItemType Directory -Path (Join-Path $stage 'licenses') -Force
$linkedLicenses = @{
    'minhook-LICENSE.txt'           = Join-Path $projectDir 'extern/minhook/LICENSE.txt'
    'cameraunlock-core-LICENSE.txt' = Join-Path $projectDir 'cameraunlock-core/LICENSE'
    'injector-LICENSE.txt'          = Join-Path $projectDir 'cameraunlock-core/legal/injector.txt'
    'miniz-LICENSE.txt'             = Join-Path $projectDir 'cameraunlock-core/legal/miniz.txt'
}
foreach ($name in $linkedLicenses.Keys) {
    $src = $linkedLicenses[$name]
    if (-not (Test-Path $src)) {
        throw "Licence missing: $src. Its code ships inside this release, so its notice must ship with it."
    }
    Copy-Item -Force $src (Join-Path $licenseDst.FullName $name)
}

$installerZip = Join-Path $releaseDir "Wreckfest2HeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $installerZip -Force
Write-Host "Built $installerZip" -ForegroundColor Green

}
finally {
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
}
