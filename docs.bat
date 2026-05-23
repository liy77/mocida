@echo off
setlocal enabledelayedexpansion

:: ======================================================================
::  Mocida - documentation toolchain.
::
::  Usage:
::    docs.bat                       build docs (no browser)
::    docs.bat --open                build and open index.html locally
::    docs.bat --serve               build + start local HTTP server
::                                   (default http://localhost:8080)
::    docs.bat --serve --port 4242   pick a different port
::    docs.bat --serve --no-build    don't rebuild, just serve what's there
::    docs.bat --serve --no-open     start the server but don't open browser
::
::  --serve uses a PowerShell HttpListener (.NET — ships with Windows),
::  so no Python / Node / extra deps required. Press Ctrl+C in the
::  console to stop the server.
:: ======================================================================

set "DO_OPEN=0"
set "DO_SERVE=0"
set "DO_BUILD=1"
set "PORT=8080"

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--open"      (set "DO_OPEN=1"  & shift & goto :parse_args)
if /I "%~1"=="--serve"     (set "DO_SERVE=1" & set "DO_OPEN=1" & shift & goto :parse_args)
if /I "%~1"=="--no-build"  (set "DO_BUILD=0" & shift & goto :parse_args)
if /I "%~1"=="--no-open"   (set "DO_OPEN=0"  & shift & goto :parse_args)
if /I "%~1"=="--port" (
    set "PORT=%~2"
    shift & shift & goto :parse_args
)
if /I "%~1"=="-h"          goto :usage
if /I "%~1"=="--help"      goto :usage
echo Unknown argument: %~1
echo.
goto :usage

:args_done

:: ----------------------------------------------------------------------
:: Locate doxygen (PATH + common winget shim locations).
:: ----------------------------------------------------------------------
if "%DO_BUILD%"=="1" (
    set "PATH=%LocalAppData%\Microsoft\WinGet\Links;C:\Program Files\doxygen\bin;%PATH%"
    where doxygen >nul 2>&1
    if errorlevel 1 (
        echo [docs] doxygen not found in PATH.
        echo        winget install --id DimitriVanHeesch.Doxygen
        echo        or https://www.doxygen.nl/download.html
        exit /b 1
    )
)

if not exist "%~dp0Doxyfile" (
    echo [docs] Doxyfile not found at %~dp0Doxyfile
    exit /b 1
)

:: ----------------------------------------------------------------------
:: Build
:: ----------------------------------------------------------------------
if "%DO_BUILD%"=="1" (
    pushd "%~dp0"
    echo [docs] running doxygen...
    doxygen Doxyfile
    if errorlevel 1 (
        popd
        echo [docs] doxygen reported errors.
        exit /b 1
    )
    :: ------------------------------------------------------------------
    :: Mirror the project's assets/ folder into the generated HTML root.
    :: The README and mainpage.md use <img src="assets/..."> paths, which
    :: Doxygen does NOT auto-copy. xcopy keeps the relative layout intact
    :: so banner.svg, feature-*.svg, sdl_logo.png etc. resolve.
    :: ------------------------------------------------------------------
    if exist "%~dp0assets" (
        echo [docs] copying assets/ into the generated site...
        xcopy /E /I /Y /Q "%~dp0assets" "%~dp0docs\generated\html\assets" >nul
    )
    popd
)

set "INDEX=%~dp0docs\generated\html\index.html"
if not exist "%INDEX%" (
    echo [docs] %INDEX% not found — run without --no-build first.
    exit /b 1
)

if "%DO_BUILD%"=="1" (
    echo.
    echo === Docs ready ===
    echo %INDEX%
)

:: ----------------------------------------------------------------------
:: Serve / open
:: ----------------------------------------------------------------------
if "%DO_SERVE%"=="1" (
    set "OPEN_FLAG="
    if "%DO_OPEN%"=="1" set "OPEN_FLAG=-Open"
    echo.
    echo [docs] starting local server on http://localhost:%PORT%
    echo        Ctrl+C to stop
    powershell -NoProfile -ExecutionPolicy Bypass ^
        -File "%~dp0docs\serve.ps1" ^
        -Root "%~dp0docs\generated\html" ^
        -Port %PORT% !OPEN_FLAG!
    exit /b %ERRORLEVEL%
)

if "%DO_OPEN%"=="1" (
    start "" "%INDEX%"
)

exit /b 0

:usage
echo Usage:
echo   docs.bat                       build docs only
echo   docs.bat --open                build + open index.html
echo   docs.bat --serve [--port N]    build + start local HTTP server
echo   docs.bat --serve --no-build    just serve what is already built
echo   docs.bat --serve --no-open     start server but don't open browser
echo.
echo Defaults: port 8080, opens browser, rebuilds docs.
exit /b 0
