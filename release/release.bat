@echo off
setlocal enabledelayedexpansion

:: ----------------------------------------------------------------------
:: Mocida SDK + installer release packager.
::
:: Builds two release-mode artefacts in a single CMake configure:
::
::   - mocida-sdk-windows-x64.zip   (the SDK the installer downloads)
::         include/uikit/*.h
::         lib/mocida.dll, mocida.lib
::         lib/SDL3.dll, SDL3_image.dll, SDL3_ttf.dll, WebView2Loader.dll
::
::   - mocida-installer-windows-x64.exe   (single-file GUI installer)
::         statically linked, no DLLs alongside.
::
:: The installer downloads the SDK zip from
::     https://github.com/liy77/mocida/releases/latest/download/<asset>
:: at install time, so the asset name must stay in sync with the
:: MOCIDA_SDK_ASSET_NAME macro in installer/installer.c.
::
:: Usage:
::     release\release.bat                    package only
::     release\release.bat --upload [tag]     also upload via `gh release`
::                                            (tag defaults to v<timestamp>)
:: ----------------------------------------------------------------------

set "SDK_ASSET=mocida-sdk-windows-x64.zip"
set "INSTALLER_ASSET=mocida-installer-windows-x64.exe"
set "MOCIDA_ROOT=%~dp0.."
set "STAGE_DIR=%~dp0stage"
set "DIST_DIR=%~dp0dist"

set "DO_UPLOAD=0"
set "REL_TAG="

:parse
if "%~1"=="" goto :args_done
if /I "%~1"=="--upload" ( set "DO_UPLOAD=1" & shift & goto :parse )
if /I "%~1"=="-h"        goto :usage
if /I "%~1"=="--help"    goto :usage
if not defined REL_TAG (
    set "REL_TAG=%~1"
    shift
    goto :parse
)
echo Unknown arg: %~1
goto :usage
:args_done

echo === Mocida release ===
echo Project root: %MOCIDA_ROOT%
echo SDK asset:    %SDK_ASSET%
echo Installer:    %INSTALLER_ASSET%

:: ----------------------------------------------------------------------
:: PATH + MSVC environment.
:: ----------------------------------------------------------------------
set "PATH=%LocalAppData%\Microsoft\WinGet\Links;C:\Program Files\LLVM\bin;%LocalAppData%\Programs\LLVM\bin;C:\Program Files\CMake\bin;C:\Program Files (x86)\GnuWin32\bin;%PATH%"
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

set "CLANG_EXE="
for /f "delims=" %%i in ('where clang.exe 2^>nul') do (
    if not defined CLANG_EXE set "CLANG_EXE=%%i"
)
set "CLANGXX_EXE="
for /f "delims=" %%i in ('where clang++.exe 2^>nul') do (
    if not defined CLANGXX_EXE set "CLANGXX_EXE=%%i"
)
if not defined CLANG_EXE (
    echo Could not find clang.exe on PATH. Install LLVM or run setup.bat.
    exit /b 1
)
set "CC=!CLANG_EXE!"
set "CXX=!CLANGXX_EXE!"

set "VCPKG_TC=%MOCIDA_ROOT%\vcpkg\scripts\buildsystems\vcpkg.cmake"
set "TOOLCHAIN_ARG="
if exist "%VCPKG_TC%" set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_TC%"

set "WV2_STATIC_LIB=%MOCIDA_ROOT%\.webview2\build\native\x64\WebView2LoaderStatic.lib"
set "WV2_ARG="
if exist "%WV2_STATIC_LIB%" (
    set "WV2_ARG=-DMOCIDA_WEBVIEW2_NUGET_DIR=%MOCIDA_ROOT%\.webview2"
)

set "GENERATOR=Unix Makefiles"
where ninja >nul 2>&1
if not errorlevel 1 set "GENERATOR=Ninja"

:: ----------------------------------------------------------------------
:: Two separate build dirs are necessary because:
::   - The SDK needs SDL3 / SDL3_image / SDL3_ttf as SHARED libs
::     (the user's apps link the DLLs they downloaded).
::   - The installer must be STATIC (single .exe, no DLLs alongside),
::     which requires SDL to be built static too.
:: SDL3's CMake doesn't expose SDL_SHARED + SDL_STATIC dual output
:: cleanly, so we just run the configure twice.
:: ----------------------------------------------------------------------

set "BUILD_SDK=%MOCIDA_ROOT%\build-release-sdk"
set "BUILD_INST=%MOCIDA_ROOT%\build-release-installer"

mkdir "%BUILD_SDK%"  2>nul
mkdir "%BUILD_INST%" 2>nul

:: ----- Pass 1: shared SDK (mocida.dll + SDL DLLs) ---------------------
pushd "%BUILD_SDK%" || ( echo Could not enter %BUILD_SDK%. & exit /b 1 )
echo.
echo === Pass 1/2: shared SDK ===
cmake -G "%GENERATOR%" ^
    %TOOLCHAIN_ARG% ^
    %WV2_ARG% ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_C_COMPILER=!CLANG_EXE!" ^
    "-DCMAKE_CXX_COMPILER=!CLANGXX_EXE!" ^
    -DMOCIDA_BUILD_SHARED=ON ^
    -DMOCIDA_BUILD_INSTALLER=OFF ^
    -DMOCIDA_BUILD_DEMO=OFF ^
    -DMOCIDA_BUILD_TESTS=OFF ^
    -Wno-dev --log-level=NOTICE ^
    "%MOCIDA_ROOT%"
if errorlevel 1 ( popd & echo SDK configure failed. & exit /b 1 )

cmake --build . --parallel --config Release --target mocida
if errorlevel 1 ( popd & echo SDK build failed. & exit /b 1 )
popd

:: ----- Pass 2: static installer (single .exe, no DLLs alongside) -----
pushd "%BUILD_INST%" || ( echo Could not enter %BUILD_INST%. & exit /b 1 )
echo.
echo === Pass 2/2: static installer ===
cmake -G "%GENERATOR%" ^
    %TOOLCHAIN_ARG% ^
    %WV2_ARG% ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_C_COMPILER=!CLANG_EXE!" ^
    "-DCMAKE_CXX_COMPILER=!CLANGXX_EXE!" ^
    -DMOCIDA_BUILD_SHARED=OFF ^
    -DMOCIDA_BUILD_INSTALLER=ON ^
    -DMOCIDA_BUILD_DEMO=OFF ^
    -DMOCIDA_BUILD_TESTS=OFF ^
    -Wno-dev --log-level=NOTICE ^
    "%MOCIDA_ROOT%"
if errorlevel 1 ( popd & echo Installer configure failed. & exit /b 1 )

cmake --build . --parallel --config Release --target mocida_installer
if errorlevel 1 ( popd & echo Installer build failed. & exit /b 1 )
popd

:: ----------------------------------------------------------------------
:: Stage the SDK payload — identical layout to the previous release.
:: ----------------------------------------------------------------------
echo Staging SDK payload ...
if exist "%STAGE_DIR%" rd /s /q "%STAGE_DIR%"
mkdir "%STAGE_DIR%\include" 2>nul
mkdir "%STAGE_DIR%\lib"     2>nul

xcopy /E /I /Y /Q "%MOCIDA_ROOT%\src\headers\uikit" "%STAGE_DIR%\include\uikit" >nul
if errorlevel 1 ( echo Could not stage headers. & exit /b 1 )

set "BUILT=%BUILD_SDK%"

set "MISSING="
for %%P in (
    "mocida.dll        : %BUILT%"
    "mocida.lib        : %BUILT%"
    "SDL3.dll          : %BUILT%\SDL"
    "SDL3_image.dll    : %BUILT%\SDL_image"
    "SDL3_ttf.dll      : %BUILT%\SDL_ttf"
    "WebView2Loader.dll: %MOCIDA_ROOT%\vcpkg\installed\x64-windows\bin"
) do (
    for /f "tokens=1,* delims=:" %%A in (%%P) do (
        set "_NAME=%%A"
        set "_DIR=%%B"
        for /f "tokens=* delims= " %%N in ("!_NAME!") do set "_NAME=%%N"
        for /f "tokens=* delims= " %%D in ("!_DIR!")  do set "_DIR=%%D"
        if exist "!_DIR!\!_NAME!" (
            copy /Y "!_DIR!\!_NAME!" "%STAGE_DIR%\lib\" >nul
        ) else (
            echo Missing build artifact: !_DIR!\!_NAME!
            set "MISSING=1"
        )
    )
)
if defined MISSING ( echo SDK staging failed. & exit /b 1 )

if exist "%BUILT%\mocida.pdb" copy /Y "%BUILT%\mocida.pdb" "%STAGE_DIR%\lib\" >nul

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
set "SDK_ZIP=%DIST_DIR%\%SDK_ASSET%"
if exist "%SDK_ZIP%" del /Q "%SDK_ZIP%"

echo Zipping %SDK_ASSET% ...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Compress-Archive -Path '%STAGE_DIR%\include','%STAGE_DIR%\lib' -DestinationPath '%SDK_ZIP%' -Force"
if errorlevel 1 ( echo Compress-Archive failed. & exit /b 1 )

for %%I in ("%SDK_ZIP%") do set "ZIP_BYTES=%%~zI"
set /a ZIP_MB=ZIP_BYTES/1048576
echo Created %SDK_ZIP% (~%ZIP_MB% MB).

:: ----------------------------------------------------------------------
:: Stage the installer.exe as its own asset (single-file, statically
:: linked — distributable as-is).
:: ----------------------------------------------------------------------
set "INSTALLER_SRC=%BUILD_INST%\mocida_installer.exe"
set "INSTALLER_DST=%DIST_DIR%\%INSTALLER_ASSET%"
if not exist "%INSTALLER_SRC%" (
    echo Installer build artefact missing: %INSTALLER_SRC%
    exit /b 1
)
copy /Y "%INSTALLER_SRC%" "%INSTALLER_DST%" >nul
for %%I in ("%INSTALLER_DST%") do set "INST_BYTES=%%~zI"
set /a INST_MB=INST_BYTES/1048576
echo Copied  %INSTALLER_DST% (~%INST_MB% MB).

:: ----------------------------------------------------------------------
:: Optional upload via gh — both assets on the same release.
:: ----------------------------------------------------------------------
if "%DO_UPLOAD%"=="1" (
    where gh >nul 2>&1
    if errorlevel 1 (
        echo gh CLI not found. Install https://cli.github.com/ or upload manually.
        exit /b 1
    )

    if not defined REL_TAG (
        for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "REL_TAG=v%%T"
    )
    set "REL_TITLE=Mocida !REL_TAG!"

    echo Creating GitHub release !REL_TAG! ...
    :: Notes are kept on a single line to avoid cmd's caret-continuation
    :: parser swallowing the leading dash of bullet items.
    set "REL_NOTES=Mocida release !REL_TAG!. %INSTALLER_ASSET% is the single-file GUI installer (no DLLs); %SDK_ASSET% is the SDK payload (headers + mocida.dll + mocida.lib + SDL DLLs) the installer downloads on first run."
    gh release view "!REL_TAG!" >nul 2>&1
    if errorlevel 1 (
        gh release create "!REL_TAG!" "%SDK_ZIP%" "%INSTALLER_DST%" ^
            --title "!REL_TITLE!" ^
            --notes "!REL_NOTES!"
    ) else (
        echo Release !REL_TAG! already exists - uploading assets only.
        gh release upload "!REL_TAG!" "%SDK_ZIP%" "%INSTALLER_DST%" --clobber
    )
    if errorlevel 1 ( echo gh release failed. & exit /b 1 )
    echo Release published.
)

exit /b 0

:usage
echo Usage:
echo   release\release.bat                    Package both assets into release\dist\.
echo   release\release.bat --upload [tag]     Also upload as a GitHub release.
echo.
echo Both artefacts are always built in Release mode:
echo   release\dist\%SDK_ASSET%
echo   release\dist\%INSTALLER_ASSET%
exit /b 0
