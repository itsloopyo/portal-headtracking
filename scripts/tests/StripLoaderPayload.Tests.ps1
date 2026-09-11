#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for scripts/strip-loader-payload.ps1
# ============================================================================
# Run: pixi run test
#
# That script is the only thing in this repo that parses an untrusted binary
# and writes bytes back into it. Its input is a DLL `pixi run update-deps` has
# just pulled off the network, and its output is committed under vendor/ and
# redistributed inside the installer ZIP. Two failure modes matter and neither
# is visible afterwards:
#
#   - a resource directory that points back at one of its own ancestors, which
#     the walk followed until the call stack ran out;
#   - a resource data entry whose RVA lands in the zero-filled tail of its
#     section, past the raw bytes that exist in the file. That maps to a file
#     offset inside whatever section follows on disk, and if those bytes happen
#     to open with 'MZ' the strip zeroes them - corrupting the loader's own
#     code in a DLL that then gets hashed, committed and shipped. -VerifyOnly
#     does not catch it, because it only re-checks that no payload signature is
#     left anywhere.
#
# The fixtures are synthetic PE32 images built byte by byte: small enough to
# reason about, with sentinel bytes either side of the payload and a second
# section behind it, so the tests can say the strip touched the payload and
# nothing else.
#
# No Pester dependency, matching cameraunlock-core/powershell/tests.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$stripScript = Join-Path (Split-Path -Parent $PSScriptRoot) 'strip-loader-payload.ps1'

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

# --- fixture -----------------------------------------------------------------
# Two sections, so an RVA that misses .rsrc's raw bytes lands somewhere real:
#
#   .rsrc  RVA 0x1000  virtual 0x200  raw 0x400 .. 0x500  (raw is half the
#                                                          virtual size, so the
#                                                          section has a tail
#                                                          that is not in the
#                                                          file at all)
#   .data  RVA 0x1200  virtual 0x100  raw 0x500 .. 0x600
#
# Inside .rsrc, relative to its raw base:
#
#   0x000  level 0 resource directory   (type     -> RT_RCDATA)
#   0x018  level 1 resource directory   (name     -> 1)
#   0x030  level 2 resource directory   (language -> 1033)
#   0x048  resource data entry          (RVA + size of the payload)
#   0x058  eight sentinel bytes         0xAA
#   0x060  payload, 32 bytes            'MZ' + 0xCC filler
#   0x080  eight sentinel bytes         0xBB
#
# .data is 0xDD filler with a decoy 'MZ' at file offset 0x580 - the bytes an
# unchecked RVA maps onto, and the reason a bad entry corrupts the image rather
# than merely throwing.

$RsrcRva     = 0x1000
$RsrcVirtual = 0x200
$RsrcRaw     = 0x400
$RsrcRawSize = 0x100

$DataRva     = 0x1200
$DataRaw     = 0x500
$DataSize    = 0x100
$DecoyAt     = 0x080

$DirL0          = 0x000
$DirL1          = 0x018
$DirL2          = 0x030
$DataEntry      = 0x048
$SentinelBefore = 0x058
$PayloadAt      = 0x060
$PayloadSize    = 32
$SentinelAfter  = 0x080

$CertOffset     = 0x600
$CertSize       = 0x040

function Set-U16 { param([byte[]]$b, [int]$o, [int]$v) [BitConverter]::GetBytes([uint16]$v).CopyTo($b, $o) }
function Set-U32 { param([byte[]]$b, [int]$o, [long]$v) [BitConverter]::GetBytes([uint32]$v).CopyTo($b, $o) }

# The PE optional-header CheckSum: a folded 16-bit sum over the image with the
# CheckSum field itself read as zero, plus the file length. A second
# transcription of the same algorithm, not an independent oracle: it catches an
# edit to one side, and says nothing about whether both are right. The algorithm
# was checked against imagehlp's MapFileAndCheckSumW on real system binaries.
function Get-ExpectedCheckSum {
    param([byte[]]$Image, [int]$PeOffset)

    $field = $PeOffset + 24 + 64
    $sum   = [long]0
    for ($i = 0; $i + 1 -lt $Image.Length; $i += 2) {
        if ($i -ge $field -and $i -lt ($field + 4)) { continue }
        $sum += [BitConverter]::ToUInt16($Image, $i)
    }
    if ($Image.Length % 2) { $sum += $Image[$Image.Length - 1] }
    while ($sum -gt 0xFFFF) { $sum = ($sum -band 0xFFFF) + ($sum -shr 16) }
    return [uint32]($sum + $Image.Length)
}

