#!/usr/bin/env pwsh
#Requires -Version 5.1
# Remove the third-party DLLs Ultimate ASI Loader carries as RCDATA resources
# from the vendored copy we redistribute.
#
# The upstream Win32 loader embeds three complete DLLs so that a user who
# renames it over one of those libraries still gets the original exports, plus
# the ini template one of them reads:
#
#   binkw32.dll    RAD Game Tools, Inc., "Bink and Smacker" 1.994i. Proprietary
#                  middleware licensed per title. We have no right to
#                  redistribute it, and committing the loader as-is
#                  redistributes it.
#   wndmode.dll    "DirectX Windower Embedded" v2.3, (C) 2008 VEG,
#                  (C) 2004 menopem. No licence accompanies it.
#   vorbisfile.dll Xiph.Org. Redistributable under BSD-3-Clause, but only with
#                  its notice, and we neither use it nor want the obligation.
#
# We deploy the loader as winmm.dll and ship no wndmode.ini, so none of the
# three is reachable in this mod: the two library payloads are keyed off the
# loader's own filename and the windower off that ini. Zeroing them changes
# nothing a user of this mod can observe, and takes the whole question of
# whether we may redistribute them off the table.
#
# The bytes are zeroed in place rather than the resource entries deleted, so
# every offset in the PE stays exactly where the upstream build put it and the
# loader's own code is untouched. Two things outside .rsrc do change, both
# because the zeroing makes them wrong: upstream's Authenticode certificate
# table is removed, so the copy reads as unsigned rather than as tampered, and
# the optional-header CheckSum is recomputed. MIT permits the modification; it
# is recorded in vendor/ultimate-asi-loader/README.md and THIRD-PARTY-NOTICES.md
# so the copy is not mistaken for stock upstream.
#
#   scripts/strip-loader-payload.ps1 -Path vendor/ultimate-asi-loader/dinput8.dll
#   scripts/strip-loader-payload.ps1 -Path <same> -VerifyOnly   # release gate

