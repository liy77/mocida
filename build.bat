@echo off
setlocal enabledelayedexpansion

:: Capture the absolute path of the script directory BEFORE any `cd`.
:: %~dp0 with a relative invocation (.\build.bat) resolves against the
:: current working directory at expansion time, so reading it after
:: `cd build` later in the script would point at the build dir, not the
:: project root. Snapshot it here while cwd is still the project root.
set "MOCIDA_ROOT=%~dp0"

echo Mocida Build Script - Debug Mode with Clang

:: ----------------------------------------------------------------------
:: Make sure tools installed via winget / GnuWin32 are reachable in this
:: session. winget drops "shims" under %LocalAppData%\Microsoft\WinGet\Links
:: that are not always on the PATH of console windows opened BEFORE the
:: install ran.
:: ----------------------------------------------------------------------
set "PATH=%LocalAppData%\Microsoft\WinGet\Links;C:\Program Files\LLVM\bin;%LocalAppData%\Programs\LLVM\bin;C:\Program Files\CMake\bin;C:\Program Files (x86)\GnuWin32\bin;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"

:: ----------------------------------------------------------------------
:: Make sure MSVC's headers / libs / RC compiler are on the environment.
:: clang-cl on Windows still needs the LIB / INCLUDE / PATH bits that
:: vcvarsall.bat exports; otherwise CMake's compiler check fails with
:: 'undefined symbol: mainCRTStartup' or 'CMAKE_RC_COMPILER not set'.
:: Skip if a Developer Command Prompt already set up VCINSTALLDIR.
:: ----------------------------------------------------------------------
if not defined VCINSTALLDIR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -prerelease -property installationPath`) do set "VSPATH=%%i"
        if defined VSPATH (
            if exist "!VSPATH!\VC\Auxiliary\Build\vcvarsall.bat" (
                echo Initialising MSVC environment from "!VSPATH!"...
                call "!VSPATH!\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
            )
        )
    )
)

:: ----------------------------------------------------------------------
:: Parse args. Flags are independent and order-agnostic:
::   --clean       drop build/ before configuring
::   --shared      build mocida as a DLL instead of a static .lib
::   --tests       also compile every tests/test_*.c into its own .exe
::   --no-demo     skip building the demo exe
:: ----------------------------------------------------------------------
set "DO_CLEAN=0"
set "MOCIDA_SHARED=OFF"
set "MOCIDA_TESTS=OFF"
set "MOCIDA_DEMO=ON"

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--clean"   (set "DO_CLEAN=1"        & shift & goto :parse_args)
if /I "%~1"=="--shared"  (set "MOCIDA_SHARED=ON"  & shift & goto :parse_args)
if /I "%~1"=="--static"  (set "MOCIDA_SHARED=OFF" & shift & goto :parse_args)
if /I "%~1"=="--tests"   (set "MOCIDA_TESTS=ON"   & shift & goto :parse_args)
if /I "%~1"=="--no-tests"(set "MOCIDA_TESTS=OFF"  & shift & goto :parse_args)
if /I "%~1"=="--no-demo" (set "MOCIDA_DEMO=OFF"   & shift & goto :parse_args)
if /I "%~1"=="-h"        goto :usage
if /I "%~1"=="--help"    goto :usage
echo Unknown argument: %~1
goto :usage

:args_done

if "%DO_CLEAN%"=="1" (
    if exist "build" (
        echo Cleaning build directory...
        rd /s /q "build"
        echo Build directory cleaned.
    ) else (
        echo Build directory does not exist, nothing to clean.
    )
)

mkdir build 2>nul
cd build

:: ----------------------------------------------------------------------
:: Local vcpkg toolchain (libcurl, etc.). Use delayed expansion (!VAR!)
:: because earlier blocks in this script enable it; mixing styles inside
:: the same setlocal scope occasionally swallows %VAR% references.
:: ----------------------------------------------------------------------
set "VCPKG_TC=!MOCIDA_ROOT!vcpkg\scripts\buildsystems\vcpkg.cmake"
set "TOOLCHAIN_ARG="
if exist "!VCPKG_TC!" (
    set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=!VCPKG_TC!"
    echo Using vcpkg toolchain: !VCPKG_TC!
) else (
    echo WARNING: vcpkg toolchain not found at "!VCPKG_TC!" - find_package CURL will fail.
)

:: ----------------------------------------------------------------------
:: Pick a generator: Ninja when available (faster, no dependency on
:: 'make'), otherwise fall back to Unix Makefiles.
:: ----------------------------------------------------------------------
set "GENERATOR=Unix Makefiles"
where ninja >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Ninja"
    echo Using generator: Ninja
) else (
    where make >nul 2>&1
    if errorlevel 1 (
        echo ERROR: neither 'ninja' nor 'make' were found in PATH.
        echo Run setup.bat to install them, or open a new terminal so it inherits the updated PATH.
        cd ..
        exit /b 1
    )
    echo Using generator: Unix Makefiles
)

:: ----------------------------------------------------------------------
:: WebView2 static loader bootstrap.
::
:: The vcpkg port only ships WebView2Loader.dll. To bundle WebView2
:: into the static mocida exe (no DLL alongside) we need
:: WebView2LoaderStatic.lib, which Microsoft distributes as part of
:: the Microsoft.Web.WebView2 NuGet package. We fetch it once into a
:: project-local .webview2/ cache and reuse it forever after — no
:: nuget.exe / vcpkg-static-triplet required.
::
:: Skip the download when the static lib is already extracted. The
:: cache lives outside build/, so `--clean` doesn't trigger a re-fetch.
:: ----------------------------------------------------------------------
set "WV2_VERSION=1.0.2792.45"
set "WV2_DIR=!MOCIDA_ROOT!.webview2"
set "WV2_STATIC_LIB=!WV2_DIR!\build\native\x64\WebView2LoaderStatic.lib"
if not exist "!WV2_STATIC_LIB!" (
    echo Fetching Microsoft.Web.WebView2 v!WV2_VERSION! ^(one-time^)...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ErrorActionPreference='Stop';" ^
        "$url='https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/!WV2_VERSION!';" ^
        "$zip = Join-Path $env:TEMP 'mocida-webview2.zip';" ^
        "New-Item -ItemType Directory -Force '!WV2_DIR!' | Out-Null;" ^
        "Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing;" ^
        "Expand-Archive -Path $zip -DestinationPath '!WV2_DIR!' -Force;" ^
        "Remove-Item $zip -ErrorAction SilentlyContinue"
    if not exist "!WV2_STATIC_LIB!" (
        echo WARNING: WebView2 NuGet fetch failed - static builds will continue to ship WebView2Loader.dll.
        set "WV2_DIR="
    )
)

:: ----------------------------------------------------------------------
:: Resolve clang to an absolute path. CMake's try_compile spawns nested
:: configures that re-run enable_language(C); on this machine the bare
:: -DCMAKE_C_COMPILER=clang sporadically loses its way (the sub-build
:: sees CMAKE_C_COMPILER unset, then SDL's check_c_source_compiles
:: aborts with "CMAKE_C_COMPILER not set, after EnableLanguage"). Pass
:: the absolute path explicitly AND export CC/CXX so the child cmakes
:: find the same compiler unambiguously.
:: ----------------------------------------------------------------------
set "CLANG_EXE="
for /f "delims=" %%i in ('where clang.exe 2^>nul') do (
    if not defined CLANG_EXE set "CLANG_EXE=%%i"
)
set "CLANGXX_EXE="
for /f "delims=" %%i in ('where clang++.exe 2^>nul') do (
    if not defined CLANGXX_EXE set "CLANGXX_EXE=%%i"
)
if not defined CLANG_EXE (
    cd ..
    echo ERROR: clang.exe not found. Install LLVM or run setup.bat.
    exit /b 1
)
set "CC=!CLANG_EXE!"
set "CXX=!CLANGXX_EXE!"

:: ----------------------------------------------------------------------
:: Configure
:: ----------------------------------------------------------------------
echo Configuring CMake with Clang...
echo   MOCIDA_BUILD_SHARED = !MOCIDA_SHARED!
echo   MOCIDA_BUILD_TESTS  = !MOCIDA_TESTS!
echo   MOCIDA_BUILD_DEMO   = !MOCIDA_DEMO!
set "WV2_ARG="
if defined WV2_DIR (
    if exist "!WV2_STATIC_LIB!" (
        set "WV2_ARG=-DMOCIDA_WEBVIEW2_NUGET_DIR=!WV2_DIR!"
    )
)
cmake -G "!GENERATOR!" ^
    !TOOLCHAIN_ARG! ^
    -DCMAKE_BUILD_TYPE=Debug ^
    "-DCMAKE_C_COMPILER=!CLANG_EXE!" ^
    "-DCMAKE_CXX_COMPILER=!CLANGXX_EXE!" ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -DMOCIDA_BUILD_SHARED=!MOCIDA_SHARED! ^
    -DMOCIDA_BUILD_TESTS=!MOCIDA_TESTS! ^
    -DMOCIDA_BUILD_DEMO=!MOCIDA_DEMO! ^
    !WV2_ARG! ^
    -Wno-dev --log-level=NOTICE ^
    ..

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    cd ..
    exit /b %ERRORLEVEL%
)

:: ----------------------------------------------------------------------
:: Build (parallel - uses every available core)
:: ----------------------------------------------------------------------
echo Building project in Debug mode...
cmake --build . --parallel

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    cd ..
    exit /b %ERRORLEVEL%
)

echo Build completed successfully.
cd ..
exit /b 0

:usage
echo Usage:
echo   build.bat                         static lib + demo (default)
echo   build.bat --shared                build mocida as a DLL
echo   build.bat --tests                 also compile tests/test_*.c
echo   build.bat --no-demo               skip the demo exe
echo   build.bat --clean                 wipe build/ before configuring
echo.
echo Flags compose: e.g. `build.bat --clean --shared --tests` does a
echo from-scratch build of mocida.dll plus every test program.
exit /b 0
