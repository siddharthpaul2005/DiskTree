<#
.SYNOPSIS
    One-command build for disktree on a fresh Windows machine.

.DESCRIPTION
    1. Finds Visual Studio / Build Tools (MSVC + CMake + Ninja) via vswhere
    2. Loads the x64 developer environment into this PowerShell session
    3. Finds vcpkg ($env:VCPKG_ROOT), or clones + bootstraps it into %USERPROFILE%\vcpkg
    4. Configures, builds and (optionally) tests with CMake presets

    First run downloads and compiles the dependencies (~10-20 min).
    Later runs reuse vcpkg's binary cache and take seconds.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Test
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Preset dev -Test
#>
[CmdletBinding()]
param(
    [ValidateSet('release-static', 'dev')]
    [string]$Preset = 'release-static',
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# ── 1. Visual Studio / Build Tools ────────────────────────────────
Write-Step 'Locating Visual Studio C++ tools'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1
}
if (-not $vsPath) {
    Fail @'
MSVC C++ build tools not found. Install them (one time, ~3 GB) with:

  winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"

then open a NEW PowerShell window and run this script again.
'@
}
Write-Host "    $vsPath"

# Remember a user-provided vcpkg before vcvars overrides VCPKG_ROOT
# with Visual Studio's bundled copy.
$userVcpkg = $env:VCPKG_ROOT

# ── 2. Load the x64 developer environment ─────────────────────────
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { Fail "vcvars64.bat not found at $vcvars" }
Write-Step 'Loading MSVC x64 environment'
$envDump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
    }
}

# CMake + Ninja ship with the "C++ CMake tools" component; fall back to its path
$cmakeDir = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake'
foreach ($p in @("$cmakeDir\CMake\bin", "$cmakeDir\Ninja")) {
    if ((Test-Path $p) -and ($env:Path -notlike "*$p*")) { $env:Path = "$p;$env:Path" }
}
foreach ($tool in 'cl', 'cmake', 'ninja', 'git') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        if ($tool -eq 'git') { Fail 'git not found. Install it with:  winget install --id Git.Git' }
        Fail "$tool not found. Re-run the Build Tools installer and add 'C++ CMake tools for Windows'."
    }
}

# ── 3. vcpkg ──────────────────────────────────────────────────────
Write-Step 'Locating vcpkg'
$bundled = Join-Path $vsPath 'VC\vcpkg'
if ($userVcpkg -and (Test-Path (Join-Path $userVcpkg 'vcpkg.exe')) -and
    ((Resolve-Path $userVcpkg).Path -ne (Resolve-Path $bundled -ErrorAction SilentlyContinue).Path)) {
    $vcpkgRoot = $userVcpkg
} else {
    $vcpkgRoot = Join-Path $env:USERPROFILE 'vcpkg'
    if (-not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
        if (-not (Test-Path (Join-Path $vcpkgRoot '.git'))) {
            Write-Step "Cloning vcpkg into $vcpkgRoot"
            git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
            if ($LASTEXITCODE) { Fail 'git clone of vcpkg failed' }
        }
        Write-Step 'Bootstrapping vcpkg'
        & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE) { Fail 'vcpkg bootstrap failed' }
    }
}
$env:VCPKG_ROOT = $vcpkgRoot
Write-Host "    $vcpkgRoot"

# ── 4. Configure / build / test ───────────────────────────────────
Push-Location $repo
try {
    $binDir = if ($Preset -eq 'dev') { 'build\dev' } else { 'build\release' }
    if ($Clean -and (Test-Path $binDir)) {
        Write-Step "Removing $binDir"
        Remove-Item -Recurse -Force $binDir
    }

    Write-Step "Configuring ($Preset) - first run builds dependencies, this can take a while"
    cmake --preset $Preset
    if ($LASTEXITCODE) { Fail 'CMake configure failed' }

    Write-Step 'Building'
    cmake --build --preset $Preset
    if ($LASTEXITCODE) { Fail 'Build failed' }

    if ($Test) {
        Write-Step 'Running tests'
        ctest --preset $Preset
        if ($LASTEXITCODE) { Fail 'Tests failed' }
    }

    $exe = Join-Path $repo "$binDir\disktree.exe"
    Write-Host ''
    Write-Host "Build OK: $exe" -ForegroundColor Green
    Write-Host "Try:  & `"$exe`" . --top 10"
} finally {
    Pop-Location
}
