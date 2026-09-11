#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Stage and zip the two release archives for Portal Head Tracking.
# ============================================================================
# Usage: pixi run package   (runs build-release first)
#
# Consumes whatever is committed under vendor/ - it never refreshes the loader
# and never reaches the network, so CI packages exactly what the repo holds.
# Bumping the loader is `pixi run update-deps` plus a commit, done by hand.
#
# No prompts, no confirmations: this runs unattended under `pixi run` and from
# CI. Every precondition fails fast with a non-zero exit instead.
# ============================================================================

# ValidateSet, not a bare string: $Configuration is concatenated straight into
# bin\<Configuration>, so anything else is a path the packager was never meant
# to read from. Same set deploy.ps1 accepts.
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path "$PSScriptRoot\.."
$binDir   = Join-Path $repoRoot "bin\$Configuration"
$outDir   = Join-Path $repoRoot 'release'

Import-Module (Join-Path $repoRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ModVersion.psm1') -Force

$modName = 'PortalHeadTracking'
$modSlug = 'portal-headtracking'
$asi     = "$modName.asi"

$vendorDir = Join-Path $repoRoot 'vendor\ultimate-asi-loader'
$vendorDll = Join-Path $vendorDir 'dinput8.dll'

# A staging directory is rebuilt from scratch every run: a leftover tree from an
# aborted package would otherwise be zipped along with this one's files.
function New-StageDirectory {
    [OutputType([string])]
    param([Parameter(Mandatory = $true)][string]$Path)

    if (Test-Path $Path) { Remove-Item $Path -Recurse -Force }
    New-Item -ItemType Directory -Path $Path | Out-Null
    return $Path
}

# LICENSE and THIRD-PARTY-NOTICES.md are not optional documentation: the
# vendored loader's MIT and cameraunlock-core's MIT both require their notice
# to accompany the binaries in each ZIP, and each ZIP is a binary distribution
# in its own right. A missing one fails the package rather than quietly
# producing a release that cannot be distributed. $Optional is the reader's
# documentation - useful, not licence-bearing - so it is copied when present.
function Copy-NoticeDocuments {
    param(
        [Parameter(Mandatory = $true)][string]$StageDir,
        [string[]]$Optional = @()
    )

    foreach ($doc in @('LICENSE', 'THIRD-PARTY-NOTICES.md')) {
        $src = Join-Path $repoRoot $doc
        if (-not (Test-Path $src)) { throw "Missing required notice file: $src" }
        Copy-Item $src $StageDir
    }
    foreach ($doc in $Optional) {
        $src = Join-Path $repoRoot $doc
        if (Test-Path $src) { Copy-Item $src $StageDir }
    }
}

# Zip a finished stage and take the stage down, so `release\` holds archives
# and never a half-built tree that the next run would have to reason about.
function Write-StageArchive {
    param(
        [Parameter(Mandatory = $true)][string]$StageDir,
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
    Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipPath
    Remove-Item $StageDir -Recurse -Force
    Write-Host ("Packaged {0,-10} {1}" -f "${Label}:", $ZipPath) -ForegroundColor Green
}

# --- Everything the ZIPs take out of the repo is checked here, before a single
# --- staging file is written, so a failure leaves no half-built tree behind.

if (-not (Test-Path $binDir)) {
    throw "Build output not found at $binDir. Run: pixi run build-release"
}

$asiPath = Join-Path $binDir $asi
if (-not (Test-Path $asiPath)) { throw "Missing build output: $asiPath" }

$version = Get-ModVersion -ProjectRoot $repoRoot

# Both ZIPs are named from src/version.h, but the number the installer writes
# into the user's .headtracking-state.json is install.cmd's MOD_VERSION literal,
# and release.ps1 is the only thing that ever mirrors one into the other. Any
# package built outside a release bump - CI on every push, release-nightly, a
# local `pixi run package` - would otherwise pair a v0.2.0 filename with an
# installer that records 0.0.0, and the launcher reads that file to decide
# whether an update is due.
$installCmd = Join-Path $repoRoot 'scripts\install.cmd'
$modVersionMatch = [regex]::Match([IO.File]::ReadAllText($installCmd), 'set "MOD_VERSION=([^"]*)"')
if (-not $modVersionMatch.Success) {
    throw "No MOD_VERSION literal in $installCmd - the installer cannot record the version it deployed."
}
if ($modVersionMatch.Groups[1].Value -ne $version) {
    throw "install.cmd MOD_VERSION is '$($modVersionMatch.Groups[1].Value)' but src/version.h is '$version'. Bump both with: pixi run release <major|minor|patch|X.Y.Z>"
}

if (-not (Test-Path $vendorDll)) {
    throw "Missing vendored loader: $vendorDll. Run: pixi run update-deps"
}

# The installer ZIP redistributes that binary, and the upstream x86 loader
# carries binkw32.dll (RAD Game Tools, proprietary), wndmode.dll and
# vorbisfile.dll as RCDATA resources. None of the three is ours to ship, so a
# loader that still has them never reaches a release. See
# vendor/ultimate-asi-loader/README.md.
& (Join-Path $PSScriptRoot 'strip-loader-payload.ps1') -Path $vendorDll -VerifyOnly

# dinput8.dll and its LICENSE are both mandatory: the loader is MIT, and its
# license has to travel with the binary. README.md is the vendoring provenance
# note and is nice to have.
$vendorRequired = @('dinput8.dll', 'LICENSE') | ForEach-Object { Join-Path $vendorDir $_ }
foreach ($f in $vendorRequired) {
    if (-not (Test-Path $f)) { throw "Missing vendored loader file: $f" }
}
$vendorReadme = Join-Path $vendorDir 'README.md'

# Portal is a 32-bit process, so a 64-bit .asi or loader is never loaded: the
# install succeeds, the game starts, and the mod does nothing. update-deps pins
# the x86 upstream asset, but `pixi run strip-loader` exists for a loader
# copied in by hand and bypasses that pin, and a build configured for the wrong
# platform produces an x64 .asi with the right filename.
$archCheck = Join-Path $repoRoot 'cameraunlock-core\scripts\check-loader-arch.ps1'
foreach ($pe in @($asiPath, $vendorDll)) {
    & $archCheck -Path $pe -ExpectedArch x86
    if ($LASTEXITCODE -ne 0) {
        throw "$pe is not a 32-bit PE (check-loader-arch exit $LASTEXITCODE). Portal is a 32-bit process and will never load it."
    }
}

# cmd.exe needs CRLF: an LF-only batch file mis-parses labels and parenthesised
# blocks, and both wrappers are built out of those, so the failure is an
# installer that exits having done nothing. .gitattributes marks these
# eol=crlf, but the packager copies from the WORKING TREE, which is what a file
# written by an LF-defaulting tool ships as. Checked rather than repaired,
# because a wrapper that reached this point with LF endings was edited by
# something that will do it again. Any bare LF fails the check, not just an
# absence of CRLF: a wrapper that is CRLF everywhere except the one block an
# editor rewrote mis-parses in exactly the same way, and "contains a CRLF
# somewhere" would have shipped it.
$wrappers = @('install.cmd', 'uninstall.cmd') | ForEach-Object { Join-Path $repoRoot "scripts\$_" }
foreach ($w in $wrappers) {
    if ([IO.File]::ReadAllText($w) -match "(?<!`r)`n") {
        throw "$w has LF line endings. cmd.exe needs CRLF - run: unix2dos $w"
    }
}

# Both wrappers resolve the game by GAME_ID through the games.json that
# Copy-SharedBundle stages into shared/, so an id with no entry there ships a
# release whose installer exits 1 on the user's first run, having done nothing.
# This is the one place that sees the wrapper and the catalogue together.
$catalogue = (Get-Content -LiteralPath (Join-Path $repoRoot 'cameraunlock-core\data\games.json') -Raw |
              ConvertFrom-Json).games.PSObject.Properties.Name
foreach ($w in $wrappers) {
    $idMatch = [regex]::Match([IO.File]::ReadAllText($w), 'set "GAME_ID=([^"]*)"')
    if (-not $idMatch.Success) { throw "No GAME_ID literal in $w." }
    if ($catalogue -notcontains $idMatch.Groups[1].Value) {
        throw "$w sets GAME_ID=$($idMatch.Groups[1].Value), which has no entry in cameraunlock-core/data/games.json. The shipped installer would fail to resolve the game on every run."
    }
}

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

# ---------- Installer ZIP (GitHub Release) ----------
$installerStage = New-StageDirectory (Join-Path $outDir "$modSlug-installer-stage")

# Mod payload deployed to <game>\bin by install.cmd.
$pluginsDir = Join-Path $installerStage 'plugins'
New-Item -ItemType Directory -Path $pluginsDir | Out-Null
Copy-Item $asiPath $pluginsDir

# Vendored Ultimate ASI Loader: install-time source of truth, copied to
# <game>\bin\winmm.dll by install.cmd. Consumed exactly as committed - the
# refresh is `pixi run update-deps`, a manual action with a commit attached.
$vendorStage = Join-Path $installerStage 'vendor\ultimate-asi-loader'
New-Item -ItemType Directory -Path $vendorStage | Out-Null
Copy-Item $vendorRequired -Destination $vendorStage
if (Test-Path $vendorReadme) { Copy-Item $vendorReadme $vendorStage }

Copy-Item $wrappers -Destination $installerStage

# shared/ is mandatory wherever install.cmd ships: both wrappers resolve the
# game through shared/find-game.ps1 on every run, even when handed an explicit
# path. A ZIP without it fails on the user's first invocation.
Copy-SharedBundle -StagingDir $installerStage

Copy-NoticeDocuments -StageDir $installerStage -Optional @('README.md', 'CHANGELOG.md')

Write-StageArchive -StageDir $installerStage -Label 'installer' `
    -ZipPath (Join-Path $outDir "$modName-v$version-installer.zip")

# ---------- Nexus ZIP (extract to game folder) ----------
# Deploy subtree only: the .asi lands in <game>\bin. Nexus users supply their
# own ASI loader (winmm.dll), so no vendored loader is bundled here.
#
# The .asi statically links cameraunlock-core (MIT), and MIT requires its
# notice to travel with a binary distribution. This ZIP is a distribution in
# its own right - it is what Nexus hands the user - so the notices ship in it,
# not only in the installer ZIP. They sit at the ZIP root so that extracting
# over the game folder does not scatter them into the engine's own directory.
$nexusStage = New-StageDirectory (Join-Path $outDir "$modSlug-nexus-stage")
$nexusBin = Join-Path $nexusStage 'bin'
New-Item -ItemType Directory -Path $nexusBin -Force | Out-Null
Copy-Item $asiPath $nexusBin

Copy-NoticeDocuments -StageDir $nexusStage -Optional @('README.md')

Write-StageArchive -StageDir $nexusStage -Label 'nexus' `
    -ZipPath (Join-Path $outDir "$modName-v$version-nexus.zip")
