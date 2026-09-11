#Requires -Version 5.1
<#
Prints the PE fingerprint of the installed Portal client.dll as a
paste-ready build-profile stub.

This is the first thing to run when a user reports the dormant log line
("no build profile matches this client.dll"), and the first step of a
rederive after a game patch. The mod routes on client.dll - not hl2.exe -
because that is the module CViewRender::RenderView lives in.

The registry the stub feeds lives under src/builds/ (steam_offsets.cpp holds
every Steam profile, build_registry.* holds the lookup order).
#>
param(
    [string]$GamePath,
    [string]$DllRelPath = 'portal\bin\client.dll'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectRoot 'cameraunlock-core\powershell\GamePathDetection.psm1') -Force

if (-not $GamePath) {
    $GamePath = Find-GamePath -GameId 'portal'
    if (-not $GamePath) { throw "Portal not found. Pass -GamePath explicitly." }
}

$dll = Join-Path $GamePath $DllRelPath
if (-not (Test-Path $dll)) { throw "client.dll not found at $dll" }

$bytes = [System.IO.File]::ReadAllBytes($dll)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
$sig = [BitConverter]::ToUInt32($bytes, $peOffset)
if ($sig -ne 0x00004550) { throw "Not a PE image: $dll" }

# COFF header follows the 4-byte signature; the optional header follows that.
$coff = $peOffset + 4
$timeDateStamp = [BitConverter]::ToUInt32($bytes, $coff + 4)
$optional = $coff + 20
$magic = [BitConverter]::ToUInt16($bytes, $optional)
if ($magic -ne 0x10B) { throw "Expected a 32-bit PE32 image (magic 0x$('{0:X}' -f $magic))" }
$checkSum = [BitConverter]::ToUInt32($bytes, $optional + 64)
$sizeOfImage = [BitConverter]::ToUInt32($bytes, $optional + 56)

$built = [DateTimeOffset]::FromUnixTimeSeconds($timeDateStamp).UtcDateTime
$stamp = $built.ToString('yyyyMMdd')

Write-Host ""
Write-Host "client.dll : $dll"
Write-Host "built      : $($built.ToString('yyyy-MM-dd HH:mm:ss')) UTC"
Write-Host ""
Write-Host "1. Append to src/builds/steam_offsets.cpp (never edit an existing profile):"
Write-Host ""
Write-Host "extern const BuildProfile kSteamProfile_$stamp = {"
Write-Host "    `"steam-win32-$stamp`","
Write-Host ("    {{ 0x{0:X8}u, 0x{1:X8}u, 0x{2:X8}u }}," -f $timeDateStamp, $sizeOfImage, $checkSum)
Write-Host "    { 0x0u, kViewSetupLayout_2025 },  // RenderView RVA - rederive before use"
Write-Host "};"
Write-Host ""
Write-Host "   A zero RVA keeps the profile a placeholder, so the mod stays dormant on"
Write-Host "   this build until the rederive lands. Confirm the CViewSetup layout still"
Write-Host "   fits before reusing kViewSetupLayout_2025; if it moved, add a new layout."
Write-Host ""
Write-Host "2. Declare it in src/builds/build_registry.h:"
Write-Host ""
Write-Host "extern const BuildProfile kSteamProfile_$stamp;"
Write-Host ""
Write-Host "3. Add it to the TOP of kKnownProfiles in src/builds/build_registry.cpp"
Write-Host "   (newest first - the top entry is the diagnostic primary):"
Write-Host ""
Write-Host "    &kSteamProfile_$stamp,"
Write-Host ""
