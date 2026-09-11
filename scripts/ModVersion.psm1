#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# The mod's canonical version, read and written in one place.
# ============================================================================
# src/version.h is the single source of truth. package-release.ps1 names the
# ZIPs from it, release-nightly.ps1 labels the nightly with it, release.ps1
# mirrors it into scripts/install.cmd and CMakeLists.txt, and release.yml
# re-reads it to check the pushed tag agrees.
#
# Those four readers used to be three different parsers - Get-Content plus
# -match in release.ps1 and Select-String in the other two - each carrying its
# own copy of the path and the regex. The parse happens here now, through the
# same Get-ProjectVersion that cameraunlock-core's release-mod.yml calls, so a
# local read and a CI read are the same code and cannot disagree.
# ============================================================================

Set-StrictMode -Version Latest

# No -Force: a forced nested import REMOVES ReleaseWorkflow from the calling
# script's scope before re-importing it privately here, which would take
# Copy-SharedBundle and Resolve-ReleaseVersion away from the packager and the
# release script that had just imported them.
Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'cameraunlock-core\powershell\ReleaseWorkflow.psm1')

# Byte-identical to release.yml's version-pattern input. CI fails the release
# when the tag and this capture disagree, so the two must stay the same string.
#
# Anchored to the #define at the start of a line, not to the macro name alone.
# Get-ProjectVersion returns the FIRST match and Get-ModVersionRewrites rewrites
# EVERY match, so an unanchored pattern reads a comment that mentions the macro
# and then rewrites that comment too. release.yml carries this same string, so
# its tag-vs-file check reads the same wrong value and agrees with the tag.
$script:VersionStringPattern = '(?m)^#define\s+HEADTRACKING_VERSION_STRING\s+"([^"]+)"'

# The same match, split so the replacement can put back the exact `#define` and
# whitespace it found. The read pattern above has to stay byte-identical to
# release.yml's input, and a capture group added for the write would change it.
$script:VersionStringWritePattern = '(?m)^(#define\s+HEADTRACKING_VERSION_STRING\s+)"[^"]*"'

# The shape Resolve-ReleaseVersion writes, asserted on both the read and the
# write. version.h is a hand-editable header and its value does not stay a
# display string: it becomes the release ZIP filenames and the git tag CI
# builds from. A leading 'v' passes the capture regex above, so without this the
# first thing that objects is release.yml's tag-vs-file comparison, by which
# point the tag is pushed.
$script:VersionValuePattern = '^\d+\.\d+\.\d+(-[a-zA-Z0-9.]+)?$'

function Get-ModVersionPath {
    [OutputType([string])]
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    return (Join-Path $ProjectRoot 'src\version.h')
}

function Get-ModVersion {
    [OutputType([string])]
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    $path = Get-ModVersionPath -ProjectRoot $ProjectRoot
    $version = Get-ProjectVersion -Source regex -Path $path -Pattern $script:VersionStringPattern
    if ($version -notmatch $script:VersionValuePattern) {
        throw "HEADTRACKING_VERSION_STRING in $path is '$version', which is not X.Y.Z[-prerelease]. The packager and the release tag are both named from it."
    }
    return $version
}

