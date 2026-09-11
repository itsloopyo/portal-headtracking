#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for scripts/ModVersion.psm1
# ============================================================================
# Run: pixi run test
#
# These lock the behaviour release.ps1, package-release.ps1 and
# release-nightly.ps1 had when each parsed src/version.h its own way, so the
# move to one shared parser is provably not a change: the same value from a
# well-formed header, the same throw on a missing file and on a missing macro,
# and the same bytes written back for everything the rewrite does not target.
#
# The one deliberate difference from those three parsers is the shape assert on
# the value itself, on both the read and the write, covered below.
#
# No Pester dependency, matching cameraunlock-core/powershell/tests.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'ModVersion.psm1') -Force

$script:Failures = 0

function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail)
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name - $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

# Returns '' when the action completed, or the terminating error's message when
# it did not - a string either way, so Check's detail never dereferences $null
# under Set-StrictMode.
function Get-ThrownMessage {
    param([scriptblock]$Action)
    try { & $Action | Out-Null; return '' } catch { return $_.Exception.Message }
}

$sandbox = Join-Path ([System.IO.Path]::GetTempPath()) "phtt-modversion-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null

# The header shape release.ps1 rewrites: three numeric macros and the string
# the packager and release.yml both read. The fixtures are CRLF; the checked-in
# src/version.h is LF, and Set-ModVersion preserves whichever it finds.
function New-ProjectRoot {
    param([string]$Name, [string]$Version = '1.2.3', [string[]]$Lines)

    $root = Join-Path $sandbox $Name
    New-Item -ItemType Directory -Path (Join-Path $root 'src') -Force | Out-Null
    if (-not $PSBoundParameters.ContainsKey('Lines')) {
        $parts = $Version.Split('.')
        $Lines = @(
            '// SPDX-License-Identifier: MIT',
            '#pragma once',
            '',
            "#define HEADTRACKING_VERSION_MAJOR $($parts[0])",
            "#define HEADTRACKING_VERSION_MINOR $($parts[1])",
            "#define HEADTRACKING_VERSION_PATCH $($parts[2])",
            "#define HEADTRACKING_VERSION_STRING `"$Version`"",
            ''
        )
    }
    [System.IO.File]::WriteAllText((Join-Path $root 'src\version.h'), ($Lines -join "`r`n"))
    return $root
}

try {

# --- reading -----------------------------------------------------------------

$root = New-ProjectRoot -Name 'read' -Version '4.5.6'
$readVersion = Get-ModVersion -ProjectRoot $root
Check 'Get-ModVersion returns HEADTRACKING_VERSION_STRING' `
    ($readVersion -eq '4.5.6') "got '$readVersion'"

$readPath = Get-ModVersionPath -ProjectRoot $root
Check 'Get-ModVersionPath points at src\version.h' `
    ($readPath -eq (Join-Path $root 'src\version.h')) "got '$readPath'"

$missing = Join-Path $sandbox 'no-such-project'
$msg = Get-ThrownMessage { Get-ModVersion -ProjectRoot $missing }
Check 'Get-ModVersion throws when version.h is absent' `
    ($msg -ne '' -and $msg -match 'version\.h') "got '$msg'"

$noMacro = New-ProjectRoot -Name 'no-macro' -Lines @('#pragma once', '')
$msg = Get-ThrownMessage { Get-ModVersion -ProjectRoot $noMacro }
Check 'Get-ModVersion throws when HEADTRACKING_VERSION_STRING is absent' `
    ($msg -ne '') 'no error raised'

# The deliberate tightening: a value that satisfies the capture regex but is
# not a version the tag and the ZIP filenames can carry.
$badValue = New-ProjectRoot -Name 'bad-value' -Lines @(
    '#define HEADTRACKING_VERSION_STRING "v1.2.3"', '')
$msg = Get-ThrownMessage { Get-ModVersion -ProjectRoot $badValue }
Check 'Get-ModVersion rejects a non-semver value' `
    ($msg -match 'not X\.Y\.Z') "got '$msg'"

$prerelease = New-ProjectRoot -Name 'prerelease' -Lines @(
    '#define HEADTRACKING_VERSION_MAJOR 1',
    '#define HEADTRACKING_VERSION_MINOR 0',
    '#define HEADTRACKING_VERSION_PATCH 0',
    '#define HEADTRACKING_VERSION_STRING "1.0.0-rc.1"',
    '')
$readVersion = Get-ModVersion -ProjectRoot $prerelease
Check 'Get-ModVersion accepts a prerelease value' `
    ($readVersion -eq '1.0.0-rc.1') "got '$readVersion'"

# --- writing -----------------------------------------------------------------

$root = New-ProjectRoot -Name 'write' -Version '1.2.3'
$path = Get-ModVersionPath -ProjectRoot $root
Set-ModVersion -ProjectRoot $root -Version '2.7.11'
$raw = [System.IO.File]::ReadAllText($path)
Check 'Set-ModVersion writes MAJOR' ($raw -match '(?m)^#define HEADTRACKING_VERSION_MAJOR 2\s*$') $raw
Check 'Set-ModVersion writes MINOR' ($raw -match '(?m)^#define HEADTRACKING_VERSION_MINOR 7\s*$') $raw
Check 'Set-ModVersion writes PATCH' ($raw -match '(?m)^#define HEADTRACKING_VERSION_PATCH 11\s*$') $raw
Check 'Set-ModVersion writes STRING' ($raw -match '#define HEADTRACKING_VERSION_STRING "2\.7\.11"') $raw
Check 'Set-ModVersion round-trips through Get-ModVersion' `
    ((Get-ModVersion -ProjectRoot $root) -eq '2.7.11') 'read back a different value'

Check 'Set-ModVersion leaves CRLF endings intact' `
    (($raw -split "`r`n").Count -eq 8 -and $raw -notmatch "(?<!`r)`n") 'line endings changed'
Check 'Set-ModVersion leaves untargeted lines byte-identical' `
    ($raw.StartsWith("// SPDX-License-Identifier: MIT`r`n#pragma once`r`n`r`n")) 'header preamble changed'

$root = New-ProjectRoot -Name 'write-prerelease' -Version '1.2.3'
Set-ModVersion -ProjectRoot $root -Version '3.0.0-rc.2'
$raw = [System.IO.File]::ReadAllText((Get-ModVersionPath -ProjectRoot $root))
Check 'Set-ModVersion keeps the numeric macros numeric for a prerelease' `
    ($raw -match '(?m)^#define HEADTRACKING_VERSION_PATCH 0\s*$') $raw
Check 'Set-ModVersion puts the prerelease suffix in the string only' `
    ($raw -match '#define HEADTRACKING_VERSION_STRING "3\.0\.0-rc\.2"') $raw

$root = New-ProjectRoot -Name 'write-bad' -Version '1.2.3'
$before = [System.IO.File]::ReadAllText((Get-ModVersionPath -ProjectRoot $root))
$msg = Get-ThrownMessage { Set-ModVersion -ProjectRoot $root -Version 'v1.2.3' }
$after = [System.IO.File]::ReadAllText((Get-ModVersionPath -ProjectRoot $root))
Check 'Set-ModVersion refuses a non-semver value' ($msg -match 'Refusing to write') "got '$msg'"
Check 'Set-ModVersion writes nothing when it refuses' ($before -eq $after) 'the file was modified'

# All four macros are rewritten in one read/write, so a header missing one of
# them must leave the other three exactly as they were rather than shipping a
# half-bumped version to the tag.
$partial = New-ProjectRoot -Name 'write-partial' -Lines @(
    '#define HEADTRACKING_VERSION_MAJOR 1',
    '#define HEADTRACKING_VERSION_MINOR 2',
    '#define HEADTRACKING_VERSION_STRING "1.2.3"',
    '')
$partialPath = Get-ModVersionPath -ProjectRoot $partial
$before = [System.IO.File]::ReadAllText($partialPath)
$msg = Get-ThrownMessage { Set-ModVersion -ProjectRoot $partial -Version '9.9.9' }
Check 'Set-ModVersion throws when a macro is missing' ($msg -match 'matched nothing') "got '$msg'"
Check 'Set-ModVersion writes nothing when one macro is missing' `
    ($before -eq [System.IO.File]::ReadAllText($partialPath)) 'the file was partly rewritten'

# Re-writing the version already in the file must not touch it, so a re-run of
# a release does not churn its mtime.
$same = New-ProjectRoot -Name 'write-same' -Version '5.6.7'
$samePath = Get-ModVersionPath -ProjectRoot $same
$stamp = (Get-Item -LiteralPath $samePath).LastWriteTimeUtc
Set-ModVersion -ProjectRoot $same -Version '5.6.7'
Check 'Set-ModVersion does not rewrite an unchanged version' `
    ((Get-Item -LiteralPath $samePath).LastWriteTimeUtc -eq $stamp) 'the file was rewritten'

# --- Update-VersionLiteral ---------------------------------------------------
# The two other files release.ps1 mirrors the version into. install.cmd is CRLF
# and the packager refuses to ship it otherwise, so the rewrite must not
# normalise it.

$installCmd = Join-Path $sandbox 'install.cmd'
[System.IO.File]::WriteAllText($installCmd, (@(
    '@echo off',
    'set "GAME_ID=portal"',
    'set "MOD_VERSION=0.0.0"',
    'set "ASI_SUBDIR=bin"',
    '') -join "`r`n"))
Update-VersionLiteral -Path $installCmd `
    -Pattern 'set "MOD_VERSION=[^"]+"' -Replacement 'set "MOD_VERSION=9.8.7"'
$raw = [System.IO.File]::ReadAllText($installCmd)
Check 'Update-VersionLiteral rewrites install.cmd MOD_VERSION' `
    ($raw -match 'set "MOD_VERSION=9\.8\.7"') $raw
Check 'Update-VersionLiteral leaves install.cmd CRLF' `
    ($raw -notmatch "(?<!`r)`n") 'line endings changed'
Check 'Update-VersionLiteral leaves the other install.cmd keys alone' `
    ($raw -match 'set "GAME_ID=portal"' -and $raw -match 'set "ASI_SUBDIR=bin"') $raw

$cmake = Join-Path $sandbox 'CMakeLists.txt'
[System.IO.File]::WriteAllText($cmake, (@(
    'cmake_minimum_required(VERSION 3.20)',
    'project(PortalHeadTracking VERSION 0.0.0 LANGUAGES CXX)',
    '') -join "`n"))
Update-VersionLiteral -Path $cmake `
    -Pattern '(?m)^(project\([^)]*VERSION\s+)\d+\.\d+\.\d+' -Replacement '${1}9.8.7'
$raw = [System.IO.File]::ReadAllText($cmake)
Check 'Update-VersionLiteral rewrites the CMake project version' `
    ($raw -match 'project\(PortalHeadTracking VERSION 9\.8\.7 LANGUAGES CXX\)') $raw
Check 'Update-VersionLiteral leaves an LF file LF' ($raw -notmatch "`r") 'line endings changed'

$msg = Get-ThrownMessage {
    Update-VersionLiteral -Path $cmake -Pattern 'no_such_literal' -Replacement 'x'
}
Check 'Update-VersionLiteral throws when its pattern matches nothing' `
    ($msg -match 'matched nothing') "got '$msg'"

# --- Update-VersionLiteralSet ------------------------------------------------
# release.ps1 mirrors one version into src/version.h, scripts/install.cmd and
# CMakeLists.txt, and does it after the CHANGELOG has already been regenerated.
# The three have to land together or not at all: a set that wrote the first two
# and threw on the third would leave a dirty tree carrying half a bump.

function New-LiteralSetFixture {
    param([string]$Name, [string]$ThirdPattern = 'set "MOD_VERSION=[^"]+"')

    $dir = Join-Path $sandbox $Name
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $paths = @()
    foreach ($n in 1..3) {
        $f = Join-Path $dir "file$n.cmd"
        [System.IO.File]::WriteAllText($f, "set `"MOD_VERSION=0.0.0`"`r`n")
        $paths += $f
    }
    return @{
        Paths = $paths
        Files = @(
            @{ Path = $paths[0]; Rewrites = @(@{ Pattern = 'set "MOD_VERSION=[^"]+"'; Replacement = 'set "MOD_VERSION=1.1.1"' }) }
            @{ Path = $paths[1]; Rewrites = @(@{ Pattern = 'set "MOD_VERSION=[^"]+"'; Replacement = 'set "MOD_VERSION=1.1.1"' }) }
            @{ Path = $paths[2]; Rewrites = @(@{ Pattern = $ThirdPattern; Replacement = 'set "MOD_VERSION=1.1.1"' }) }
        )
    }
}

