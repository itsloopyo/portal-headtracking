#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Bump the vendored Ultimate ASI Loader under vendor/ultimate-asi-loader/.
# ============================================================================
# Usage:    pixi run update-deps
# Frequency: manual. The vendored copy is the install-time source of truth, so
# the dev runs this when they want a fresh upstream bump, reviews the diff and
# commits it. build / package / release never refresh, and neither does CI.
#
# Portal is a 32-bit Source Engine game, so the x86 asset
# (Ultimate-ASI-Loader.zip; the x64 build ships as Ultimate-ASI-Loader_x64.zip)
# is the right one. Upstream ships the loader inside a wrapper zip, but
# install.cmd consumes the raw dinput8.dll, copied to <game>\bin\winmm.dll:
# Source loads tier0.dll from bin\ with an altered search path, so a proxy at
# the game root is never consulted, and bin\tier0.dll and bin\engine.dll both
# import winmm.dll, while nothing Portal ships imports xinput. So the zip is
# staged in TEMP and only the DLL is vendored.
#
# The extracted DLL is NOT vendored as it comes: the x86 build embeds
# binkw32.dll (RAD Game Tools, proprietary), wndmode.dll (VEG / menopem, no
# licence) and vorbisfile.dll (Xiph.Org) as RCDATA resources, and committing it
# as-is would redistribute all three. strip-loader-payload.ps1 zeroes them
# before the copy is hashed and committed. Never skip that step.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = $PSScriptRoot
$projectDir = Split-Path -Parent $scriptDir

$modulePath = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $modulePath)) {
    throw "ModLoaderSetup.psm1 not found at $modulePath. Run 'git submodule update --init --recursive' to fetch cameraunlock-core."
}
Import-Module $modulePath -Force
# Update-VersionLiteral: the same CRLF- and encoding-preserving rewrite the
# release path uses, so install.cmd's ASI_LOADER_VERSION is bumped by the one
# implementation rather than by a second copy of it here.
Import-Module (Join-Path $scriptDir 'ModVersion.psm1') -Force

$installCmdPath = Join-Path $projectDir 'scripts/install.cmd'
$vendorDir   = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorDll   = Join-Path $vendorDir 'dinput8.dll'
$readmePath  = Join-Path $vendorDir 'README.md'
$licensePath = Join-Path $vendorDir 'LICENSE'

# Upstream ships the loader inside a wrapper zip; only dinput8.dll is vendored.
function Expand-LoaderDll {
    param(
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [Parameter(Mandatory = $true)][string]$AssetName,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $entry = $archive.Entries | Where-Object { $_.Name -ieq 'dinput8.dll' } | Select-Object -First 1
        if (-not $entry) {
            throw "$AssetName has no dinput8.dll (entries: $($archive.Entries.Name -join ', '))"
        }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $Destination, $true)
    } finally {
        $archive.Dispose()
    }
}

# The provenance note that travels with the vendored binary: where it came
# from, what it hashed to before and after the strip, and why it is not stock
# upstream. Regenerated only when the DLL actually changed, so an unchanged
# upstream leaves the tree clean.
function New-VendorReadme {
    [OutputType([string])]
    param(
        [Parameter(Mandatory = $true)]$Meta,
        [Parameter(Mandatory = $true)][string]$UpstreamSha,
        [Parameter(Mandatory = $true)][string]$VendoredSha
    )

    return (@(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader (x86), the install-time source of truth.',
        'install.cmd copies it straight out of here and never reaches out to the network.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($Meta.Tag)``",
        "- Commit: ``$($Meta.CommitSha)``",
        "- Asset: ``$($Meta.AssetName)``",
        "- Asset URL: $($Meta.AssetUrl)",
        "- Upstream dinput8.dll SHA-256: ``$UpstreamSha``",
        "- Vendored dinput8.dll SHA-256: ``$VendoredSha`` (after the strip below)",
        "- Fetched at: $($Meta.FetchedAt)",
        '',
        'install.cmd copies `dinput8.dll` to <game>\bin\winmm.dll, the proxy slot Portal',
        'loads ASI plugins through.',
        '',
        '## Modified: third-party payload stripped',
        '',
        'The upstream x86 loader carries three complete third-party DLLs as RCDATA resources,',
        'so that a user who renames it over one of those libraries still gets the original',
        'exports, plus the ini template one of them reads:',
        '',
        '- `binkw32.dll` - RAD Game Tools, Inc., Bink and Smacker 1.994i. Proprietary',
        '  middleware licensed per title; we have no right to redistribute it.',
        '- `wndmode.dll` - DirectX Windower Embedded v2.3, (C) 2008 VEG, (C) 2004 menopem.',
        '  No licence accompanies it.',
        '- `vorbisfile.dll` - Xiph.Org, BSD-3-Clause. Redistributable only with its notice.',
        '',
        '`scripts/strip-loader-payload.ps1` zeroes all three, and the windower ini template,',
        'before the file is committed. The loader code, its imports, relocations and appended',
        'PDB are byte-identical to upstream. Outside `.rsrc`, the same script removes the',
        'upstream Authenticode certificate table and recomputes the optional-header CheckSum:',
        'zeroing a resource invalidates the signature, and Windows reports a file whose',
        'signature no longer verifies as tampered rather than as unsigned. The vendored copy',
        'is unsigned. Nothing in this mod can reach the stripped resources - the two library',
        "payloads are keyed off the loader's own filename, and we deploy it as `winmm.dll`,",
        'while the windower needs a `wndmode.ini` we never ship. MIT permits the modification;',
        'it is recorded here and in THIRD-PARTY-NOTICES.md so this copy is not mistaken for',
        'stock upstream.'
    ) -join "`n")
}

