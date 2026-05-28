@echo off
setlocal enabledelayedexpansion

:: Convenience wrapper: build mocida as a DLL + the GUI installer in one
:: cmake invocation. Result lands in build\win32\mocida_installer.exe, with
:: mocida.dll, the SDL DLLs, and src\headers\* staged next to it so the
:: installer can ship them as its payload.

set "MOCIDA_ROOT=%~dp0.."
set "BUILD_DIR=%MOCIDA_ROOT%\build\win32"

:: ----------------------------------------------------------------------
:: Parse args (order-agnostic):
::   --release         optimised build (CMAKE_BUILD_TYPE=Release)
::   --debug           debug build (default)
::   --relwithdebinfo  optimised + debug symbols
:: ----------------------------------------------------------------------
set "BUILD_TYPE=Debug"
:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--release"        (set "BUILD_TYPE=Release"        & shift & goto :parse_args)
if /I "%~1"=="--debug"          (set "BUILD_TYPE=Debug"          & shift & goto :parse_args)
if /I "%~1"=="--relwithdebinfo" (set "BUILD_TYPE=RelWithDebInfo" & shift & goto :parse_args)
if /I "%~1"=="-h"               goto :usage
if /I "%~1"=="--help"           goto :usage
echo Unknown argument: %~1
goto :usage
:args_done

echo Mocida Installer - Build Script
echo Project root: %MOCIDA_ROOT%
echo Build type:   %BUILD_TYPE%

:: ----------------------------------------------------------------------
:: PATH augmentation: same as build.bat — make sure LLVM (clang), CMake,
:: and the winget shim folder are reachable before the MSVC bootstrap.
:: ----------------------------------------------------------------------
set "PATH=%LocalAppData%\Microsoft\WinGet\Links;C:\Program Files\LLVM\bin;%LocalAppData%\Programs\LLVM\bin;C:\Program Files\CMake\bin;C:\Program Files (x86)\GnuWin32\bin;%PATH%"

:: ----------------------------------------------------------------------
:: MSVC environment (LIB / INCLUDE / RC). Skip when already initialised.
:: Snapshot %ProgramFiles(x86)% into a local without `(` to avoid the
:: cmd parser confusing the parentheses with the `if` block.
:: ----------------------------------------------------------------------
if not defined VCINSTALLDIR (
    set "PROGFILESX86=%ProgramFiles(x86)%"
    set "VSWHERE=!PROGFILESX86!\Microsoft Visual Studio\Installer\vswhere.exe"
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

if not exist "%MOCIDA_ROOT%\build" mkdir "%MOCIDA_ROOT%\build"
if not exist "%BUILD_DIR%"        mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%" || (
    echo Could not enter build directory %BUILD_DIR%.
    exit /b 1
)

set "VCPKG_TC=%MOCIDA_ROOT%\vcpkg\scripts\buildsystems\vcpkg.cmake"
set "TOOLCHAIN_ARG="
if exist "%VCPKG_TC%" set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_TC%"

:: Static WebView2 loader (NuGet package staged by the project's
:: build.bat into .webview2\). When this lib is on disk, CMake bakes
:: it into the installer so no WebView2Loader.dll is shipped alongside.
set "WV2_STATIC_LIB=%MOCIDA_ROOT%\.webview2\build\native\x64\WebView2LoaderStatic.lib"
set "WV2_ARG="
if exist "%WV2_STATIC_LIB%" (
    set "WV2_ARG=-DMOCIDA_WEBVIEW2_NUGET_DIR=%MOCIDA_ROOT%\.webview2"
) else (
    echo NOTE: %WV2_STATIC_LIB% not found - installer will need WebView2Loader.dll alongside.
    echo       Run the project's build.bat once to download the WebView2 NuGet.
)

set "GENERATOR=Unix Makefiles"
where ninja >nul 2>&1
if not errorlevel 1 set "GENERATOR=Ninja"

:: ----------------------------------------------------------------------
:: Resolve clang to an absolute path. CMake's try_compile spawns nested
:: configures that re-run enable_language(C); on this machine the bare
:: -DCMAKE_C_COMPILER=clang sporadically loses its way (the sub-build
:: sees CMAKE_C_COMPILER unset, then SDL's check_c_source_compiles
:: aborts). Passing the absolute path AND exporting CC/CXX makes the
:: child cmakes find the same compiler unambiguously.
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
    popd
    echo Could not find clang.exe on PATH. Install LLVM or run setup.bat.
    exit /b 1
)
echo Using C   compiler: !CLANG_EXE!
echo Using C++ compiler: !CLANGXX_EXE!
set "CC=!CLANG_EXE!"
set "CXX=!CLANGXX_EXE!"

echo Configuring with MOCIDA_BUILD_INSTALLER=ON (installer is statically linked) ...
cmake -G "%GENERATOR%" ^
    %TOOLCHAIN_ARG% ^
    %WV2_ARG% ^
    -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ^
    "-DCMAKE_C_COMPILER=!CLANG_EXE!" ^
    "-DCMAKE_CXX_COMPILER=!CLANGXX_EXE!" ^
    -DMOCIDA_BUILD_SHARED=OFF ^
    -DMOCIDA_BUILD_INSTALLER=ON ^
    -DMOCIDA_BUILD_DEMO=OFF ^
    -DMOCIDA_BUILD_TESTS=OFF ^
    -Wno-dev --log-level=NOTICE ^
    "%MOCIDA_ROOT%"
if errorlevel 1 (
    popd
    echo CMake configuration failed.
    exit /b %ERRORLEVEL%
)

echo Compiling mocida_installer (%BUILD_TYPE%) ...
cmake --build . --target mocida_installer --parallel --config !BUILD_TYPE!
if errorlevel 1 (
    popd
    echo Installer compilation failed.
    exit /b %ERRORLEVEL%
)

popd

echo.
echo Installer built: %BUILD_DIR%\mocida_installer.exe
echo Fully static: no DLLs are shipped alongside the .exe. The installer
echo talks to GitHub via WinINet and bundles the WebView2 static loader.
echo.
echo At install time, it downloads the Mocida SDK zip from
echo https://github.com/liy77/mocida/releases/latest. Make sure a
echo release exists (build one via release\release.bat).
exit /b 0

:usage
echo Usage:
echo   installer\build.bat                       Debug build (default)
echo   installer\build.bat --release             optimised build
echo   installer\build.bat --relwithdebinfo      optimised + debug symbols
exit /b 0
