#!/usr/bin/env pwsh
#Requires -Version 5.1
# Build and run the unit tests in their own build directory so the normal
# build/ tree never carries a test binary. -NoRun builds them only, for
# render-config, whose binary is one of them.

[CmdletBinding()]
# Both configurations by default. Debug alone is what the suite used to run,
# which meant no assertion in the repo had ever executed the code as it is
# actually shipped: the .asi users install is Release, where the optimiser is
# free to expose the aliasing precondition in RotateBasis, an uninitialised
# read, or a floating-point contraction that the 1e-5 tolerances would catch.
# The whole suite takes about two seconds, so there is nothing to save by
# picking one.
param(
    [ValidateSet('Release', 'Debug', 'Both')][string]$Config = 'Both',
    [switch]$NoRun,
    [ValidateSet('all', 'unit', 'differential')][string]$Suite = 'all'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$buildDir   = Join-Path $projectDir 'build-tests'

cmake -S $projectDir -B $buildDir -A x64 -DWF2_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

if ($Config -eq 'Both') { $configs = @('Debug', 'Release') } else { $configs = @($Config) }

# The runtime discovery test resolves every installed copy of the game from
# scratch and checks it against its measured profile. None installed (CI, or a
# contributor without the game) leaves the variable empty and that check unrun.
Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
$gameExe = (Get-GameConfigs)['wreckfest-2'].Executable
$installed = @(Find-AllGamePaths -GameId 'wreckfest-2' | ForEach-Object { Join-Path $_ $gameExe })
$env:WF2_GAME_EXES = $installed -join ';'
if ($installed.Count -eq 0) {
    Write-Host 'No installed Wreckfest 2 found: the real-image discovery check will not run.' -ForegroundColor Yellow
} else {
    $installed | ForEach-Object { Write-Host "Real-image discovery check: $_" }
}

foreach ($c in $configs) {
    Write-Host "--- $c ---" -ForegroundColor Cyan

    cmake --build $buildDir --config $c --target wf2_tests
    if ($LASTEXITCODE -ne 0) { throw "Test build failed for $c ($LASTEXITCODE)" }
    if ($NoRun) { continue }

    $labels = @{ all = @(); unit = @('-LE', 'differential'); differential = @('--no-tests=error', '-L', 'differential') }[$Suite]
    ctest --test-dir $buildDir -C $c --output-on-failure @labels
    if ($LASTEXITCODE -ne 0) { throw "Tests failed for $c ($LASTEXITCODE)" }
}

if ($NoRun) {
    Write-Host "Tests built ($($configs -join ', '))" -ForegroundColor Green
} else {
    Write-Host "All tests passed ($($configs -join ', '))" -ForegroundColor Green
}
