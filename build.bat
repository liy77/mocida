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
set "PATH=%LocalAppData%\Microsoft\WinGet\Links;C:\Program Files\LLVM\bin;%LocalAppData%\Programs\LLVM\bin;C:\Program Files\CMake\bin;C:\Program Files (x86)\GnuWin32\bin;%PATH%"

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
:: Configure
:: ----------------------------------------------------------------------
echo Configuring CMake with Clang...
echo   MOCIDA_BUILD_SHARED = %MOCIDA_SHARED%
echo   MOCIDA_BUILD_TESTS  = %MOCIDA_TESTS%
echo   MOCIDA_BUILD_DEMO   = %MOCIDA_DEMO%
cmake -G "!GENERATOR!" ^
    !TOOLCHAIN_ARG! ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_C_COMPILER=clang ^
    -DCMAKE_CXX_COMPILER=clang++ ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -DMOCIDA_BUILD_SHARED=!MOCIDA_SHARED! ^
    -DMOCIDA_BUILD_TESTS=!MOCIDA_TESTS! ^
    -DMOCIDA_BUILD_DEMO=!MOCIDA_DEMO! ^
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
