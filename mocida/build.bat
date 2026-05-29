@echo off
setlocal enabledelayedexpansion

:: ----------------------------------------------------------------------
:: Mocida build script — Windows side.
::
:: Build dirs: build\<platform>\<config>
::   build\win32\debug   build\win32\release   build\win32\relwithdebinfo
::
:: Per-config isolation: switching Debug ⇄ Release does NOT trigger a
:: full SDL / SDL_image / SDL_ttf rebuild — each config has its own
:: dependency tree and Ninja's incremental engine handles everything.
::
:: Skip-configure: if build\<platform>\<config>\CMakeCache.txt already
:: exists, the configure pass is skipped — `cmake --build` re-runs the
:: configure on its own only if CMakeLists.txt is newer than the cache.
::
:: Force-rebuild: `--force` wipes build\<platform>\<config>\ first.
:: ----------------------------------------------------------------------

:: Capture the absolute path of the script directory BEFORE any `cd`.
:: %~dp0 always ends with a backslash; strip it so quoted uses like
:: cmake "%MOCIDA_ROOT%" don't end up with `\"` (which cmd treats as
:: an escaped quote, leaking the trailing `"` into the path).
set "MOCIDA_ROOT=%~dp0"
if "%MOCIDA_ROOT:~-1%"=="\" set "MOCIDA_ROOT=%MOCIDA_ROOT:~0,-1%"

echo Mocida Build Script

:: ----------------------------------------------------------------------
:: PATH: make sure tools installed via winget / LLVM / CMake / GnuWin32
:: are reachable in this session.
:: ----------------------------------------------------------------------
set "PATH=%LocalAppData%\Microsoft\WinGet\Links;C:\Program Files\LLVM\bin;%LocalAppData%\Programs\LLVM\bin;C:\Program Files\CMake\bin;C:\Program Files (x86)\GnuWin32\bin;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"

:: ----------------------------------------------------------------------
:: Parse args (order-agnostic). See :usage for the full list.
:: ----------------------------------------------------------------------
set "DO_FORCE=0"
set "DO_RECONFIGURE=0"
set "DO_VERBOSE=0"
set "MOCIDA_SHARED=OFF"
set "MOCIDA_TESTS=OFF"
set "MOCIDA_DEMO=ON"
set "MOCIDA_ASAN="
set "BUILD_TYPE=Debug"

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--clean"          (set "DO_FORCE=1"                    & shift & goto :parse_args)
if /I "%~1"=="--force"          (set "DO_FORCE=1"                    & shift & goto :parse_args)
if /I "%~1"=="--reconfigure"    (set "DO_RECONFIGURE=1"              & shift & goto :parse_args)
if /I "%~1"=="--verbose"        (set "DO_VERBOSE=1"                  & shift & goto :parse_args)
if /I "%~1"=="--shared"         (set "MOCIDA_SHARED=ON"              & shift & goto :parse_args)
if /I "%~1"=="--static"         (set "MOCIDA_SHARED=OFF"             & shift & goto :parse_args)
if /I "%~1"=="--tests"          (set "MOCIDA_TESTS=ON"               & shift & goto :parse_args)
if /I "%~1"=="--no-tests"       (set "MOCIDA_TESTS=OFF"              & shift & goto :parse_args)
if /I "%~1"=="--no-demo"        (set "MOCIDA_DEMO=OFF"               & shift & goto :parse_args)
if /I "%~1"=="--asan"           (set "MOCIDA_ASAN=address,undefined" & shift & goto :parse_args)
if /I "%~1"=="--release"        (set "BUILD_TYPE=Release"            & shift & goto :parse_args)
if /I "%~1"=="--debug"          (set "BUILD_TYPE=Debug"              & shift & goto :parse_args)
if /I "%~1"=="--relwithdebinfo" (set "BUILD_TYPE=RelWithDebInfo"     & shift & goto :parse_args)
if /I "%~1"=="-h"               goto :usage
if /I "%~1"=="--help"           goto :usage
echo Unknown argument: %~1
goto :usage

:args_done

:: Per-config dir: build\win32\<config-lowercase>.
set "BUILD_TYPE_LC=%BUILD_TYPE%"
if /I "%BUILD_TYPE%"=="Debug"          set "BUILD_TYPE_LC=debug"
if /I "%BUILD_TYPE%"=="Release"        set "BUILD_TYPE_LC=release"
if /I "%BUILD_TYPE%"=="RelWithDebInfo" set "BUILD_TYPE_LC=relwithdebinfo"
set "BUILD_DIR=build\win32\%BUILD_TYPE_LC%"

