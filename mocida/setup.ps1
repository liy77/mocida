<#
.SYNOPSIS
    Mocida - full project bootstrap on a fresh Windows machine.

.DESCRIPTION
    1. Installs (user scope, via winget) any of the required tools that
       are missing: Git, CMake, LLVM (clang), Ninja, GnuWin32 Make.
    2. Bootstraps a local copy of vcpkg and installs libcurl
       (x64-windows), the single external dep required by
       find_package(CURL REQUIRED).
    3. Clones SDL / SDL_image / SDL_ttf at the exact commits referenced
       by the repository (mode 160000 gitlinks, no .gitmodules at root).
    4. Initializes the nested submodules of SDL_image and SDL_ttf
       (libpng, freetype, etc. - they live inside those repos).
    5. Runs the first Debug build via build.bat.

.PARAMETER NoBuild
    Configures everything but skips the first build.

.PARAMETER Force
    Re-checks out the SDL pinned commits even when the folders already
    exist.

.PARAMETER SkipInstall
    Does not call winget. Only validates that the tools are present.

.EXAMPLE
    .\setup.ps1
    .\setup.ps1 -NoBuild
    .\setup.ps1 -Force -SkipInstall
#>

[CmdletBinding()]
param(
    [switch]$NoBuild,
    [switch]$Force,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
Set-Location $Root

# ---------------------------------------------------------------------------
# Project constants
# ---------------------------------------------------------------------------

# Pinned SDL commits (from `git ls-tree HEAD` on the original repo).
$SdlPins = @(
    [pscustomobject]@{
        Dir = 'SDL'
        Url = 'https://github.com/libsdl-org/SDL.git'
        Sha = '877399b2b2cf21e67554ed9046410f268ce1d1b2'
    }
    [pscustomobject]@{
        Dir = 'SDL_image'
        Url = 'https://github.com/libsdl-org/SDL_image.git'
        Sha = '11154afb7855293159588b245b446a4ef09e574f'
    }
    [pscustomobject]@{
        Dir = 'SDL_ttf'
        Url = 'https://github.com/libsdl-org/SDL_ttf.git'
        Sha = 'a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b'
    }
)

# Required tools + winget id + common install directories used as PATH
# fallbacks (winget updates the persistent PATH but the current session
# already captured the old one - we inject manually so the user doesn't
# need to open a new terminal between install and use).
$Tools = @(
    [pscustomobject]@{
        Cmd = 'git';   WingetId = 'Git.Git'
        Probes = @('C:\Program Files\Git\cmd', "$env:LocalAppData\Programs\Git\cmd")
    }
    [pscustomobject]@{
        Cmd = 'cmake'; WingetId = 'Kitware.CMake'
        Probes = @('C:\Program Files\CMake\bin', "$env:LocalAppData\Programs\CMake\bin")
    }
    [pscustomobject]@{
        Cmd = 'clang'; WingetId = 'LLVM.LLVM'
        Probes = @('C:\Program Files\LLVM\bin', "$env:LocalAppData\Programs\LLVM\bin")
    }
    [pscustomobject]@{
        Cmd = 'ninja'; WingetId = 'Ninja-build.Ninja'
        Probes = @("$env:LocalAppData\Microsoft\WinGet\Links")
    }
    [pscustomobject]@{
        # build.bat usa -G "Unix Makefiles", entao precisamos de `make`.
        Cmd = 'make';  WingetId = 'GnuWin32.Make'
        Probes = @('C:\Program Files (x86)\GnuWin32\bin')
    }
)

# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------
function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host "=== $msg ===" -ForegroundColor Cyan
}
function Write-Ok   ([string]$msg) { Write-Host "  [OK]   $msg" -ForegroundColor Green  }
function Write-Info ([string]$msg) { Write-Host "  [..]   $msg" -ForegroundColor Gray   }
function Write-Warn2([string]$msg) { Write-Host "  [!]    $msg" -ForegroundColor Yellow }
function Write-Err2 ([string]$msg) { Write-Host "  [X]    $msg" -ForegroundColor Red    }

