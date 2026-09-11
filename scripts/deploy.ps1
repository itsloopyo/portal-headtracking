#!/usr/bin/env pwsh
#Requires -Version 5.1
# Thin wrapper - dev-deploy orchestration lives in
# cameraunlock-core/powershell/DevDeploy.psm1, which resolves the game through
# GamePathDetection.psm1 in the same order install.cmd uses (env var, Steam
# registry, games.json, then the positional path argument).

param(
    [Parameter(Position = 0)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [Parameter(Position = 1)]
    [string]$GivenPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectRoot 'cameraunlock-core\powershell\DevDeploy.psm1') -Force

$buildOutput  = Join-Path $projectRoot "bin\$Configuration"
$vendorLoader = Join-Path $projectRoot 'vendor\ultimate-asi-loader\dinput8.dll'

# The same x86 check package-release.ps1 runs, for the same reason: Portal is a
# 32-bit process, so an x64 .asi installs cleanly, the game launches, and the
# mod does nothing. Without it here, a build configured without -A Win32 sends
# the dev looking at the loader rather than at the build. The packager catches
# it, but only at release.
$asiPath = Join-Path $buildOutput 'PortalHeadTracking.asi'
if (-not (Test-Path $asiPath)) {
    throw "Build output not found at $asiPath. Run: pixi run build"
}
$archCheck = Join-Path $projectRoot 'cameraunlock-core\scripts\check-loader-arch.ps1'
& $archCheck -Path $asiPath -ExpectedArch x86
if ($LASTEXITCODE -ne 0) {
    throw "$asiPath is not a 32-bit PE (check-loader-arch exit $LASTEXITCODE). Portal is a 32-bit process and will never load it. Reconfigure with: pixi run clean; pixi run build"
}

# ExeSubDir 'bin': Source loads tier0.dll from bin\ with an altered search
# path, so a proxy DLL sitting beside hl2.exe is never consulted. AsiLoaderName
# winmm.dll because bin\tier0.dll and bin\engine.dll import it and nothing
# Portal ships imports xinput.
$result = Invoke-DevDeployASILoader `
    -GameId 'portal' `
    -GameDisplayName 'Portal' `
    -BuildOutputPath $buildOutput `
    -ModDllName 'PortalHeadTracking.asi' `
    -VendorLoaderDll $vendorLoader `
    -AsiLoaderName 'winmm.dll' `
    -ExeSubDir 'bin' `
    -GivenPath $GivenPath

Write-Host ""
Write-Host "Deployed PortalHeadTracking.asi to: $($result.ExeDir)" -ForegroundColor Green