if "%DO_FORCE%"=="1" (
    if exist "%BUILD_DIR%" (
        echo Force-rebuild: wiping %BUILD_DIR% ...
        rd /s /q "%BUILD_DIR%"
    )
)
if "%DO_RECONFIGURE%"=="1" (
    if exist "%BUILD_DIR%\CMakeCache.txt" (
        echo Reconfigure: removing %BUILD_DIR%\CMakeCache.txt + CMakeFiles\ ...
        del /Q "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
        if exist "%BUILD_DIR%\CMakeFiles" rd /s /q "%BUILD_DIR%\CMakeFiles"
    )
)
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: Decide if we need a configure pass. Skipping when CMakeCache.txt
:: already exists saves the ~10-30 s SDL / SDL_image / SDL_ttf
:: detection cost on every run.
set "NEEDS_CONFIGURE=1"
if exist "%BUILD_DIR%\CMakeCache.txt" set "NEEDS_CONFIGURE=0"

:: ----------------------------------------------------------------------
:: MSVC environment — only needed on the configure pass (compiler ID,
:: feature detection). After the cache is populated, `cmake --build`
:: re-uses what was found and doesn't need vcvarsall anymore.
:: ----------------------------------------------------------------------
if "%NEEDS_CONFIGURE%"=="1" (
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
)

:: ----------------------------------------------------------------------
:: Generator (Ninja preferred). Both configure and incremental build
:: rely on this — Ninja's stat-based dependency check is what makes
:: "skip rebuilding SDL when only mocida changed" work automatically.
:: ----------------------------------------------------------------------
set "GENERATOR=Unix Makefiles"
where ninja >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Ninja"
) else (
    where make >nul 2>&1
    if errorlevel 1 (
        echo ERROR: neither 'ninja' nor 'make' were found in PATH.
        echo Run setup.bat to install them, or open a new terminal so it inherits the updated PATH.
        exit /b 1
    )
)

:: ----------------------------------------------------------------------
:: Clang lookup — only needed for the configure pass (CMake bakes the
:: absolute path into the cache).
:: ----------------------------------------------------------------------
if "%NEEDS_CONFIGURE%"=="1" (
    set "CLANG_EXE="
    for /f "delims=" %%i in ('where clang.exe 2^>nul') do (
        if not defined CLANG_EXE set "CLANG_EXE=%%i"
    )
    set "CLANGXX_EXE="
    for /f "delims=" %%i in ('where clang++.exe 2^>nul') do (
        if not defined CLANGXX_EXE set "CLANGXX_EXE=%%i"
    )
    if not defined CLANG_EXE (
        echo ERROR: clang.exe not found. Install LLVM or run setup.bat.
        exit /b 1
    )
    set "CC=!CLANG_EXE!"
    set "CXX=!CLANGXX_EXE!"
)

:: ----------------------------------------------------------------------
:: vcpkg toolchain + WebView2 NuGet — both only consulted at configure
:: time. After the first build the cache remembers the paths.
:: ----------------------------------------------------------------------
if "%NEEDS_CONFIGURE%"=="1" (
    set "VCPKG_TC=!MOCIDA_ROOT!\vcpkg\scripts\buildsystems\vcpkg.cmake"
    set "TOOLCHAIN_ARG="
    if exist "!VCPKG_TC!" (
        set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=!VCPKG_TC!"
    ) else (
        echo WARNING: vcpkg toolchain not found at "!VCPKG_TC!" - find_package CURL will fail.
    )

    set "WV2_VERSION=1.0.2792.45"
    set "WV2_DIR=!MOCIDA_ROOT!\.webview2"
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

    set "WV2_ARG="
    if defined WV2_DIR (
        if exist "!WV2_STATIC_LIB!" (
            set "WV2_ARG=-DMOCIDA_WEBVIEW2_NUGET_DIR=!WV2_DIR!"
        )
    )

    :: Optional sccache as a compiler launcher.
    set "SCCACHE_ARG="
    where sccache >nul 2>&1
    if not errorlevel 1 (
        set "SCCACHE_ARG=-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache"
    )
)

echo Platform           : win32
echo Build dir          : %BUILD_DIR%
echo Generator          : %GENERATOR%
echo CMAKE_BUILD_TYPE   : %BUILD_TYPE%
echo MOCIDA_BUILD_SHARED: %MOCIDA_SHARED%
echo MOCIDA_BUILD_TESTS : %MOCIDA_TESTS%
echo MOCIDA_BUILD_DEMO  : %MOCIDA_DEMO%
if defined MOCIDA_ASAN (
    echo MOCIDA_SANITIZE    : %MOCIDA_ASAN%
) else (
    echo MOCIDA_SANITIZE    : ^(off^)
)
if "%NEEDS_CONFIGURE%"=="0" (
    echo Configure          : SKIPPED ^(CMakeCache.txt exists - pass --reconfigure to force^)
) else (
    echo Configure          : YES ^(first build for this config^)
)