$set = New-LiteralSetFixture -Name 'literal-set-ok'
Update-VersionLiteralSet -Files $set.Files
$written = @($set.Paths | Where-Object { [System.IO.File]::ReadAllText($_) -match '1\.1\.1' })
Check 'Update-VersionLiteralSet writes every file when every pattern matches' `
    ($written.Count -eq 3) "$($written.Count) of 3 files were rewritten"

$set = New-LiteralSetFixture -Name 'literal-set-partial' -ThirdPattern 'no_such_literal'
$before = @($set.Paths | ForEach-Object { [System.IO.File]::ReadAllText($_) })
$msg = Get-ThrownMessage { Update-VersionLiteralSet -Files $set.Files }
$after = @($set.Paths | ForEach-Object { [System.IO.File]::ReadAllText($_) })
Check 'Update-VersionLiteralSet throws when a later file misses its pattern' `
    ($msg -match 'matched nothing') "got '$msg'"
Check 'Update-VersionLiteralSet writes nothing when a later file misses its pattern' `
    ((Compare-Object $before $after -SyncWindow 0) -eq $null) 'an earlier file was rewritten anyway'

$set = New-LiteralSetFixture -Name 'literal-set-absent'
$set.Files[2].Path = Join-Path $sandbox 'literal-set-absent\no-such-file.txt'
$before = [System.IO.File]::ReadAllText($set.Paths[0])
$msg = Get-ThrownMessage { Update-VersionLiteralSet -Files $set.Files }
Check 'Update-VersionLiteralSet throws when a later file is absent' `
    ($msg -match 'no-such-file') "got '$msg'"