# Rewrite version literals across one or more files as a single unit. Each
# entry in $Files is @{ Path = ...; Rewrites = @(@{ Pattern; Replacement }) };
# the caller supplies the pattern that locates each literal, so a rename of the
# literal fails loudly here rather than writing nothing and letting the release
# carry a stale version through to the tag.
#
# Every pattern in every file is matched, and every file that will change is
# proved writable, before ANY file is written. release.ps1 mirrors one version
# into three files that have to move together, and it does so after the CHANGELOG
# has already been regenerated: a bump that rewrote the first two and threw on
# the third left a dirty tree carrying a half-applied version, no tag, and
# nothing to say which half had landed.
#
# Resolve-VersionLiteralSet is that match-and-prove phase on its own, so
# release.ps1 can run it as a precondition and refuse a release whose version
# files are missing or have drifted off their patterns, before it writes
# anything at all.
function Resolve-VersionLiteralSet {
    [OutputType([hashtable[]])]
    param([Parameter(Mandatory = $true)][hashtable[]]$Files)

    # Byte-level read rather than Get-Content or ReadAllText: install.cmd is CRLF
    # and the packager refuses to ship it otherwise, so a rewrite must leave the
    # file's existing line endings exactly as it found them, and version.h is a
    # C++ header that Visual Studio saves with a UTF-8 BOM. ReadAllText consumes
    # a BOM and WriteAllText writes none, so a round trip through those two drops
    # it and shows up as a whole-file diff on the next bump.
    $pending = @()
    foreach ($file in $Files) {
        if (-not (Test-Path -LiteralPath $file.Path)) {
            throw "Version file not found: $($file.Path)"
        }
        $raw     = [System.IO.File]::ReadAllBytes($file.Path)
        $hasBom  = $raw.Length -ge 3 -and $raw[0] -eq 0xEF -and $raw[1] -eq 0xBB -and $raw[2] -eq 0xBF
        $start   = $(if ($hasBom) { 3 } else { 0 })
        $original = [System.Text.Encoding]::UTF8.GetString($raw, $start, $raw.Length - $start)
        $updated  = $original
        foreach ($rewrite in $file.Rewrites) {
            if (-not [regex]::IsMatch($updated, $rewrite.Pattern)) {
                throw "Pattern '$($rewrite.Pattern)' matched nothing in $($file.Path) - the version was not updated."
            }
            $updated = [regex]::Replace($updated, $rewrite.Pattern, $rewrite.Replacement)
        }
        if ($updated -ne $original) {
            # Prove the file can be written before any file is written, with the
            # same access WriteAllText will take. Matching every pattern is not
            # enough on its own: a read-only attribute, or a CMake configure
            # holding CMakeLists.txt open, fails the third write after the first
            # two have landed, which is the half-applied bump this set exists to
            # prevent.
            ([System.IO.File]::Open($file.Path, [System.IO.FileMode]::Open,
                                    [System.IO.FileAccess]::Write,
                                    [System.IO.FileShare]::Read)).Dispose()
            $pending += @{ Path = $file.Path; Text = $updated; Bom = $hasBom }
        }
    }
    # Comma before the cast: PowerShell unrolls an empty array to $null on
    # return, and an unchanged version is the common case here, so a caller
    # reading .Count on the result would break under Set-StrictMode.
    return ,([hashtable[]]$pending)
}

function Update-VersionLiteralSet {
    param([Parameter(Mandatory = $true)][hashtable[]]$Files)

    foreach ($write in (Resolve-VersionLiteralSet -Files $Files)) {
        [System.IO.File]::WriteAllText($write.Path, $write.Text,
                                       (New-Object System.Text.UTF8Encoding $write.Bom))
    }
}

function Update-VersionLiterals {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][hashtable[]]$Rewrites
    )

    Update-VersionLiteralSet -Files @(@{ Path = $Path; Rewrites = $Rewrites })
}

function Update-VersionLiteral {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Replacement
    )

    Update-VersionLiterals -Path $Path -Rewrites @(@{ Pattern = $Pattern; Replacement = $Replacement })
}

# The four literals src/version.h carries, as a rewrite list rather than a
# write, so release.ps1 can put them in the same all-or-nothing set as
# install.cmd's MOD_VERSION and CMakeLists.txt's project version.
function Get-ModVersionRewrites {
    [OutputType([hashtable[]])]
    param([Parameter(Mandatory = $true)][string]$Version)

    if ($Version -notmatch $script:VersionValuePattern) {
        throw "Refusing to write version '$Version' - it is not X.Y.Z[-prerelease]."
    }

    # Resolve-ReleaseVersion accepts X.Y.Z[-prerelease], and the three numeric
    # macros are ints - a '1.0.0-rc1' would otherwise write PATCH as '0-rc1'.
    # Only the string literal carries the prerelease.
    $parts = $Version.Split('-')[0].Split('.')
    return @(
        @{ Pattern = '(?m)^(#define\s+HEADTRACKING_VERSION_MAJOR\s+)\d+'; Replacement = "`${1}$($parts[0])" }
        @{ Pattern = '(?m)^(#define\s+HEADTRACKING_VERSION_MINOR\s+)\d+'; Replacement = "`${1}$($parts[1])" }
        @{ Pattern = '(?m)^(#define\s+HEADTRACKING_VERSION_PATCH\s+)\d+'; Replacement = "`${1}$($parts[2])" }
        @{ Pattern = $script:VersionStringWritePattern; Replacement = "`${1}`"$Version`"" }
    )
}

function Set-ModVersion {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [Parameter(Mandatory = $true)][string]$Version
    )

    Update-VersionLiterals -Path (Get-ModVersionPath -ProjectRoot $ProjectRoot) `
                           -Rewrites (Get-ModVersionRewrites -Version $Version)
}

Export-ModuleMember -Function Get-ModVersionPath, Get-ModVersion, Get-ModVersionRewrites,
                              Set-ModVersion, Update-VersionLiteral, Update-VersionLiteralSet,
                              Resolve-VersionLiteralSet
