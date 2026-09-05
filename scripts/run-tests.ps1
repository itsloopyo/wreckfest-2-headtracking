#!/usr/bin/env pwsh
#Requires -Version 5.1
# Build and run the unit tests in their own build directory so the normal
# build/ tree never carries a test binary.

[CmdletBinding()]
# Both configurations by default. Debug alone is what the suite used to run,
# which meant no assertion in the repo had ever executed the code as it is
# actually shipped: the .asi users install is Release, where the optimiser is
# free to expose the aliasing precondition in RotateBasis, an uninitialised
# read, or a floating-point contraction that the 1e-5 tolerances would catch.
# The whole suite takes about two seconds, so there is nothing to save by
# picking one.
param([ValidateSet('Release', 'Debug', 'Both')][string]$Config = 'Both')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$buildDir   = Join-Path $projectDir 'build-tests'

cmake -S $projectDir -B $buildDir -A x64 -DWF2_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

if ($Config -eq 'Both') { $configs = @('Debug', 'Release') } else { $configs = @($Config) }

foreach ($c in $configs) {
    Write-Host "--- $c ---" -ForegroundColor Cyan

    cmake --build $buildDir --config $c --target wf2_tests
    if ($LASTEXITCODE -ne 0) { throw "Test build failed for $c ($LASTEXITCODE)" }

    ctest --test-dir $buildDir -C $c --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed for $c ($LASTEXITCODE)" }
}

Write-Host "All tests passed ($($configs -join ', '))" -ForegroundColor Green