Check 'Update-VersionLiteralSet writes nothing when a later file is absent' `
    ($before -eq [System.IO.File]::ReadAllText($set.Paths[0])) 'an earlier file was rewritten anyway'

# Matching every pattern is not the same as being able to write every file. A
# read-only third file passes the match phase and then fails the write, which is
# the half-applied bump the set exists to prevent - and the version.h and
# CMakeLists.txt cases (a configure holding the file open, a copied tree that
# kept its read-only attribute) arrive the same way.
$set = New-LiteralSetFixture -Name 'literal-set-readonly'
$before = @($set.Paths | ForEach-Object { [System.IO.File]::ReadAllText($_) })
Set-ItemProperty -LiteralPath $set.Paths[2] -Name IsReadOnly -Value $true
try {
    $msg = Get-ThrownMessage { Update-VersionLiteralSet -Files $set.Files }
} finally {
    Set-ItemProperty -LiteralPath $set.Paths[2] -Name IsReadOnly -Value $false
}
$after = @($set.Paths | ForEach-Object { [System.IO.File]::ReadAllText($_) })
Check 'Update-VersionLiteralSet throws when a later file cannot be written' `
    ($msg -match 'denied|read-only|being used') "got '$msg'"
Check 'Update-VersionLiteralSet writes nothing when a later file cannot be written' `
    ((Compare-Object $before $after -SyncWindow 0) -eq $null) 'an earlier file was rewritten anyway'

# Resolve-VersionLiteralSet is that match-and-prove phase on its own, so
# release.ps1 can refuse a release before it regenerates the CHANGELOG.
$set = New-LiteralSetFixture -Name 'resolve-ok'
$before = @($set.Paths | ForEach-Object { [System.IO.File]::ReadAllText($_) })
# No @() around the call: the function returns its array with a leading comma
# so an empty result stays an empty array instead of unrolling to $null, and
# wrapping it again would produce a one-element array holding the array.
$pending = Resolve-VersionLiteralSet -Files $set.Files
$after = @($set.Paths | ForEach-Object { [System.IO.File]::ReadAllText($_) })
Check 'Resolve-VersionLiteralSet reports every file that would change' `
    ($pending.Count -eq 3) "$($pending.Count) of 3 files reported"