if (-not (Test-Path $vendorDir)) {
    New-Item -ItemType Directory -Path $vendorDir -Force | Out-Null
}

$stageDir = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
try {
    $meta = Update-VendoredLoader `
        -Name 'ultimate-asi-loader' `
        -OutputDir $stageDir `
        -OutputFileName 'Ultimate-ASI-Loader.zip' `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader\.zip$' `
        -LicenseName 'license'

    $stagedDll = Join-Path $stageDir 'dinput8.dll'
    Expand-LoaderDll -ArchivePath $meta.LocalPath -AssetName $meta.AssetName -Destination $stagedDll

    $upstreamSha = (Get-FileHash -LiteralPath $stagedDll -Algorithm SHA256).Hash.ToLower()

    $AsiLoaderVersionPattern = 'set "ASI_LOADER_VERSION=[^"]*"'
    $AsiLoaderVersionLiteral = "set `"ASI_LOADER_VERSION=$($meta.Tag -replace '^v', '')`""

    # Strip before the hash the idempotency check and the README use, so both see
    # the file that actually gets committed.
    # One call. Re-running -VerifyOnly here would re-read the file the line
    # above just wrote, with the same detector that zeroed every payload it
    # found, so it can only ever agree with itself. package-release.ps1's gate
    # is the one that adds something: it re-checks the copy committed under
    # vendor/, which is a different file from this one.
    Write-Host "    stripping the loader's embedded third-party DLLs..." -ForegroundColor DarkGray
    & (Join-Path $scriptDir 'strip-loader-payload.ps1') -Path $stagedDll

    $dllSha = (Get-FileHash -LiteralPath $stagedDll -Algorithm SHA256).Hash.ToLower()

    # Idempotency: an unchanged upstream must leave the tree clean. Rewriting
    # README.md unconditionally would churn its fetched_at on every run and
    # produce a commit that says nothing.
    $unchanged = (Test-Path $vendorDll) -and (Test-Path $readmePath) -and (Test-Path $licensePath) -and
        ((Get-FileHash -LiteralPath $vendorDll -Algorithm SHA256).Hash.ToLower() -eq $dllSha)

    if ($unchanged) {
        Write-Host "    no change (dinput8.dll sha256=$($dllSha.Substring(0,12))... matches on-disk vendor copy)" -ForegroundColor DarkGray
    } else {
        # Checked before the first write, not relied on to fail one of them.
        # Update-VendoredLoader resolves the licence through fallbacks whose last
        # one only warns, so a licence that never arrived would otherwise throw
        # partway through and leave vendor/ holding some mixture of a new binary,
        # a new README and an old licence. Whichever write went first, the diff
        # left behind describes a release that was never assembled.
        $stagedLicense = Join-Path $stageDir 'LICENSE'
        if (-not (Test-Path -LiteralPath $stagedLicense)) {
            throw "Update-VendoredLoader produced no LICENSE for $($meta.Tag). The loader is MIT and cannot be redistributed without it."
        }

        # install.cmd's literal is proved present before the first write too,
        # for the same reason as the licence above: Update-VersionLiteral throws
        # when its pattern matches nothing, and running that after the copies
        # would leave vendor/ holding a refresh the tree does not otherwise
        # record. Resolve-VersionLiteralSet matches without writing.
        $loaderVersionRewrite = @(@{
            Path = $installCmdPath
            Rewrites = @(@{ Pattern = $AsiLoaderVersionPattern; Replacement = $AsiLoaderVersionLiteral })
        })
        Resolve-VersionLiteralSet -Files $loaderVersionRewrite | Out-Null

        Copy-Item -LiteralPath $stagedDll -Destination $vendorDll -Force
        Copy-Item -LiteralPath $stagedLicense -Destination $licensePath -Force

        $readme = New-VendorReadme -Meta $meta -UpstreamSha $upstreamSha -VendoredSha $dllSha
        # BOM-less UTF8 with LF endings, matching package-release.ps1: PS 5.1's
        # `Set-Content -Encoding utf8` writes a BOM and terminates the file with
        # CRLF, which makes every regenerated README a mixed-ending diff.
        [IO.File]::WriteAllText($readmePath, $readme + "`n",
                                (New-Object System.Text.UTF8Encoding $false))

        Write-Host "  tag=$($meta.Tag) dinput8.dll sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
    }

    # Outside the branch above: install.cmd records the loader build in the
    # user's state file and is the only copy of this version outside vendor/.
    # $unchanged compares the DLL hash and nothing else, so a run that finds
    # upstream unchanged would never notice this literal had been edited or
    # emptied. Update-VersionLiteral rewrites only when the value differs, so an
    # already-correct install.cmd is left byte-identical.
    Update-VersionLiteral -Path $installCmdPath `
        -Pattern $AsiLoaderVersionPattern -Replacement $AsiLoaderVersionLiteral

} finally {
    Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader checked against upstream. Review and commit any diff under vendor/." -ForegroundColor Green