# ---------------------------------------------------------------------------
# Clone helper - unified path for all external repos. With a non-empty
# $sha we do a full clone + checkout (used for the SDL pins). With an
# empty $sha we shallow-clone the default branch (vcpkg / mimalloc -
# disk size matters more than reproducibility there).
# ---------------------------------------------------------------------------
function Invoke-ClonePinned([string]$dir, [string]$url, [string]$sha) {
    $target = Join-Path $Root $dir
    $pinned = -not [string]::IsNullOrWhiteSpace($sha)

    if (Test-Path (Join-Path $target '.git')) {
        if ($pinned -and $Force) {
            Write-Info "$dir : --force, re-checking out $sha"
            Push-Location $target
            try {
                & git fetch --quiet origin
                & git checkout --quiet $sha
                if ($LASTEXITCODE -ne 0) { throw "checkout $sha failed in $dir." }
            } finally { Pop-Location }
        } else {
            $hint = if ($pinned) { ' (use -Force to re-checkout)' } else { '' }
            Write-Ok "$dir already cloned$hint"
        }
        return
    }

    if (Test-Path $target) {
        throw "$dir exists but is not a git repo. Remove it and run again."
    }

    if ($pinned) {
        Write-Info "$dir : cloning $url"
        & git clone --quiet $url $target
        if ($LASTEXITCODE -ne 0) { throw "git clone $url failed." }

        Push-Location $target
        try {
            & git checkout --quiet $sha
            if ($LASTEXITCODE -ne 0) { throw "checkout $sha failed in $dir." }
            Write-Ok "$dir @ $sha"
        } finally { Pop-Location }
    } else {
        Write-Info "$dir : shallow clone $url"
        & git clone --depth 1 --quiet $url $target
        if ($LASTEXITCODE -ne 0) { throw "git clone $url failed." }
        Write-Ok "$dir ready"
    }
}

# ---------------------------------------------------------------------------
# PATH / detection helpers
# ---------------------------------------------------------------------------

function Sync-PathFromRegistry {
    # Re-reads the persistent PATH (Machine + User) into this session.
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = ($machine, $user | Where-Object { $_ }) -join ';'
}

function Add-PathDir([string]$dir) {
    if (-not $dir) { return }
    if (-not (Test-Path $dir)) { return }
    $parts = $env:Path -split ';'
    if ($parts -notcontains $dir) {
        $env:Path = "$dir;$env:Path"
    }
}

