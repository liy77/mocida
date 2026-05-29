@echo off
:: ======================================================================
::  Mocida - wrapper around setup.ps1
::
::  Forwards every argument to the PowerShell script, which does the
::  actual work (install via winget, vcpkg, clone the SDL deps, etc.).
::
::  Examples:
::    setup.bat
::    setup.bat -NoBuild
::    setup.bat -Force
::    setup.bat -SkipInstall
:: ======================================================================

setlocal
where powershell >nul 2>&1
if errorlevel 1 (
    echo ERROR: powershell.exe was not found in PATH.
    echo Setup requires Windows PowerShell to run.
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
exit /b %ERRORLEVEL%
