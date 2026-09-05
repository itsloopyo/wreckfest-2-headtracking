#Requires -Version 5.1
# Fully unattended. `pixi run release <major|minor|patch|nightly|X.Y.Z>` is the
# authorization - there is no confirmation gate. Preconditions (on main, clean
# tree, tag absent, valid semver) are the safety net; any failure exits
# non-zero with a one-line diagnostic.
param(
    [string]$Version,
    # Ship a release even when there are no user-facing commits since the last
    # tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

$pixiPath       = Join-Path $projectDir "pixi.toml"
$changelogPath  = Join-Path $projectDir "CHANGELOG.md"
$installCmdPath = Join-Path $projectDir "scripts/install.cmd"
$cmakePath      = Join-Path $projectDir "CMakeLists.txt"
$manifestPath   = Join-Path $projectDir "launcher-manifest.json"

# 1. Resolve + validate version against the canonical source (pixi.toml).
$pixiContent = Get-Content $pixiPath -Raw
if ($pixiContent -notmatch '(?m)^version\s*=\s*"([^"]+)"') {
    throw "No version field found in $pixiPath"
}
$currentVersion = $matches[1]
$newVersion = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
if (-not (Test-SemanticVersion -Version $newVersion)) {
    throw "Resolved version '$newVersion' is not valid semver (X.Y.Z)."
}
Write-Host "Releasing v$newVersion (current v$currentVersion)" -ForegroundColor Cyan

# 2. Preconditions - fail fast, never prompt.
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne "main") { throw "Releases must run on 'main' (currently on '$branch')." }
if (-not (Test-CleanGitStatus)) { throw "Working tree is not clean. Commit or stash first." }
if (Test-GitTagExists -Tag "v$newVersion") { throw "Tag v$newVersion already exists." }

# 2b. Tests, BEFORE anything is mutated. Nothing else on the release path runs
#     them: build.yml skips the "Release v..." commit by design, and the
#     reusable release workflow only packages - so without this the exact tree
#     that gets tagged and published is the one tree never tested. Here rather
#     than next to the build for the same reason step 3 sits where it does: a
#     failure has to leave a clean tree, not a half-applied version bump.
Write-Host "Running tests..." -ForegroundColor Cyan
pixi run test
if ($LASTEXITCODE -ne 0) { throw "Tests failed; aborting release." }

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
#
# This runs AFTER the preconditions above, not before them: it is the first
# step that writes to the repo, and it used to commit on `main` before the
# script had established it was on main, that the tree was clean, or that the
# version argument was even valid - including on the `nightly` path, which
# never gets this far now.
& (Join-Path $projectDir 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $projectDir
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $projectDir diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $projectDir commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

# 3. Generate the changelog from commits since the last tag. This is the gate
#    that aborts when there are no user-facing commits, so run it BEFORE
#    mutating any version files or building - a failure here then leaves a
#    clean tree instead of stranding a half-applied version bump with no tag.
Write-Host "Generating CHANGELOG..." -ForegroundColor Cyan
$hasExistingTags = git tag -l 2>$null
$changelogHasThisVersion = (Test-Path $changelogPath) -and
    ((Get-Content -Raw $changelogPath) -match [regex]::Escape("## [$newVersion]"))
if (-not $hasExistingTags -and -not (Test-Path $changelogPath)) {
    $date = Get-Date -Format 'yyyy-MM-dd'
    "# Changelog`n`n## [$newVersion] - $date`n`nFirst release.`n" | Set-Content $changelogPath
    Write-Host "  Wrote initial CHANGELOG.md" -ForegroundColor Gray
} elseif ($changelogHasThisVersion) {
    Write-Host "  CHANGELOG.md already has a [$newVersion] section" -ForegroundColor Gray
} else {
    # Reached on the FIRST tagged release too, when a pre-release CHANGELOG.md
    # already exists. The old shape skipped generation entirely in that case, so
    # the release commit bumped every version file and shipped a changelog whose
    # newest entry was still the pre-release one. New-ChangelogFromCommits
    # handles "no previous tag" by taking every commit, which is what is wanted.
    try {
        New-ChangelogFromCommits -ChangelogPath $changelogPath -Version $newVersion | Out-Null
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $newVersion
    }
}

# 4. Bump the canonical version, then mirror it into every derived copy.
$pixiContent = $pixiContent -replace '(?m)^(version\s*=\s*")[^"]+(")', "`${1}$newVersion`${2}"
$pixiContent | Set-Content $pixiPath -NoNewline

$cmakeContent = Get-Content $cmakePath -Raw
if ($cmakeContent -notmatch 'project\(Wreckfest2HeadTracking VERSION [0-9]+\.[0-9]+\.[0-9]+') {
    throw "No project VERSION found in $cmakePath"
}
$cmakeContent = $cmakeContent -replace '(project\(Wreckfest2HeadTracking VERSION )[0-9]+\.[0-9]+\.[0-9]+', "`${1}$newVersion"
$cmakeContent | Set-Content $cmakePath -NoNewline

# -Raw + a digits-only regex keeps install.cmd's CRLF endings intact (a .cmd
# silently fails on Windows if rewritten LF).
$installContent = Get-Content $installCmdPath -Raw
if ($installContent -notmatch '(?m)^set "MOD_VERSION=[0-9]+\.[0-9]+\.[0-9]+"') {
    throw "No MOD_VERSION line found in $installCmdPath"
}
$installContent = $installContent -replace '(?m)^(set "MOD_VERSION=)[0-9]+\.[0-9]+\.[0-9]+(")', "`${1}$newVersion`${2}"
$installContent | Set-Content $installCmdPath -NoNewline

$manifestContent = Get-Content $manifestPath -Raw
if ($manifestContent -notmatch '(?m)^\s*"version"\s*:\s*"[0-9]+\.[0-9]+\.[0-9]+"') {
    throw "No version field found in $manifestPath"
}
$manifestContent = $manifestContent -replace '(?m)^(\s*"version"\s*:\s*")[0-9]+\.[0-9]+\.[0-9]+(")', "`${1}$newVersion`${2}"
$manifestContent | Set-Content $manifestPath -NoNewline

# 5. Release-config build.
Write-Host "Building release..." -ForegroundColor Cyan
pixi run build
if ($LASTEXITCODE -ne 0) { throw "Build failed; aborting release." }

# 6. Commit the version bump + changelog. "Release v..." matches the build.yml
#    skip guard so CI doesn't double-build this commit.
git add $pixiPath $changelogPath $installCmdPath $manifestPath $cmakePath
if ($LASTEXITCODE -ne 0) { throw "git add failed." }
git commit -m "Release v$newVersion"
if ($LASTEXITCODE -ne 0) { throw "git commit failed." }

# 7 + 8. Annotated tag, then push commits + tag (triggers release.yml).
New-ReleaseTag -Version $newVersion -Message "Release v$newVersion" -Branch "main"

Write-Host "Released v$newVersion. CI release workflow will publish the ZIPs." -ForegroundColor Green
