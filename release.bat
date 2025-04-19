@echo off
setlocal enabledelayedexpansion

:: Set color codes
set "BLUE=1"
set "GREEN=2"
set "RED=4"
set "PURPLE=5"
set "YELLOW=6"
set "WHITE=7"

:: Display header
color 0%PURPLE%
echo.
echo ===================================
echo     MOCIDA RELEASE BUILD SCRIPT
echo ===================================
echo.

:: Check if build tools are available
color 0%YELLOW%
echo Checking environment...
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    color 0%RED%
    echo CMake not found in PATH! Please install CMake.
    goto :error
)
timeout /t 1 >nul

:: Set build directory
set "BUILD_DIR=build"
set "DIST_DIR=dist"

:: Create directories if they don't exist
if not exist %BUILD_DIR% (
    color 0%BLUE%
    echo Creating build directory...
    mkdir %BUILD_DIR%
)

if not exist %DIST_DIR% (
    color 0%BLUE%
    echo Creating distribution directory...
    mkdir %DIST_DIR%
)

:: Clean previous build
color 0%YELLOW%
echo Cleaning previous build files...
if exist %BUILD_DIR%\* del /Q %BUILD_DIR%\*
if exist %DIST_DIR%\* del /Q %DIST_DIR%\*

:: Start build process
color 0%BLUE%
echo.
echo Starting build process...
echo.

:: Build with CMake
color 0%WHITE%
echo Configuring CMake...
cd %BUILD_DIR% || goto :error
cmake .. -DCMAKE_BUILD_TYPE=Release || (
    color 0%RED%
    echo CMake configuration failed!
    cd ..
    goto :error
)

echo Building with CMake...
cmake --build . --config Release || (
    color 0%RED%
    echo CMake build failed!
    cd ..
    goto :error
)
cd ..

:: Copy files to distribution directory
color 0%BLUE%
echo.
echo Copying files to distribution directory...
xcopy /E /I /Y %BUILD_DIR%\* %DIST_DIR%\

:: Create release zip
color 0%YELLOW%
echo.
echo Creating release archive...
powershell -command "Compress-Archive -Path %DIST_DIR%\* -DestinationPath %DIST_DIR%\uikit-release.zip -Force"

:: Build completed successfully
color 0%GREEN%
echo.
echo ===================================
echo BUILD COMPLETED SUCCESSFULLY!
echo Release file: %CD%\%DIST_DIR%\uikit-release.zip
echo ===================================
echo.

color 07
goto :end

:error
color 0%RED%
echo.
echo ===================================
echo BUILD PROCESS FAILED!
echo Please check the error messages above.
echo ===================================
echo.
color 07
exit /b 1

:end
color 07
exit /b 0