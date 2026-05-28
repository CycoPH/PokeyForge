# PokeyForge release builder.
#
# Builds the Release configuration, stages the runtime DLLs + driver +
# license + docs next to the exe, and produces a versioned zip under dist/.
# Run from the repo root: `./release.ps1`. Use `-NoBuild` to repackage without
# rebuilding (e.g. when invoked from CI after a separate build step).

param(
    [string]$Configuration = 'Release',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot

# --- read version from src/Version.h --------------------------------------
$versionHeader = Join-Path $PSScriptRoot 'src\Version.h'
$match = Select-String -Path $versionHeader -Pattern 'POKEYFORGE_VERSION\s+"([^"]+)"'
if (-not $match) { throw "Could not read POKEYFORGE_VERSION from $versionHeader" }
$version = $match.Matches[0].Groups[1].Value
Write-Host "PokeyForge $version - $Configuration" -ForegroundColor Cyan

# --- build ---------------------------------------------------------------
if (-not $NoBuild) {
    $msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe'
    if (-not (Test-Path $msbuild)) {
        # Fall back to whatever msbuild is on PATH (used by CI runners).
        $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
        if (-not $cmd) { throw "MSBuild not found at $msbuild and not on PATH" }
        $msbuild = $cmd.Source
    }
    & $msbuild 'PokeyForge.sln' "/p:Configuration=$Configuration" '/p:Platform=x64' '/v:minimal'
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed (exit $LASTEXITCODE)" }
}

# --- stage ---------------------------------------------------------------
$out   = Join-Path 'build' $Configuration
$dist  = 'dist'
$stage = Join-Path $dist "PokeyForge-$version-win64"
$zip   = Join-Path $dist "PokeyForge-$version-win64.zip"

if (-not (Test-Path $dist)) { New-Item -ItemType Directory -Force $dist | Out-Null }
if (Test-Path $stage)       { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

$runtime = @('PokeyForge.exe','SDL3.dll','sa_pokey.dll','sa_c6502.dll','rmt_driver_v2.obx')
foreach ($f in $runtime) {
    $src = Join-Path $out $f
    if (-not (Test-Path $src)) { throw "Missing build output: $src" }
    Copy-Item $src $stage
}

$docs = @('Readme.md','LICENSE','CHANGELOG.md')
foreach ($d in $docs) {
    if (Test-Path $d) { Copy-Item $d $stage }
}

# --- zip -----------------------------------------------------------------
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Write-Host ""
Write-Host "Staged at $stage" -ForegroundColor Green
Write-Host ("Zip:    $zip ({0:N1} KB)" -f ((Get-Item $zip).Length / 1KB)) -ForegroundColor Green