param(
    [Parameter(Mandatory = $true)][string]$Path,
    [switch]$VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# PE/COFF field offsets and magic values, per the Microsoft PE format spec.
# Named so the walk below reads as a description of the format rather than as
# arithmetic on a byte array.
$MzSignature            = 0x5A4D          # 'MZ', little-endian at offset 0
$PeSignature            = 0x00004550      # 'PE\0\0'
$PeSignatureOffsetField = 0x3C            # e_lfanew, in the DOS header
$SectionCountField      = 6               # from the PE signature: COFF NumberOfSections
$OptionalHeaderSizeField = 20             # from the PE signature: COFF SizeOfOptionalHeader
$OptionalHeaderOffset   = 24              # from the PE signature: end of the COFF header
$Pe32Magic              = 0x10B
$Pe32PlusMagic          = 0x20B
$DataDirectoryOffsetPe32     = 96         # from the start of the optional header
$DataDirectoryOffsetPe32Plus = 112
$RvaCountFieldPe32      = 92              # NumberOfRvaAndSizes, from the optional header
$RvaCountFieldPe32Plus  = 108
$CheckSumField          = 64              # from the start of the optional header
$ResourceDirectoryIndex = 2               # data directory entry for .rsrc
$SecurityDirectoryIndex = 4               # certificate table; a file offset, not an RVA
$SectionHeaderSize      = 40
$ResourceTypeRcData     = 10              # RT_RCDATA

# Offsets within one section header.
$SectionVirtualSizeField    = 8
$SectionVirtualAddressField = 12
$SectionRawSizeField        = 16
$SectionRawOffsetField      = 20

# Offsets within one resource directory table and its entries.
$ResourceNamedEntryCountField = 12
$ResourceIdEntryCountField    = 14
$ResourceEntriesOffset        = 16
$ResourceEntrySize            = 8
$ResourceEntryDataField       = 4         # OffsetToData, high bit set = subdirectory
$ResourceSubdirectoryFlag     = 0x80000000
$ResourceOffsetMask           = 0x7FFFFFFF
$ResourceLeafSizeField        = 4         # from the leaf: Size, after the data RVA
$ResourceDirectoryHeaderSize  = 16        # before the first entry
$ResourceDataEntrySize        = 16        # OffsetToData, Size, CodePage, Reserved
# type -> name -> language, and nothing nests below language. A subdirectory
# entry at the language level is either corruption or a directory pointing back
# at one of its own ancestors, and following it recurses until the stack ends
# the process - which is not something the caller can catch.
$ResourceMaxLevel             = 2

if (-not (Test-Path $Path)) { throw "Loader not found: $Path" }
# .NET resolves a relative path against the PROCESS working directory, which is
# not PowerShell's current location. Pin it to one absolute path here: without
# this, Test-Path can pass on the file you meant while ReadAllBytes reads a
# different one, and the write below then lands on a third.
$Path = (Resolve-Path -LiteralPath $Path).ProviderPath
$bytes = [System.IO.File]::ReadAllBytes($Path)

function Get-U16 { param([byte[]]$b, [int]$o) [BitConverter]::ToUInt16($b, $o) }
function Get-U32 { param([byte[]]$b, [int]$o) [BitConverter]::ToUInt32($b, $o) }

# Every offset below is read out of the file being parsed, so each is checked
# against the file's length before it indexes $bytes. A header field that lies
# is the normal case for a truncated download, and both ways it can end are
# bad: a raw .NET index exception in place of a diagnostic, or - once the walk
# reaches the resource leaves - a write to bytes that are not the payload.
function Assert-Readable {
    param([long]$Offset, [long]$Size, [string]$What)

    if ($Offset -lt 0 -or $Size -lt 0 -or ($Offset + $Size) -gt $bytes.Length) {
        throw "$Path is not a valid PE image: $What at 0x$($Offset.ToString('x')) (+$Size bytes) runs past the end of the file."
    }
}

Assert-Readable 0 ($PeSignatureOffsetField + 4) 'DOS header'
if ((Get-U16 $bytes 0) -ne $MzSignature) { throw "$Path is not a PE image (no MZ)." }
$peOffset = [long](Get-U32 $bytes $PeSignatureOffsetField)
Assert-Readable $peOffset ($OptionalHeaderOffset + 2) 'PE and COFF headers'
$pe = [int]$peOffset
if ((Get-U32 $bytes $pe) -ne $PeSignature) { throw "$Path is not a PE image (no PE signature)." }

# Checked rather than assumed: every offset below is derived from these three
# fields, and a truncated download is the normal way they come back wrong. An
# unrecognised magic used to fall through to the PE32 layout, so a ROM image or
# a corrupt header put $dirBase somewhere arbitrary and the walk proceeded on
# whatever bytes were there.
$optMagic = Get-U16 $bytes ($pe + $OptionalHeaderOffset)
if ($optMagic -ne $Pe32Magic -and $optMagic -ne $Pe32PlusMagic) {
    throw "$Path has optional header magic 0x$($optMagic.ToString('x')) - not PE32 (0x10b) or PE32+ (0x20b)."
}
$dataDirOffset = $(if ($optMagic -eq $Pe32PlusMagic) { $DataDirectoryOffsetPe32Plus } else { $DataDirectoryOffsetPe32 })
$rvaCountField = $(if ($optMagic -eq $Pe32PlusMagic) { $RvaCountFieldPe32Plus } else { $RvaCountFieldPe32 })

# Bounded to the LAST directory this script reads, the certificate table at
# index 4, not to the resource entry at index 2. The section table begins at
# $pe + $OptionalHeaderOffset + $optHeaderSize, so a short header puts a data
# directory read inside a section header, and a section's VirtualSize and
# VirtualAddress then get used as a certificate offset and size.
$optHeaderSize = [int](Get-U16 $bytes ($pe + $OptionalHeaderSizeField))
$neededOptSize = $dataDirOffset + ($SecurityDirectoryIndex + 1) * 8
if ($optHeaderSize -lt $neededOptSize) {
    throw "$Path declares SizeOfOptionalHeader $optHeaderSize, too small to hold the data directories this reads ($neededOptSize needed). The section table starts where they would be."
}
Assert-Readable ($pe + $OptionalHeaderOffset + $rvaCountField) 4 'NumberOfRvaAndSizes'
$rvaCount = Get-U32 $bytes ($pe + $OptionalHeaderOffset + $rvaCountField)
if ($rvaCount -le $ResourceDirectoryIndex) {
    throw "$Path declares NumberOfRvaAndSizes $rvaCount, so it has no resource data directory to read."
}

$dirBase  = $pe + $OptionalHeaderOffset + $dataDirOffset
Assert-Readable $dirBase (($ResourceDirectoryIndex + 1) * 8) 'data directory'

$numSections = [int](Get-U16 $bytes ($pe + $SectionCountField))
$secBase     = $pe + $OptionalHeaderOffset + $optHeaderSize
Assert-Readable $secBase ([long]$numSections * $SectionHeaderSize) 'section table'
$sections = @()
for ($i = 0; $i -lt $numSections; $i++) {
    $s = $secBase + $i * $SectionHeaderSize
    $sections += [pscustomobject]@{
        VirtualSize    = Get-U32 $bytes ($s + $SectionVirtualSizeField)
        VirtualAddress = Get-U32 $bytes ($s + $SectionVirtualAddressField)
        RawSize        = Get-U32 $bytes ($s + $SectionRawSizeField)
        RawOffset      = Get-U32 $bytes ($s + $SectionRawOffsetField)
    }
}

# The walkers below read $bytes, $sections, $rsrcBase and $rsrcLimit from
# script scope: they are the one parsed image this script exists to work on,
# and threading them through every recursive call would say nothing a reader
# does not already know.
function Get-RvaSection {
    param([uint32]$rva)

    foreach ($s in $sections) {
        $span = [Math]::Max($s.VirtualSize, $s.RawSize)
        if ($rva -ge $s.VirtualAddress -and $rva -lt ([long]$s.VirtualAddress + $span)) { return $s }
    }
    throw "RVA 0x$($rva.ToString('x')) is outside every section."
}

function Convert-RvaToOffset {
    [OutputType([int])]
    param([uint32]$rva, [long]$size)

    $s = Get-RvaSection $rva
    $delta = [long]$rva - [long]$s.VirtualAddress
    # Only the first RawSize bytes of a section are in the file; the rest is
    # zero-filled by the loader. An RVA in that tail has no bytes to read, and
    # RawOffset + delta then addresses whatever section follows it on disk -
    # which is how a bad resource entry ends up zeroing the loader's own code
    # in a DLL we go on to hash, commit and redistribute.
    if (($delta + $size) -gt [long]$s.RawSize) {
        throw "RVA 0x$($rva.ToString('x')) (+$size bytes) is not backed by raw data in $Path."
    }
    $offset = [long]$s.RawOffset + $delta
    Assert-Readable $offset $size 'resource data'
    return [int]$offset
}

$rsrcRva = Get-U32 $bytes ($dirBase + $ResourceDirectoryIndex * 8)
if ($rsrcRva -eq 0) { throw "$Path has no resource directory." }
$rsrcSection = Get-RvaSection $rsrcRva
$rsrcBase    = Convert-RvaToOffset $rsrcRva $ResourceDirectoryHeaderSize
# Every directory and data entry the walk visits is addressed relative to
# $rsrcBase, so this is the window all of them have to stay inside.
$rsrcLimit   = [long]$rsrcSection.RawOffset + $rsrcSection.RawSize
Assert-Readable $rsrcBase ($rsrcLimit - $rsrcBase) 'resource section'

function Assert-InResourceSection {
    param([long]$Offset, [long]$Size, [string]$What)

    if ($Offset -lt $rsrcBase -or $Size -lt 0 -or ($Offset + $Size) -gt $rsrcLimit) {
        throw "$Path has a malformed resource directory: $What at 0x$($Offset.ToString('x')) (+$Size bytes) lies outside the resource section."
    }
}

# Walk type -> name -> language and collect the RCDATA leaves. The type id is
# the entry name at level 0 and is carried down from there.
function Get-ResourceLeaves {
    param([int]$dirOffset, [int]$level, [uint32]$typeId)

    Assert-InResourceSection $dirOffset $ResourceDirectoryHeaderSize 'resource directory'
    $named = [int](Get-U16 $bytes ($dirOffset + $ResourceNamedEntryCountField))
    $ids   = [int](Get-U16 $bytes ($dirOffset + $ResourceIdEntryCountField))
    Assert-InResourceSection ($dirOffset + $ResourceEntriesOffset) `
        ([long]($named + $ids) * $ResourceEntrySize) 'resource directory entries'
    $out = @()
    for ($i = 0; $i -lt ($named + $ids); $i++) {
        $entry = $dirOffset + $ResourceEntriesOffset + $i * $ResourceEntrySize
        $nameField = Get-U32 $bytes $entry
        $dataField = Get-U32 $bytes ($entry + $ResourceEntryDataField)
        $thisType = $(if ($level -eq 0) { $nameField } else { $typeId })
        if (($dataField -band $ResourceSubdirectoryFlag) -ne 0) {
            if ($level -ge $ResourceMaxLevel) {
                throw "$Path has a resource subdirectory below the language level - the directory is malformed or points back at one of its own ancestors."
            }
            $child = [long]$rsrcBase + [long]($dataField -band $ResourceOffsetMask)
            Assert-InResourceSection $child $ResourceDirectoryHeaderSize 'resource subdirectory'
            $out += Get-ResourceLeaves ([int]$child) ($level + 1) $thisType
        } elseif ($level -ge 1 -and $typeId -eq $ResourceTypeRcData) {
            $leaf = [long]$rsrcBase + [long]$dataField
            Assert-InResourceSection $leaf $ResourceDataEntrySize 'resource data entry'
            $leaf = [int]$leaf
            $size = [long](Get-U32 $bytes ($leaf + $ResourceLeafSizeField))
            # Assert-InResourceSection above confines the 16-byte data ENTRY;
            # the RVA stored inside it is a separate address and has to be
            # confined too. Convert-RvaToOffset only asks whether that RVA is
            # backed by raw data in whatever section it lands in, so an entry
            # pointing into .text resolves to a raw offset in the loader's own
            # code - and if those bytes happen to start with MZ, the strip zeroes
            # them and -VerifyOnly then finds nothing left to report.
            $payloadOffset = Convert-RvaToOffset (Get-U32 $bytes $leaf) $size
            Assert-InResourceSection $payloadOffset $size 'resource payload'
            $out += [pscustomobject]@{
                Offset = $payloadOffset
                Size   = [int]$size
                Id     = $nameField
            }
        }
    }
    return $out
}

# What a payload looks like from the outside: an embedded DLL starts with its
# own MZ header, the windower's ini template with its first section header.
# Anything else in RCDATA is the loader's own data and is left alone.
function Get-ThirdPartyPayloads {
    $found = @()
    foreach ($leaf in (Get-ResourceLeaves $rsrcBase 0 0)) {
        if ($leaf.Size -lt 2) { continue }
        $head = [System.Text.Encoding]::ASCII.GetString($bytes, $leaf.Offset, [Math]::Min(12, $leaf.Size))
        $what = $null
        if ($head.StartsWith('MZ')) { $what = 'embedded DLL' }
        elseif ($head.StartsWith('[WINDOWMODE]')) { $what = 'windower ini template' }
        if ($what) { $found += [pscustomobject]@{ Offset = $leaf.Offset; Size = $leaf.Size; What = $what } }
    }
    return $found
}

# Upstream ships the loader Authenticode-signed (CN=FusionFix), and the
# certificate table is the only part of a PE that is addressed by file offset
# rather than by RVA: it is appended after the last section, and its
# offset + size is the file length. Zeroing a megabyte inside .rsrc breaks the
# hash that signature covers, and Get-AuthenticodeSignature then reports the
# vendored copy as HashMismatch - "changed by an unauthorized user or process" -
# which is a strictly worse thing to drop into a game's bin\ than an unsigned
# DLL, and is exactly the shape an AV engine flags. Removing the table instead
# leaves the copy we redistribute cleanly unsigned.
function Get-CertificateTable {
    if ($rvaCount -le $SecurityDirectoryIndex) { return $null }
    $entry = $dirBase + $SecurityDirectoryIndex * 8
    Assert-Readable $entry 8 'certificate data directory'
    $certOffset = [long](Get-U32 $bytes $entry)
    $certSize   = [long](Get-U32 $bytes ($entry + 4))
    if ($certOffset -eq 0 -or $certSize -eq 0) { return $null }
    Assert-Readable $certOffset $certSize 'certificate table'
    # The removal below truncates the file at $certOffset, which is only the
    # certificate table when the table is the last thing in the image. Upstream
    # signs after appending, so it is - but this loader also carries a 3 MB
    # appended PDB blob, and a toolchain that ever appends after signing would
    # otherwise have it silently eaten by a message reporting the table's size.
    if (($certOffset + $certSize) -ne $bytes.Length) {
        throw "$Path has a certificate table at 0x$($certOffset.ToString('x')) (+$certSize bytes) that does not end at the end of the file ($($bytes.Length) bytes). Removing it would discard whatever follows."
    }
    return [pscustomobject]@{ Entry = $entry; Offset = $certOffset; Size = $certSize }
}

# The optional header CheckSum, computed the way the loader does: a 16-bit
# one's-complement sum over the whole image with the CheckSum field itself read
# as zero, folded, plus the file length. Left stale, it is the other half of the
# hand-patched-binary fingerprint that PE triage tools and AV engines report.
function Get-PeCheckSum {
    param([byte[]]$Image)

    $field = $pe + $OptionalHeaderOffset + $CheckSumField
    $sum   = [long]0
    for ($i = 0; $i + 1 -lt $Image.Length; $i += 2) {
        if ($i -ge $field -and $i -lt ($field + 4)) { continue }
        $sum += [BitConverter]::ToUInt16($Image, $i)
    }
    if ($Image.Length % 2) { $sum += $Image[$Image.Length - 1] }
    while ($sum -gt 0xFFFF) { $sum = ($sum -band 0xFFFF) + ($sum -shr 16) }
    return [uint32]($sum + $Image.Length)
}

# @() around the call: PowerShell unrolls a function's empty-array return to
# $null, and an already-stripped loader is the common case here.
$payloads   = @(Get-ThirdPartyPayloads)
$cert       = Get-CertificateTable
$storedSum  = Get-U32 $bytes ($pe + $OptionalHeaderOffset + $CheckSumField)

if ($VerifyOnly) {
    $problems = @()
    foreach ($p in $payloads) {
        Write-Host ("  {0} at 0x{1:x} ({2:N0} bytes)" -f $p.What, $p.Offset, $p.Size) -ForegroundColor Red
    }
    if ($payloads.Count -gt 0) {
        $problems += "$($payloads.Count) third-party RCDATA payload(s) still present"
    }
    if ($cert) {
        $problems += "an Authenticode certificate table at 0x$($cert.Offset.ToString('x')) that no longer matches the modified image"
    }
    $computed = Get-PeCheckSum $bytes
    if ($storedSum -ne $computed) {
        $problems += "a stale PE CheckSum (header says 0x$($storedSum.ToString('x')), the image hashes to 0x$($computed.ToString('x')))"
    }
    if ($problems.Count -gt 0) {
        throw "$Path carries $($problems -join '; '). Run: pixi run strip-loader"
    }
    Write-Host "  loader payload: already stripped, unsigned, checksum current" -ForegroundColor DarkGray
    exit 0
}

$total = 0
foreach ($p in $payloads) {
    [Array]::Clear($bytes, $p.Offset, $p.Size)
    $total += $p.Size
    Write-Host ("  stripped {0} at 0x{1:x} ({2:N0} bytes)" -f $p.What, $p.Offset, $p.Size) -ForegroundColor Yellow
}

if ($cert) {
    $trimmed = New-Object byte[] $cert.Offset
    [Array]::Copy($bytes, 0, $trimmed, 0, $cert.Offset)
    $bytes = $trimmed
    [Array]::Clear($bytes, $cert.Entry, 8)
    Write-Host ("  removed the Authenticode certificate table at 0x{0:x} ({1:N0} bytes)" -f $cert.Offset, $cert.Size) -ForegroundColor Yellow
}

$computed = Get-PeCheckSum $bytes
if ($total -eq 0 -and -not $cert -and $storedSum -eq $computed) {
    Write-Host "  loader payload: already stripped, unsigned, checksum current" -ForegroundColor DarkGray
    exit 0
}

[Array]::Copy([BitConverter]::GetBytes($computed), 0,
              $bytes, $pe + $OptionalHeaderOffset + $CheckSumField, 4)
[System.IO.File]::WriteAllBytes($Path, $bytes)
Write-Host ("  {0:N0} bytes of third-party payload removed from {1}; CheckSum set to 0x{2:x}" -f `
            $total, (Split-Path $Path -Leaf), $computed) -ForegroundColor Green