# Variants:
#   payload      a well-formed image carrying one embedded-DLL payload
#   clean        the same image with a leaf that is not a third-party payload
#   cycle        the language-level entry is a subdirectory pointing back at
#                the type directory
#   unbacked     the data entry's RVA lands in .rsrc's virtual tail, so an
#                unchecked mapping addresses the decoy 'MZ' in .data
#   orphan       the data entry's RVA lands in .data's RAW bytes, on the decoy
#                'MZ'. Backed by raw data, so the RawSize check passes; the only
#                thing that rejects it is bounding the payload to .rsrc
#   signed       a well-formed payload image with an appended certificate table
#   trailing     the same, with bytes appended AFTER the certificate table, so
#                truncating at the table start would discard them
#   badmagic     an optional header magic that is neither PE32 nor PE32+
#   shortdirs    SizeOfOptionalHeader stops before the certificate directory
#                while NumberOfRvaAndSizes claims all 16 are present
#   nodirs       NumberOfRvaAndSizes too small to hold a resource directory
#   shortopt     SizeOfOptionalHeader overlaps the section table
#   truncated    e_lfanew points past the end of the file
function New-TestPe {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [ValidateSet('payload', 'clean', 'cycle', 'unbacked', 'orphan', 'signed',
                     'trailing', 'badmagic', 'nodirs', 'shortopt', 'shortdirs',
                     'truncated')][string]$Variant = 'payload'
    )

    $b = New-Object byte[] $(
        switch ($Variant) {
            'signed'   { 0x640 }
            'trailing' { 0x680 }
            default    { 0x600 }
        })

    $b[0] = 0x4D; $b[1] = 0x5A                      # 'MZ'
    Set-U32 $b 0x3C $(if ($Variant -eq 'truncated') { 0x5FF0 } else { 0x80 })

    $pe = 0x80
    $b[$pe] = 0x50; $b[$pe + 1] = 0x45              # 'PE\0\0'
    Set-U16 $b ($pe + 4) 0x014C                     # Machine: x86
    Set-U16 $b ($pe + 6) 2                          # NumberOfSections
    # 100 is short of the 96 + 3*8 a resource data directory needs, and 120 is
    # long enough for that but short of the 96 + 5*8 the certificate directory
    # needs. Either way the section table starts where a directory read lands.
    $optHeaderSize = $(
        switch ($Variant) {
            'shortopt'  { 100 }
            'shortdirs' { 120 }
            default     { 224 }
        })
    Set-U16 $b ($pe + 20) $optHeaderSize

    $opt = $pe + 24
    Set-U16 $b $opt $(if ($Variant -eq 'badmagic') { 0x0107 } else { 0x010B })
    # Every real PE32 image declares 16. 'nodirs' declares two, which stops
    # short of the resource entry at index 2.
    Set-U32 $b ($opt + 92) $(if ($Variant -eq 'nodirs') { 2 } else { 16 })
    $dataDir = $opt + 96
    Set-U32 $b ($dataDir + 2 * 8) $RsrcRva          # resource directory RVA
    Set-U32 $b ($dataDir + 2 * 8 + 4) $RsrcRawSize
    if ($Variant -eq 'signed' -or $Variant -eq 'trailing') {
        # The certificate table is the one data directory addressed by file
        # offset rather than RVA, and it sits after the last section.
        Set-U32 $b ($dataDir + 4 * 8) $CertOffset
        Set-U32 $b ($dataDir + 4 * 8 + 4) $CertSize
        for ($i = 0; $i -lt $CertSize; $i++) { $b[$CertOffset + $i] = 0xEE }
    }
    if ($Variant -eq 'trailing') {
        for ($i = $CertOffset + $CertSize; $i -lt $b.Length; $i++) { $b[$i] = 0x5A }
    }

    $sec = $opt + $optHeaderSize
    [System.Text.Encoding]::ASCII.GetBytes('.rsrc').CopyTo($b, $sec)
    Set-U32 $b ($sec + 8) $RsrcVirtual              # VirtualSize
    Set-U32 $b ($sec + 12) $RsrcRva                 # VirtualAddress
    Set-U32 $b ($sec + 16) $RsrcRawSize             # SizeOfRawData
    Set-U32 $b ($sec + 20) $RsrcRaw                 # PointerToRawData

    $sec2 = $sec + 40
    [System.Text.Encoding]::ASCII.GetBytes('.data').CopyTo($b, $sec2)
    Set-U32 $b ($sec2 + 8) $DataSize
    Set-U32 $b ($sec2 + 12) $DataRva
    Set-U32 $b ($sec2 + 16) $DataSize
    Set-U32 $b ($sec2 + 20) $DataRaw

    for ($i = 0; $i -lt $DataSize; $i++) { $b[$DataRaw + $i] = 0xDD }
    $b[$DataRaw + $DecoyAt] = 0x4D
    $b[$DataRaw + $DecoyAt + 1] = 0x5A

    # Decimal, not 0x80000000: PowerShell parses that hex literal as [int]
    # -2147483648, and -bor would then produce a negative offset.
    $subdir = 2147483648

    Set-U16 $b ($RsrcRaw + $DirL0 + 14) 1           # one id entry
    Set-U32 $b ($RsrcRaw + $DirL0 + 16) 10          # RT_RCDATA
    Set-U32 $b ($RsrcRaw + $DirL0 + 20) ($subdir -bor $DirL1)

    Set-U16 $b ($RsrcRaw + $DirL1 + 14) 1
    Set-U32 $b ($RsrcRaw + $DirL1 + 16) 1           # resource name id
    Set-U32 $b ($RsrcRaw + $DirL1 + 20) ($subdir -bor $DirL2)

    Set-U16 $b ($RsrcRaw + $DirL2 + 14) 1
    Set-U32 $b ($RsrcRaw + $DirL2 + 16) 1033        # language id
    Set-U32 $b ($RsrcRaw + $DirL2 + 20) $(if ($Variant -eq 'cycle') { $subdir -bor $DirL0 } else { $DataEntry })

    # 0x1180 is inside .rsrc's virtual span and past its raw bytes. Mapped the
    # unchecked way it is file offset 0x580 - the decoy 'MZ' in .data.
    Set-U32 $b ($RsrcRaw + $DataEntry) $(
        switch ($Variant) {
            'unbacked' { $RsrcRva + $RsrcRawSize + $DecoyAt }
            'orphan'   { $DataRva + $DecoyAt }
            default    { $RsrcRva + $PayloadAt }
        })
    Set-U32 $b ($RsrcRaw + $DataEntry + 4) $PayloadSize

    for ($i = 0; $i -lt 8; $i++) { $b[$RsrcRaw + $SentinelBefore + $i] = 0xAA }
    for ($i = 0; $i -lt 8; $i++) { $b[$RsrcRaw + $SentinelAfter + $i] = 0xBB }

    # 'MZ' is what marks a leaf as an embedded DLL. 'clean' writes RCDATA the
    # loader legitimately carries instead, which must be left alone.
    if ($Variant -eq 'clean') {
        $b[$RsrcRaw + $PayloadAt] = 0x7B           # '{'
    } else {
        $b[$RsrcRaw + $PayloadAt] = 0x4D           # 'M'
        $b[$RsrcRaw + $PayloadAt + 1] = 0x5A       # 'Z'
    }
    for ($i = 2; $i -lt $PayloadSize; $i++) { $b[$RsrcRaw + $PayloadAt + $i] = 0xCC }

    # A real PE carries a valid CheckSum, and the script now recomputes it and
    # rewrites the file whenever it is stale. Without this every fixture would
    # be "stale checksum" and no test could assert that an already-clean image
    # is left untouched. Computed here rather than imported from the script, so
    # the two implementations have to agree.
    Set-U32 $b ($opt + 64) (Get-ExpectedCheckSum $b $pe)

    [System.IO.File]::WriteAllBytes($Path, $b)
    return $Path
}

