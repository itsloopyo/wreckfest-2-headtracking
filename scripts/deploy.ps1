#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev loop: copy the built .asi and the vendored ASI loader into the game folder.

[CmdletBinding()]
param(
    # Positional so `deploy.ps1 "D:\Games\Wreckfest 2"` works, matching the
    # positional game path install.cmd takes. Named -Config stays available.
    [Parameter(Position = 0)][string]$GamePath,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

if (-not $GamePath) {
    $GamePath = Find-GamePath -GameId 'wreckfest-2'
}
if (-not $GamePath -or -not (Test-Path $GamePath)) {
    throw "Wreckfest 2 not found. Pass -GamePath explicitly."
}

$asi = Join-Path $projectDir "build/$Config/Wreckfest2HeadTracking.asi"
if (-not (Test-Path $asi)) { throw "Build output not found: $asi. Run 'pixi run build' first." }

$loader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
if (-not (Test-Path $loader)) { throw "Vendored ASI loader missing. Run 'pixi run update-deps'." }

Copy-Item $asi (Join-Path $GamePath 'Wreckfest2HeadTracking.asi') -Force
Write-Host "  deployed Wreckfest2HeadTracking.asi" -ForegroundColor DarkGray

# Wreckfest2.exe statically imports both DINPUT8.dll and VERSION.dll, and
# neither is a KnownDLL, so a game-local copy of either wins the safe DLL
# search order. VERSION.dll is the one used: it proxies only the four
# GetFileVersionInfo entry points, while DINPUT8.dll carries the gamepad
# API this game genuinely calls, and a proxy that gets that wrong costs a
# racing game its wheel.
$loaderTarget = Join-Path $GamePath 'version.dll'
if (-not (Test-Path $loaderTarget)) {
    Copy-Item $loader $loaderTarget -Force
    Write-Host "  deployed version.dll (Ultimate ASI Loader)" -ForegroundColor DarkGray
} else {
    Write-Host "  version.dll already present, left alone" -ForegroundColor DarkGray
}

Write-Host "Deployed to $GamePath" -ForegroundColor Green
