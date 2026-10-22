#!/usr/bin/env pwsh
#Requires -Version 5.1
# Read the PE fingerprint (TimeDateStamp / SizeOfImage / CheckSum) of every
# installed Wreckfest2.exe, or of the one passed with -ExePath, and compare it
# against the measured build profiles in src/builds/steam_offsets.cpp.
#
# A build with no profile is not a dormant build: the mod resolves its addresses
# at startup through runtime discovery and runs on those. `pixi run test` maps
# every installed copy, runs the same discovery and prints what it finds.

[CmdletBinding()]
param([string]$ExePath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

if ($ExePath) {
    $exes = @($ExePath)
} else {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $gameExe = (Get-GameConfigs)['wreckfest-2'].Executable
    $exes = @(Find-AllGamePaths -GameId 'wreckfest-2' | ForEach-Object { Join-Path $_ $gameExe })
    if ($exes.Count -eq 0) { throw "Wreckfest 2 not found. Pass -ExePath explicitly." }
}

$offsetsFile = Join-Path $projectDir 'src/builds/steam_offsets.cpp'
$known = Select-String -Path $offsetsFile -Pattern '\{\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8})\s*\}'

foreach ($exe in $exes) {
    if (-not (Test-Path $exe)) { throw "EXE not found: $exe" }

    $bytes = [System.IO.File]::ReadAllBytes($exe)
    # Every offset below is taken from the file, so each one is range-checked
    # before it is used. Without this a truncated download or a text file passed
    # by mistake comes back as a raw ArgumentException naming an array index.
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a PE image (no MZ header): $exe"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset -gt ($bytes.Length - 4)) {
        throw "Not a PE image (PE header offset 0x{0:X} is outside the file): {1}" -f $peOffset, $exe
    }
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) { throw "Not a PE image: $exe" }

    $coff    = $peOffset + 4
    $optHdr  = $coff + 20
    # SizeOfImage sits at optional-header +56 and CheckSum at +64, so the header
    # has to reach at least +68.
    if (($optHdr + 68) -gt $bytes.Length) {
        throw "PE optional header is truncated: $exe"
    }
    $timeDateStamp = [BitConverter]::ToUInt32($bytes, $coff + 4)
    $sizeOfImage   = [BitConverter]::ToUInt32($bytes, $optHdr + 56)
    $checkSum      = [BitConverter]::ToUInt32($bytes, $optHdr + 64)

    $built = [DateTimeOffset]::FromUnixTimeSeconds($timeDateStamp).UtcDateTime
    Write-Host "EXE: $exe"
    Write-Host ("  TimeDateStamp 0x{0:X8}  ({1:yyyy-MM-dd HH:mm:ss} UTC)" -f $timeDateStamp, $built)
    Write-Host ("  SizeOfImage   0x{0:X8}" -f $sizeOfImage)
    Write-Host ("  CheckSum      0x{0:X8}" -f $checkSum)

    $matched = $false
    foreach ($k in $known) {
        $t = [Convert]::ToUInt32($k.Matches[0].Groups[1].Value, 16)
        $s = [Convert]::ToUInt32($k.Matches[0].Groups[2].Value, 16)
        $c = [Convert]::ToUInt32($k.Matches[0].Groups[3].Value, 16)
        if ($t -eq $timeDateStamp -and $s -eq $sizeOfImage -and $c -eq $checkSum) {
            Write-Host "  MATCH: measured profile at steam_offsets.cpp line $($k.LineNumber)." -ForegroundColor Green
            $matched = $true
        }
    }
    if (-not $matched) {
        Write-Host "  No measured profile matches this EXE. The mod resolves it through runtime discovery;" -ForegroundColor Yellow
        Write-Host "  'pixi run test' shows what discovery finds on it, and HeadTracking.log what a launch ran on."
    }
    Write-Host ""
}