# `exit` inside a script invoked with & ends that script only, so both a clean
# exit and a throw are observable from here. 6> drops the script's Write-Host
# progress lines without touching the terminating errors under test.
function Invoke-Strip {
    param([string]$Path, [switch]$VerifyOnly)

    try {
        if ($VerifyOnly) { & $stripScript -Path $Path -VerifyOnly 6> $null }
        else { & $stripScript -Path $Path 6> $null }
        return ''
    } catch {
        return $_.Exception.Message
    }
}

function Get-Sha { param([string]$Path) (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }

$sandbox = Join-Path ([System.IO.Path]::GetTempPath()) "phtt-strip-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null

try {

# --- the strip itself --------------------------------------------------------

$pe = New-TestPe -Path (Join-Path $sandbox 'payload.dll') -Variant payload
$before = [System.IO.File]::ReadAllBytes($pe)
$msg = Invoke-Strip -Path $pe
$after = [System.IO.File]::ReadAllBytes($pe)

Check 'strip completes on a well-formed image' ($msg -eq '') "got '$msg'"

$payloadBytes = $after[($RsrcRaw + $PayloadAt)..($RsrcRaw + $PayloadAt + $PayloadSize - 1)]
Check 'strip zeroes the embedded payload' `
    (@($payloadBytes | Where-Object { $_ -ne 0 }).Count -eq 0) 'payload bytes survived'

# The CheckSum field is the one thing outside the payload the strip is meant to
# rewrite: zeroing the payload invalidates it, and a stale one is half the
# fingerprint of a hand-patched binary.
$checkSumField = 0x80 + 24 + 64
$changedOutsidePayload = 0
for ($i = 0; $i -lt $before.Length; $i++) {
    if ($i -ge ($RsrcRaw + $PayloadAt) -and $i -lt ($RsrcRaw + $PayloadAt + $PayloadSize)) { continue }
    if ($i -ge $checkSumField -and $i -lt ($checkSumField + 4)) { continue }
    if ($before[$i] -ne $after[$i]) { $changedOutsidePayload++ }
}
Check 'strip changes nothing outside the payload and the checksum' `
    ($changedOutsidePayload -eq 0) "$changedOutsidePayload byte(s) outside the payload changed"

Check 'strip leaves the CheckSum field correct for the new bytes' `
    ([BitConverter]::ToUInt32($after, $checkSumField) -eq (Get-ExpectedCheckSum $after 0x80)) `
    'the stored CheckSum does not match the stripped image'

$msg = Invoke-Strip -Path $pe -VerifyOnly
Check 'VerifyOnly passes once the payload is gone' ($msg -eq '') "got '$msg'"

# --- the release gate --------------------------------------------------------

$pe = New-TestPe -Path (Join-Path $sandbox 'gate.dll') -Variant payload
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe -VerifyOnly
Check 'VerifyOnly fails on an image that still carries a payload' `
    ($msg -match 'third-party RCDATA payload') "got '$msg'"
Check 'VerifyOnly does not modify the image' ((Get-Sha $pe) -eq $sha) 'the file was written to'

# --- RCDATA the loader legitimately carries ----------------------------------

$pe = New-TestPe -Path (Join-Path $sandbox 'clean.dll') -Variant clean
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'strip leaves non-payload RCDATA alone' `
    ($msg -eq '' -and (Get-Sha $pe) -eq $sha) "got '$msg'"

# --- malformed input ---------------------------------------------------------
# Each of these used to end in a way the caller could not act on: a call-depth
# overflow, a write to bytes in a different section, or a raw .NET index
# exception in place of a diagnostic.

$pe = New-TestPe -Path (Join-Path $sandbox 'cycle.dll') -Variant cycle
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'a resource directory that points back at an ancestor is rejected' `
    ($msg -match 'below the language level') "got '$msg'"
Check 'a cyclic directory does not modify the image' ((Get-Sha $pe) -eq $sha) 'the file was written to'

$pe = New-TestPe -Path (Join-Path $sandbox 'unbacked.dll') -Variant unbacked
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'a data entry outside the section raw data is rejected' `
    ($msg -match 'not backed by raw data') "got '$msg'"
Check 'an unbacked data entry does not zero bytes in another section' `
    ((Get-Sha $pe) -eq $sha) 'the file was written to'

$pe = New-TestPe -Path (Join-Path $sandbox 'truncated.dll') -Variant truncated
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'a header offset past the end of the file is rejected' `
    ($msg -match 'runs past the end of the file') "got '$msg'"
Check 'a truncated image does not modify the file' ((Get-Sha $pe) -eq $sha) 'the file was written to'

# The variant the RawSize check cannot see: the RVA IS backed by raw data, just
# not by .rsrc's. Before the payload was bounded to the resource section this
# zeroed 32 bytes of .data and -VerifyOnly then reported the image clean.
$pe = New-TestPe -Path (Join-Path $sandbox 'orphan.dll') -Variant orphan
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'a raw-backed data entry pointing outside .rsrc is rejected' `
    ($msg -match 'outside the resource section') "got '$msg'"
Check 'an out-of-section data entry does not zero bytes in another section' `
    ((Get-Sha $pe) -eq $sha) 'the file was written to'

$pe = New-TestPe -Path (Join-Path $sandbox 'nodirs.dll') -Variant nodirs
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'an image with no resource data directory is rejected' `
    ($msg -match 'NumberOfRvaAndSizes') "got '$msg'"
Check 'a missing resource directory does not modify the file' ((Get-Sha $pe) -eq $sha) 'the file was written to'

$pe = New-TestPe -Path (Join-Path $sandbox 'shortopt.dll') -Variant shortopt
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'an optional header too short for the data directory is rejected' `
    ($msg -match 'SizeOfOptionalHeader') "got '$msg'"
Check 'a short optional header does not modify the file' ((Get-Sha $pe) -eq $sha) 'the file was written to'

# Long enough for the resource directory, short of the certificate one. The
# certificate read then lands in the first section header, and that section's
# VirtualSize and VirtualAddress were used as a table offset and size - which
# truncated the whole image and reported success.
$pe = New-TestPe -Path (Join-Path $sandbox 'shortdirs.dll') -Variant shortdirs
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'an optional header too short for the certificate directory is rejected' `
    ($msg -match 'SizeOfOptionalHeader') "got '$msg'"
Check 'a short certificate directory does not truncate the image' ((Get-Sha $pe) -eq $sha) 'the file was written to'

$pe = New-TestPe -Path (Join-Path $sandbox 'badmagic.dll') -Variant badmagic
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'an unrecognised optional header magic is rejected' `
    ($msg -match 'optional header magic') "got '$msg'"
Check 'an unrecognised magic does not modify the file' ((Get-Sha $pe) -eq $sha) 'the file was written to'

# --- the Authenticode certificate table --------------------------------------
# Upstream signs the loader. Zeroing a payload breaks that signature's hash, and
# Windows then reports the vendored copy as tampered rather than as unsigned.

$pe = New-TestPe -Path (Join-Path $sandbox 'signed.dll') -Variant signed
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe -VerifyOnly
Check 'VerifyOnly fails on an image that still carries a certificate table' `
    ($msg -match 'certificate table') "got '$msg'"
Check 'VerifyOnly does not modify a signed image' ((Get-Sha $pe) -eq $sha) 'the file was written to'

$msg = Invoke-Strip -Path $pe
$after = [System.IO.File]::ReadAllBytes($pe)
Check 'strip removes the certificate table' ($msg -eq '' -and $after.Length -eq $CertOffset) `
    "got '$msg', length $($after.Length)"
$certEntry = 0x80 + 24 + 96 + 4 * 8
Check 'strip clears the certificate data directory entry' `
    (@($after[$certEntry..($certEntry + 7)] | Where-Object { $_ -ne 0 }).Count -eq 0) `
    'the certificate data directory entry still points somewhere'
Check 'VerifyOnly passes once the certificate table is gone' `
    ((Invoke-Strip -Path $pe -VerifyOnly) -eq '') 'the gate still objects'

# The removal truncates at the table's start, which is only correct while the
# table is the last thing in the file. This loader carries a 3 MB appended PDB
# blob, so getting that wrong costs more than the certificate.
$pe = New-TestPe -Path (Join-Path $sandbox 'trailing.dll') -Variant trailing
$sha = Get-Sha $pe
$msg = Invoke-Strip -Path $pe
Check 'a certificate table that is not last is rejected' `
    ($msg -match 'does not end at the end of the file') "got '$msg'"
Check 'a trailing-data image is not truncated' ((Get-Sha $pe) -eq $sha) 'the file was written to'

$notPe = Join-Path $sandbox 'notpe.dll'
[System.IO.File]::WriteAllBytes($notPe, (New-Object byte[] 0x600))
$msg = Invoke-Strip -Path $notPe
Check 'a file with no MZ signature is rejected' ($msg -match 'no MZ') "got '$msg'"

$msg = Invoke-Strip -Path (Join-Path $sandbox 'no-such-file.dll')
Check 'a missing file is rejected' ($msg -match 'Loader not found') "got '$msg'"

} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$script:Failures check(s) failed." -ForegroundColor Red
    exit 1
}
Write-Host 'strip-loader-payload.ps1: all checks passed.' -ForegroundColor Green
