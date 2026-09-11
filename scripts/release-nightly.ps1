#!/usr/bin/env pwsh
#Requires -Version 5.1
# Thin shim - build, package, hash and publish logic lives in
# cameraunlock-core/powershell/NightlyRelease.psm1.

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ModVersion.psm1') -Force

$version = Get-ModVersion -ProjectRoot $ProjectRoot

Publish-NightlyBuild `
    -ModId 'portal' `
    -ModName 'PortalHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