Check 'Resolve-VersionLiteralSet writes nothing' `
    ((Compare-Object $before $after -SyncWindow 0) -eq $null) 'a file was rewritten'

$set = New-LiteralSetFixture -Name 'resolve-miss' -ThirdPattern 'no_such_literal'
$msg = Get-ThrownMessage { Resolve-VersionLiteralSet -Files $set.Files }
Check 'Resolve-VersionLiteralSet throws when a pattern matches nothing' `
    ($msg -match 'matched nothing') "got '$msg'"

# --- the anchored HEADTRACKING_VERSION_STRING pattern ------------------------
# Get-ProjectVersion returns the FIRST match and the rewrite replaces EVERY
# match, so a pattern not anchored to the #define reads a comment that mentions
# the macro and rewrites that comment too. release.yml carries the same pattern
# string, so its tag-vs-file check reads the same wrong value and agrees with
# the tag; nothing downstream can catch it.
$root = New-ProjectRoot -Name 'anchored' -Lines @(
    '// HEADTRACKING_VERSION_STRING "0.0.0-doc" is what the packager reads.',
    '#define HEADTRACKING_VERSION_MAJOR 1',
    '#define HEADTRACKING_VERSION_MINOR 2',
    '#define HEADTRACKING_VERSION_PATCH 3',
    '#define HEADTRACKING_VERSION_STRING "1.2.3"'
)
$read = Get-ModVersion -ProjectRoot $root
Check 'Get-ModVersion ignores a comment that mentions the macro' ($read -eq '1.2.3') "read '$read'"