if "%NEEDS_CONFIGURE%"=="1" (
    echo Configuring CMake with Clang...
    cmake -G "%GENERATOR%" ^
        -S "%MOCIDA_ROOT%" ^
        -B "%BUILD_DIR%" ^
        !TOOLCHAIN_ARG! ^
        !SCCACHE_ARG! ^
        !WV2_ARG! ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        "-DCMAKE_C_COMPILER=!CLANG_EXE!" ^
        "-DCMAKE_CXX_COMPILER=!CLANGXX_EXE!" ^
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
        -DMOCIDA_BUILD_SHARED=%MOCIDA_SHARED% ^
        -DMOCIDA_BUILD_TESTS=%MOCIDA_TESTS% ^
        -DMOCIDA_BUILD_DEMO=%MOCIDA_DEMO% ^
        -DMOCIDA_SANITIZE=%MOCIDA_ASAN% ^
        -Wno-dev --log-level=NOTICE

    if %ERRORLEVEL% neq 0 (
        echo CMake configuration failed!
        exit /b %ERRORLEVEL%
    )
)

:: ----------------------------------------------------------------------
:: Build with explicit target list. The default is mocida + demo (+
:: tests when --tests is on). Explicit targets prevent any stray SDL
:: utility from ending up in the dependency graph. When SDL was built
:: once for this config, none of these targets force a rebuild on a
:: no-source-change run — Ninja stats first and rebuilds only stale
:: objects.
:: ----------------------------------------------------------------------
set "TARGETS=mocida"
if "%MOCIDA_DEMO%"=="ON" set "TARGETS=%TARGETS% demo"
if "%MOCIDA_TESTS%"=="ON" (
    for %%T in ("%MOCIDA_ROOT%\tests\test_*.c") do (
        set "TNAME=%%~nT"
        set "TARGETS=!TARGETS! !TNAME!"
    )
)

if not defined MOCIDA_BUILD_JOBS (
    set /a "MOCIDA_BUILD_JOBS=%NUMBER_OF_PROCESSORS%-4"
    if !MOCIDA_BUILD_JOBS! lss 2 set "MOCIDA_BUILD_JOBS=2"
)

set "VERBOSE_ARG="
if "%DO_VERBOSE%"=="1" set "VERBOSE_ARG=--verbose"

echo.
echo Building targets: %TARGETS%
echo Parallel jobs:    %MOCIDA_BUILD_JOBS%
cmake --build "%BUILD_DIR%" --parallel %MOCIDA_BUILD_JOBS% --config %BUILD_TYPE% --target %TARGETS% %VERBOSE_ARG%

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed.
echo Artefacts: %BUILD_DIR%\
if defined MOCIDA_ASAN (
    echo.
    echo ASan/LSan instrumentation is on. Run a binary directly to see
    echo the leak report at exit:
    echo     %BUILD_DIR%\demo.exe
    echo Optional env knobs ^(set before running^):
    echo     set LSAN_OPTIONS=verbosity=1:log_threads=1
    echo     set ASAN_OPTIONS=detect_leaks=1:print_stats=1:halt_on_error=0
)
exit /b 0

:usage
echo Usage:
echo   build.bat                         static lib + demo, Debug ^(default^)
echo   build.bat --release               optimised build ^(CMAKE_BUILD_TYPE=Release^)
echo   build.bat --debug                 debug build ^(explicit; default^)
echo   build.bat --relwithdebinfo        optimised + debug symbols
echo   build.bat --shared                build mocida as a DLL
echo   build.bat --tests                 also compile tests/test_*.c
echo   build.bat --no-demo               skip the demo exe
echo   build.bat --asan                  instrument with ASan + LSan + UBSan
echo   build.bat --force                 wipe build\win32\^<config^>\ before configuring
echo   build.bat --reconfigure           wipe only CMakeCache.txt ^(keep objects^)
echo   build.bat --clean                 alias for --force
echo   build.bat --verbose               per-command output
echo.
echo Build dirs: build\win32\^<config-lowercase^>
echo   debug, release, relwithdebinfo are fully isolated from each
echo   other - switching configs does NOT rebuild SDL. SDL only
echo   rebuilds inside a given config dir if its sources changed
echo   or --force was passed.
exit /b 0
