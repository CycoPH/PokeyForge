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
if (Test-Path $stage) {
    # Sometimes the stage dir is locked by an open File Explorer window
    # or a previous test launch of the exe. Try the clean removal first,
    # then fall back to renaming the locked dir out of the way so the
    # build can proceed.
    try {
        Remove-Item $stage -Recurse -Force -ErrorAction Stop
    } catch {
        $bak = "$stage.locked-$(Get-Date -Format 'yyyyMMddHHmmss')"
        Write-Host "Stage dir locked, moving aside to $bak" -ForegroundColor Yellow
        Move-Item -Path $stage -Destination $bak -Force
    }
}
New-Item -ItemType Directory -Force $stage | Out-Null

$runtime = @(
    'PokeyForge.exe',
    'SDL3.dll',
    'SDL3_ttf.dll',
    'sa_pokey.dll',
    'sa_c6502.dll',
    'rmt_driver_v2.obx',
    'JetBrainsMono-Regular.ttf'
)
foreach ($f in $runtime) {
    $src = Join-Path $out $f
    if (-not (Test-Path $src)) { throw "Missing build output: $src" }
    Copy-Item $src $stage
}

$docs = @('Readme.md','LICENSE','CHANGELOG.md')
foreach ($d in $docs) {
    if (Test-Path $d) { Copy-Item $d $stage }
}

# --- bundle instruments + pre-analyse ------------------------------------
# Ship the example instruments folder so users see a populated library on
# first launch, and run PokeyForge --analyse against the staged copy so
# the bundled analysis.json + analysis_report.csv are already current
# (no ~30s splash on first run).
$srcInstr = Join-Path $PSScriptRoot 'instruments'
if (Test-Path $srcInstr) {
    $stageInstr = Join-Path $stage 'instruments'
    Write-Host "Copying instruments -> $stageInstr" -ForegroundColor Cyan
    Copy-Item $srcInstr $stageInstr -Recurse

    # Strip any analysis byproducts that snuck in from the source tree;
    # we want the ones this exe produces in a moment, not whatever was
    # sitting in the repo from local testing.
    $byproducts = @(
        'analysis.json', 'analysis_report.csv',
        'analysis_debug.log', 'analysis_first.raw',
        'audio_debug.log', 'scratch.raw', 'scratch_first.raw'
    )
    foreach ($stale in $byproducts) {
        $p = Join-Path $stageInstr $stale
        if (Test-Path $p) { Remove-Item $p -Force }
    }

    Write-Host "Pre-analysing bundled instruments ..." -ForegroundColor Cyan
    $exe  = Join-Path $stage 'PokeyForge.exe'
    $proc = Start-Process -FilePath $exe `
                          -ArgumentList '--analyse', $stageInstr `
                          -Wait -NoNewWindow -PassThru
    if ($proc.ExitCode -ne 0) {
        throw "PokeyForge --analyse failed (exit $($proc.ExitCode))"
    }
    $jsonPath = Join-Path $stageInstr 'analysis.json'
    if (-not (Test-Path $jsonPath)) {
        throw "PokeyForge --analyse did not produce analysis.json"
    }
    $jsonKB = (Get-Item $jsonPath).Length / 1KB
    Write-Host ("analysis.json: {0:N1} KB" -f $jsonKB) -ForegroundColor Green
}
else {
    Write-Host "No instruments/ folder in source tree - skipping bundle." -ForegroundColor Yellow
}

# --- zip -----------------------------------------------------------------
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Write-Host ""
Write-Host "Staged at $stage" -ForegroundColor Green
Write-Host ("Zip:    $zip ({0:N1} KB)" -f ((Get-Item $zip).Length / 1KB)) -ForegroundColor Green
