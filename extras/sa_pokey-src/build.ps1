#requires -Version 5

<#
.SYNOPSIS
    Build sa_pokey.dll from vendored AltirraSDL source.

.DESCRIPTION
    PokeyForge's analysis pipeline needs three custom exports on top of
    the stock Altirra RMT POKEY plugin (see patches/rmtinterface.cpp.patch
    for the actual diff): Pokey_GetAnalysisAbiVersion, Pokey_SetAudioTap,
    Pokey_SetMute. This script builds the four MSBuild projects vendored
    under src/ in dependency order and drops the resulting DLL where
    PokeyForge can pick it up.

    Run from a Visual Studio Developer PowerShell or from any shell where
    MSBuild is locatable (the script falls back to the VS 2022
    Professional default install path and finally to whatever is on PATH).

.PARAMETER Configuration
    MSBuild configuration name (default 'Release'). Use 'Debug' for an
    unoptimised build with full symbols, useful when stepping through
    the tap callback in WinDbg.

.PARAMETER Platform
    MSBuild platform name (default 'x64'). The shipped PokeyForge.exe is
    x64, so the matching DLL is x64 too. ARM64 should also work; Win32
    is not supported by PokeyForge.

.PARAMETER InstallTo
    Directory to copy sa_pokey.dll into after a successful build. Defaults
    to ..\..\runtime relative to this script. Pass an empty string to skip
    the install step.

.PARAMETER NoVerify
    Skip the post-build dumpbin /EXPORTS check that the three PokeyForge
    extension exports are present. Saves a few seconds when iterating.

.EXAMPLE
    .\build.ps1
    Build Release x64 and install into ..\..\runtime\sa_pokey.dll.

.EXAMPLE
    .\build.ps1 -Configuration Debug -InstallTo ''
    Debug build, leave the DLL in src\..\out\DebugAMD64\.
#>

param(
    [string]$Configuration = 'Release',
    [string]$Platform      = 'x64',
    [string]$InstallTo     = (Join-Path $PSScriptRoot '..\..\runtime'),
    [switch]$NoVerify
)

$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot

# ---- MSBuild resolution -------------------------------------------------
function Resolve-MSBuild {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
    )
    foreach ($p in $candidates) { if (Test-Path $p) { return $p } }
    $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "MSBuild not found. Install Visual Studio 2022 with the 'Desktop development with C++' workload, or run from a Developer PowerShell."
}

$msbuild = Resolve-MSBuild
Write-Host "MSBuild: $msbuild" -ForegroundColor Cyan
Write-Host "Building $Configuration|$Platform" -ForegroundColor Cyan
Write-Host ''

# ---- Dependency-ordered build -------------------------------------------
# system  -> ATCore  -> ATAudio  -> AltirraRMTPOKEY
# The AltirraRMT.sln itself only exposes Win32 (x86) configs, so we build
# each .vcxproj directly with /p:Platform=x64. Altirra.props handles the
# real platform mapping (VDPlatformDirTag -> AMD64, etc.).
$projects = @(
    'src\system\system.vcxproj',
    'src\ATCore\ATCore.vcxproj',
    'src\ATAudio\ATAudio.vcxproj',
    'src\AltirraRMTPOKEY\AltirraRMTPOKEY.vcxproj'
)

foreach ($proj in $projects) {
    Write-Host "==> $proj" -ForegroundColor Yellow
    & $msbuild $proj `
        "/p:Configuration=$Configuration" `
        "/p:Platform=$Platform" `
        '/v:minimal' `
        '/nologo' `
        '/m'
    if ($LASTEXITCODE -ne 0) { throw "$proj failed (exit $LASTEXITCODE)" }
    Write-Host ''
}

# ---- Locate the produced DLL --------------------------------------------
# Altirra.props sets VDOutputPath = $(VDBaseDir)\out\$(VDConfigDirTag)$(VDPlatformDirTag)
# with VDBaseDir = src\..\ and VDPlatformDirTag = AMD64 for x64. So the
# DLL lands in src\..\out\<Cfg>AMD64\sa_pokey.dll.
$platTag = switch ($Platform) {
    'x64'   { 'AMD64' }
    'ARM64' { 'ARM64' }
    default { '' }
}
$outDir = Join-Path $PSScriptRoot "out\$Configuration$platTag"
$dll    = Join-Path $outDir 'sa_pokey.dll'
if (-not (Test-Path $dll)) { throw "Build claimed success but $dll is missing." }
$sz = [int]((Get-Item $dll).Length / 1KB)
Write-Host "Built: $dll ($sz KB)" -ForegroundColor Green

# ---- Verify exports -----------------------------------------------------
if (-not $NoVerify) {
    # dumpbin lives next to cl.exe under VC\Tools\MSVC\<ver>\bin\Hostx64\x64.
    # Easiest robust find is via the VS installation that owns $msbuild.
    $vsRoot = $msbuild -replace '\\MSBuild\\Current\\Bin\\amd64\\MSBuild\.exe$', ''
    $dumpbin = Get-ChildItem -Path (Join-Path $vsRoot 'VC\Tools\MSVC') -Filter 'dumpbin.exe' -Recurse -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -match 'Hostx64\\x64' } |
               Select-Object -First 1
    if ($dumpbin) {
        $exports = & $dumpbin.FullName /EXPORTS $dll 2>$null | Select-String -Pattern 'Pokey_SetAudioTap|Pokey_SetMute|Pokey_GetAnalysisAbiVersion'
        $count = ($exports | Measure-Object).Count
        if ($count -ne 3) {
            throw "Expected 3 PokeyForge extension exports, found $count. Patch was not applied or build picked up stale objs."
        }
        Write-Host "Verified PokeyForge extension exports (3/3):" -ForegroundColor Green
        $exports | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
    } else {
        Write-Host "dumpbin not found under $vsRoot; skipping export verification." -ForegroundColor Yellow
    }
}

# ---- Install ------------------------------------------------------------
if ($InstallTo) {
    $installDir = (Resolve-Path -LiteralPath $InstallTo -ErrorAction SilentlyContinue).Path
    if (-not $installDir) {
        New-Item -ItemType Directory -Force $InstallTo | Out-Null
        $installDir = (Resolve-Path -LiteralPath $InstallTo).Path
    }
    $target = Join-Path $installDir 'sa_pokey.dll'
    Copy-Item $dll $target -Force
    Write-Host ''
    Write-Host "Installed: $target" -ForegroundColor Green
} else {
    Write-Host ''
    Write-Host "Install step skipped (no -InstallTo). DLL is at $dll." -ForegroundColor Yellow
}