function Test-Tool([string]$name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Find-Winget {
    $cmd = Get-Command winget -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # winget sometimes lives in WindowsApps and isn't on PATH yet.
    $candidate = Join-Path $env:LocalAppData 'Microsoft\WindowsApps\winget.exe'
    if (Test-Path $candidate) { return $candidate }
    return $null
}

function Install-Tool([pscustomobject]$tool, [string]$wingetExe) {
    Write-Info "installing $($tool.WingetId) ..."
    # --scope user avoids the UAC prompt whenever the package supports it.
    # If the package refuses user-scope (e.g. LLVM in some versions) we
    # retry without --scope.
    & $wingetExe install --id $tool.WingetId --silent `
        --accept-package-agreements --accept-source-agreements `
        --scope user 2>&1 | Out-Host
    $code = $LASTEXITCODE

    if ($code -ne 0) {
        Write-Warn2 "winget returned $code with --scope user; retrying without --scope ..."
        & $wingetExe install --id $tool.WingetId --silent `
            --accept-package-agreements --accept-source-agreements 2>&1 | Out-Host
        $code = $LASTEXITCODE
    }

    # 0 = success. We also accept the codes meaning "already installed".
    # 0x8A15002B (-1978335189) = NoUpgrade; 0x8A150019 = no applicable upgrade.
    $okCodes = @(0, -1978335189, -1978335207)
    if ($okCodes -notcontains $code) {
        throw "Failed to install $($tool.WingetId) (winget exit $code)."
    }

    Sync-PathFromRegistry
    foreach ($p in $tool.Probes) { Add-PathDir $p }

    if (-not (Test-Tool $tool.Cmd)) {
        Write-Warn2 ("'{0}' installed but not yet visible on this session's PATH. " +
                     "The next steps may fail - open a new terminal and re-run setup.bat.") -f $tool.Cmd
    } else {
        Write-Ok "$($tool.Cmd) available"
    }
}

# ---------------------------------------------------------------------------
# 1) Tools
# ---------------------------------------------------------------------------
Write-Step "1/5  Checking / installing tools"

# Make sure the common fallback dirs are visible before we evaluate.
Sync-PathFromRegistry
foreach ($t in $Tools) { foreach ($p in $t.Probes) { Add-PathDir $p } }

$wingetExe = $null
if (-not $SkipInstall) {
    $wingetExe = Find-Winget
    if (-not $wingetExe) {
        throw "winget not found. Install 'App Installer' from the Microsoft Store, or run with -SkipInstall."
    }
    Write-Info "winget: $wingetExe"
}

foreach ($t in $Tools) {
    if (Test-Tool $t.Cmd) {
        Write-Ok "$($t.Cmd) already installed"
        continue
    }
    if ($SkipInstall) {
        Write-Err2 "$($t.Cmd) missing. Re-run without -SkipInstall to install via winget."
        exit 1
    }
    Install-Tool -tool $t -wingetExe $wingetExe
}

# ---------------------------------------------------------------------------
# 2) vcpkg + libcurl
# ---------------------------------------------------------------------------
Write-Step "2/5  Bootstrap vcpkg + libcurl"

$VcpkgDir       = Join-Path $Root 'vcpkg'
$VcpkgExe       = Join-Path $VcpkgDir 'vcpkg.exe'
$VcpkgBootstrap = Join-Path $VcpkgDir 'bootstrap-vcpkg.bat'
$ToolchainFile  = Join-Path $VcpkgDir 'scripts\buildsystems\vcpkg.cmake'

Invoke-ClonePinned -dir 'vcpkg' `
                   -url 'https://github.com/microsoft/vcpkg.git' `
                   -sha ''

if (-not (Test-Path $VcpkgExe)) {
    Write-Info "bootstrap-vcpkg.bat ..."
    & $VcpkgBootstrap -disableMetrics | Out-Host
    if (-not (Test-Path $VcpkgExe)) { throw "vcpkg bootstrap did not produce vcpkg.exe" }
}
Write-Ok "vcpkg.exe ready"

Write-Info "vcpkg install curl:x64-windows  (this can take a few minutes the first time)"
& $VcpkgExe install curl:x64-windows | Out-Host
if ($LASTEXITCODE -ne 0) { throw "vcpkg install curl:x64-windows failed." }
Write-Ok "libcurl installed (x64-windows)"

# Export the vcpkg toolchain for the current session. build.bat/release.bat
# detect ./vcpkg/scripts/buildsystems/vcpkg.cmake automatically, but
# exporting the env var also helps IDEs / CMake invocations outside build.bat.
$env:CMAKE_TOOLCHAIN_FILE = $ToolchainFile
Write-Ok "CMAKE_TOOLCHAIN_FILE = $ToolchainFile (this session)"

# ---------------------------------------------------------------------------
# 3) SDL family at pinned commits
# ---------------------------------------------------------------------------
Write-Step "3/5  Cloning SDL / SDL_image / SDL_ttf"

foreach ($pin in $SdlPins) {
    Invoke-ClonePinned -dir $pin.Dir -url $pin.Url -sha $pin.Sha
}

# Microsoft mimalloc - high-performance allocator. Unpinned because the
# project rarely breaks API and a shallow clone is enough for production
# builds. The .gitignore excludes the directory, so each environment
# fetches its own copy.
Invoke-ClonePinned -dir 'mimalloc' `
                   -url 'https://github.com/microsoft/mimalloc.git' `
                   -sha ''

# ---------------------------------------------------------------------------
# 4) Nested submodules (vendored inside SDL_image / SDL_ttf)
# ---------------------------------------------------------------------------
Write-Step "4/5  Nested submodules in SDL_image and SDL_ttf"

foreach ($d in @('SDL_image', 'SDL_ttf')) {
    $target = Join-Path $Root $d
    $gm = Join-Path $target '.gitmodules'
    if (Test-Path $gm) {
        Push-Location $target
        try {
            Write-Info "$d : git submodule update --init --recursive"
            & git submodule update --init --recursive
            if ($LASTEXITCODE -ne 0) { throw "submodule update failed in $d." }
            Write-Ok "$d submodules ready"
        } finally { Pop-Location }
    } else {
        Write-Info "$d : no .gitmodules, skipping"
    }
}

# ---------------------------------------------------------------------------
# 5) Initial build
# ---------------------------------------------------------------------------
Write-Step "5/5  Initial build (Debug)"

if ($NoBuild) {
    Write-Info "-NoBuild specified, skipping build."
} else {
    # --clean ensures that a pre-existing build/ with a different
    # generator (e.g. Visual Studio from an old CMake run) doesn't
    # break the first build with "Unix Makefiles" / Ninja.
    & (Join-Path $Root 'build.bat') --clean
    if ($LASTEXITCODE -ne 0) {
        Write-Err2 "build.bat returned $LASTEXITCODE"
        Write-Warn2 "Dependency setup completed but compilation failed - see the output above."
        exit 1
    }
}

Write-Host ""
Write-Host "=== Setup completed successfully ===" -ForegroundColor Green
Write-Host "  build.bat    -> Debug rebuild" -ForegroundColor Gray
Write-Host "  release.bat  -> Release + distribution zip" -ForegroundColor Gray
Write-Host ""
Write-Host "If this was the first time installing one of the tools in" -ForegroundColor Gray
Write-Host "this session, open a new terminal before the next build so" -ForegroundColor Gray
Write-Host "it inherits the updated PATH." -ForegroundColor Gray
