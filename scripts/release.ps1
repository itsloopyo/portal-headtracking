#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Release workflow for Portal Head Tracking.

.DESCRIPTION
    Runs unattended end to end. The command-line invocation is the
    authorization; there is no second gate and nothing here reads stdin.
    Preconditions (semver, main branch, clean tree, tag absent) are the safety
    net, and any of them failing exits 1 with a one-line diagnostic.

    1. Validate semver + git state.
    2. Regenerate CHANGELOG.md from conventional commits (via
       cameraunlock-core/powershell/ReleaseWorkflow.psm1).
    3. Bump the version in src/version.h, scripts/install.cmd and
       CMakeLists.txt.
    4. Build the x86 release.
    5. Commit the version + changelog as "Release v<version>".
    6. Create annotated tag v<version> and push it; CI picks up the tag and
       publishes the GitHub release artifacts.

.EXAMPLE
    pixi run release 1.0.0
    pixi run release patch
#>
param(
    [Parameter(Position = 0)]
    [string]$Version = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir      = $PSScriptRoot
$projectDir     = Split-Path -Parent $scriptDir
$installCmdPath = Join-Path $projectDir 'scripts\install.cmd'
$cmakePath      = Join-Path $projectDir 'CMakeLists.txt'
$changelogPath  = Join-Path $projectDir 'CHANGELOG.md'

Import-Module (Join-Path $projectDir 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force
# src/version.h is canonical; ModVersion.psm1 owns its path, its regex and both
# the read and the write, so the packager, the nightly shim and this script
# cannot disagree about what the version is.
Import-Module (Join-Path $scriptDir 'ModVersion.psm1') -Force

$versionPath = Get-ModVersionPath -ProjectRoot $projectDir

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
#
# Called only once the preconditions below have passed: it writes a commit, and
# a commit must never land off the back of an invocation that then aborts for a
# dirty tree, the wrong branch, an existing tag, or no version argument at all.
function Sync-CoreNotices {
    & git -C $projectDir diff --quiet -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run." }
    # Reset first: a &-invoked .ps1 sets $LASTEXITCODE only when it calls exit,
    # and sync-core-notices.ps1 returns normally on every path but one. Without
    # the reset the check below reads whatever git the callee last ran left
    # behind - a stale non-zero from an internal `rev-parse --verify` that found
    # nothing would abort a perfectly good release.
    $global:LASTEXITCODE = 0
    & (Join-Path $projectDir 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $projectDir
    if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
    & git -C $projectDir diff --quiet -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) {
        & git -C $projectDir commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
        if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
        Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
    }
}

# The whole safety net, gathered in one place so it is readable as a list of
# what must hold before anything is written. Exits 1 rather than throwing: a
# failed precondition is a normal outcome of this script, not a crash.
function Assert-ReleasePreconditions {
    param(
        [Parameter(Mandatory = $true)][string]$Tag,
        [Parameter(Mandatory = $true)][string]$NewVersion
    )

    # git -C $projectDir on every call, not bare git. $projectDir comes from
    # $PSScriptRoot, so the rest of the script works from any directory; a bare
    # git here reads the cwd instead, and `powershell -File scripts\release.ps1`
    # run from another clone checks that clone's branch, tree and tags while the
    # writes below still land in this one.
    $branch = git -C $projectDir rev-parse --abbrev-ref HEAD
    if ($branch -ne 'main') {
        Write-Host "Must be on main branch to release (currently on '$branch')" -ForegroundColor Red
        exit 1
    }
    Push-Location $projectDir
    try {
        if (-not (Test-CleanGitStatus)) {
            Write-Host 'Working tree has uncommitted changes - commit or stash first.' -ForegroundColor Red
            git -C $projectDir status --short
            exit 1
        }
        if (Test-GitTagExists -Tag $Tag) {
            Write-Host "Tag '$Tag' already exists." -ForegroundColor Red
            exit 1
        }
    } finally {
        Pop-Location
    }
    # Test-GitTagExists reads local refs only and never fetches, so a tag cut
    # from another machine or by CI is invisible here. Without this check the
    # run gets all the way to Publish-ReleaseTag, pushes main with a duplicate
    # release commit on it, and only then fails on the tag push - and main
    # cannot be un-pushed.
    & git -C $projectDir ls-remote --exit-code --tags origin "refs/tags/$Tag" | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Tag '$Tag' already exists on origin. Someone has already released it." -ForegroundColor Red
        exit 1
    }
    # 2 is ls-remote's "--exit-code matched nothing", which is the answer we
    # want. Every other code (128 for no origin, no network or bad credentials)
    # means the question was not answered, and carrying on would put us back at
    # a pushed main with a tag that cannot be pushed.
    if ($LASTEXITCODE -ne 2) {
        Write-Host "Could not reach origin to check whether tag '$Tag' exists (git ls-remote exit $LASTEXITCODE)." -ForegroundColor Red
        exit 1
    }
    # The version files are rewritten as one all-or-nothing set, but that set is
    # written AFTER the CHANGELOG has been regenerated. Match every pattern in
    # every file here, while the tree is still clean, so a missing CMakeLists.txt
    # or a renamed MOD_VERSION literal refuses the release instead of stranding a
    # regenerated CHANGELOG with no bump and no tag.
    try {
        Resolve-VersionLiteralSet -Files (Get-VersionFileSet -NewVersion $NewVersion) | Out-Null
    } catch {
        Write-Host "Cannot bump the version files: $($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
}

# src/version.h is the canonical version: the packager reads it to name the
# ZIPs, and .github/workflows/release.yml reads the same regex out of the same
# file. The other two copies are written here so they cannot drift away from
# it - install.cmd's MOD_VERSION, which the installer records in the user's
# .headtracking-state.json, and CMakeLists.txt's project version.
# One all-or-nothing set rather than three writes: every pattern is matched in
# every file before the first byte is written. The CHANGELOG has already been
# regenerated by the time this runs, so a file that had drifted off its pattern
# used to abort mid-bump and leave the tree dirty with two of three files moved.
function Get-VersionFileSet {
    [OutputType([hashtable[]])]
    param([Parameter(Mandatory = $true)][string]$NewVersion)

    return @(
        @{ Path = $versionPath; Rewrites = (Get-ModVersionRewrites -Version $NewVersion) }
        @{ Path = $installCmdPath; Rewrites = @(
            @{ Pattern = 'set "MOD_VERSION=[^"]+"'; Replacement = "set `"MOD_VERSION=$NewVersion`"" }) }
        @{ Path = $cmakePath; Rewrites = @(
            @{ Pattern = '(?m)^(project\([^)]*VERSION\s+)\d+\.\d+\.\d+'; Replacement = "`${1}$NewVersion" }) }
    )
}

function Update-VersionFiles {
    param([Parameter(Mandatory = $true)][string]$NewVersion)

    Write-Host "Updating src/version.h, scripts/install.cmd and CMakeLists.txt to $NewVersion..." -ForegroundColor Cyan
    Update-VersionLiteralSet -Files (Get-VersionFileSet -NewVersion $NewVersion)
}

# Tag + push. Every step is checked, because git failures are exit codes rather
# than exceptions and $ErrorActionPreference does not see them. The push order
# is load-bearing: pushing the tag first, or pushing it after a main push that
# was rejected (a non-fast-forward, a protected branch), lands a release tag on
# a commit that is not on main - and the tag push carries the commit's objects
# with it, so CI happily builds and publishes from it.
function Publish-ReleaseTag {
    param([Parameter(Mandatory = $true)][string]$Tag)

    Write-Host "Creating tag $Tag..." -ForegroundColor Cyan
    git -C $projectDir tag -a $Tag -m "Release $Tag"
    if ($LASTEXITCODE -ne 0) { throw "Could not create tag $Tag." }
    git -C $projectDir push origin main
    if ($LASTEXITCODE -ne 0) { throw "Pushing main failed - the local tag $Tag was NOT pushed. Fix the push, then run: git push origin main; git push origin $Tag" }
    git -C $projectDir push origin $Tag
    if ($LASTEXITCODE -ne 0) { throw "Pushing tag $Tag failed - main is pushed, so CI has not been triggered. Re-run: git push origin $Tag" }
}

Write-Host ''
Write-Host '=== Portal Head Tracking Release ===' -ForegroundColor Cyan
Write-Host ''

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host 'Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>' -ForegroundColor Red
    exit 1
}

if ($Version -eq 'nightly') {
    # Reset first, for the same reason as Sync-CoreNotices: release-nightly.ps1
    # returns rather than exiting, so an unreset $LASTEXITCODE would report the
    # exit code of the last native command that ran inside it.
    $global:LASTEXITCODE = 0
    & (Join-Path $scriptDir 'release-nightly.ps1')
    exit $LASTEXITCODE
}

# Rejected on the argument the user typed, not on what it resolves to.
# Resolve-ReleaseVersion accepts X.Y.Z-prerelease, so a bare `-notmatch` after
# the call told the user their argument was fine and then that the result was
# not. A prerelease has no path through this script - the tag, the ZIP names and
# release.yml's tag-vs-file check are all X.Y.Z - and `pixi run release nightly`
# is the pre-release channel.
if ($Version -notmatch '^(major|minor|patch)$' -and $Version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host "'$Version' is not major, minor, patch or X.Y.Z. For a pre-release build use: pixi run release nightly" -ForegroundColor Red
    exit 1
}

$current = Get-ModVersion -ProjectRoot $projectDir

try {
    $Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}


$tag = "v$Version"

Assert-ReleasePreconditions -Tag $tag -NewVersion $Version

Sync-CoreNotices

Write-Host "Current version: $current" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ''

# Step 1 - changelog first, because it is the gate that can fail. Generating it
# before mutating any version file means an abort leaves the tree clean rather
# than stranding a half-applied bump with no tag.
# ArtifactPaths must list the same paths as release.yml's artifact-paths input:
# that workflow generates the GitHub release notes from the same commit range,
# and a wider list here produces a CHANGELOG entry that names commits the
# release notes for the same tag leave out. scripts/ as a whole is too wide -
# only the two installer wrappers ship, so a packager or dev-deploy edit is not
# a user-visible change.
Write-Host 'Generating CHANGELOG from commits...' -ForegroundColor Cyan
try {
    New-ChangelogFromCommits -ChangelogPath $changelogPath -Version $Version `
        -ArtifactPaths @('src/', 'cameraunlock-core', 'vendor/', 'scripts/install.cmd', 'scripts/uninstall.cmd')
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# Step 2 - mirror the new version into the three files that carry it
Update-VersionFiles -NewVersion $Version

# Step 3 - build
Write-Host 'Building release (x86)...' -ForegroundColor Cyan
Push-Location $projectDir
try {
    pixi run build-release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
} finally {
    Pop-Location
}

# Step 4 - commit named files only, so build artifacts cannot sweep in
Write-Host 'Committing version + changelog...' -ForegroundColor Cyan
git -C $projectDir add $versionPath $changelogPath $installCmdPath $cmakePath
if ($LASTEXITCODE -ne 0) { throw 'git add failed - the version bump was not staged.' }
git -C $projectDir diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host 'No version/changelog changes - tagging existing HEAD.' -ForegroundColor Yellow
} else {
    git -C $projectDir commit -m "Release v$Version"
    if ($LASTEXITCODE -ne 0) { throw 'Commit failed' }
}

# Step 5 - tag + push
Publish-ReleaseTag -Tag $tag

Write-Host ''
Write-Host "Release $tag pushed - CI will build and publish artifacts." -ForegroundColor Green