Set-ModVersion -ProjectRoot $root -Version '4.5.6'
$header = [System.IO.File]::ReadAllText((Get-ModVersionPath -ProjectRoot $root))
Check 'Set-ModVersion leaves a comment that mentions the macro alone' `
    ($header -match [regex]::Escape('// HEADTRACKING_VERSION_STRING "0.0.0-doc"')) $header
Check 'Set-ModVersion keeps the #define on the line it rewrites' `
    ($header -match [regex]::Escape('#define HEADTRACKING_VERSION_STRING "4.5.6"')) $header
$read = Get-ModVersion -ProjectRoot $root
Check 'Set-ModVersion round-trips past a comment' ($read -eq '4.5.6') "read '$read'"

# --- encoding ----------------------------------------------------------------
# version.h is a C++ header and Visual Studio saves those with a UTF-8 BOM.
# ReadAllText consumes a BOM and WriteAllText writes none, so a round trip
# through that pair drops it and the next bump is a whole-file diff.
$root = New-ProjectRoot -Name 'bom'
$headerPath = Get-ModVersionPath -ProjectRoot $root
$text = [System.IO.File]::ReadAllText($headerPath)
[System.IO.File]::WriteAllText($headerPath, $text, (New-Object System.Text.UTF8Encoding $true))
Set-ModVersion -ProjectRoot $root -Version '2.0.0'
$bytes = [System.IO.File]::ReadAllBytes($headerPath)
Check 'Set-ModVersion preserves a UTF-8 BOM' `
    ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) 'the BOM was dropped'
$read = Get-ModVersion -ProjectRoot $root
Check 'Set-ModVersion still reads back with a BOM present' ($read -eq '2.0.0') "read '$read'"

$root = New-ProjectRoot -Name 'no-bom'
Set-ModVersion -ProjectRoot $root -Version '2.0.0'
$bytes = [System.IO.File]::ReadAllBytes((Get-ModVersionPath -ProjectRoot $root))
Check 'Set-ModVersion adds no BOM to a file that had none' `
    (-not ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) 'a BOM appeared'

# --- the patterns, against the files this repo actually ships ----------------
# Everything above is synthetic. These assert the literals release.ps1 rewrites
# are present in the real src/version.h and scripts/install.cmd, so a rename
# that would abort a release mid-bump fails here instead.
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# ModVersion.psm1 says its pattern is "byte-identical to release.yml's
# version-pattern input", and the whole design rests on that: CI re-reads
# version.h with the workflow's copy to check the pushed tag. Nothing else
# compares the two, and they live in different files in different languages.
$releaseYml = [System.IO.File]::ReadAllText((Join-Path $repoRoot '.github/workflows/release.yml'))
$ymlPattern = [regex]::Match($releaseYml, "(?m)^\s*version-pattern:\s*'(.*)'\s*$")
Check 'release.yml declares a version-pattern' ($ymlPattern.Success) 'no version-pattern line'
$modulePattern = & (Get-Module ModVersion) { $script:VersionStringPattern }
Check 'release.yml version-pattern is byte-identical to the module pattern' `
    ($ymlPattern.Groups[1].Value -eq $modulePattern) `
    "release.yml has '$($ymlPattern.Groups[1].Value)', the module has '$modulePattern'"

Check 'the repo has the src/version.h release.yml reads' `
    (Test-Path (Get-ModVersionPath -ProjectRoot $repoRoot)) 'src/version.h is missing'
$repoVersion = Get-ModVersion -ProjectRoot $repoRoot
$installCmdText = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'scripts\install.cmd'))
$repoModVersion = [regex]::Match($installCmdText, 'set "MOD_VERSION=([^"]*)"')
Check 'scripts/install.cmd carries the MOD_VERSION literal release.ps1 rewrites' `
    ($repoModVersion.Success) 'no MOD_VERSION literal'
Check 'scripts/install.cmd MOD_VERSION agrees with src/version.h' `
    ($repoModVersion.Groups[1].Value -eq $repoVersion) `
    "install.cmd says '$($repoModVersion.Groups[1].Value)', version.h says '$repoVersion'"

} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$script:Failures check(s) failed." -ForegroundColor Red
    exit 1
}
Write-Host 'ModVersion.psm1: all checks passed.' -ForegroundColor Green
