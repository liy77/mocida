@echo off
setlocal enabledelayedexpansion

echo UIKit Build Script - Debug Mode with Clang

:: Create or clean build directory
if "%1"=="--clean" (
    if exist "build" (
        echo Cleaning build directory...
        rd /s /q "build"
        echo Build directory cleaned.
    ) else (
        echo Build directory does not exist, nothing to clean.
    )
)

:: Create or clean build directory
mkdir build 2>nul
cd build

:: Configure CMake with Clang (ucrt64)
echo Configuring CMake with Clang...
cmake -G "Unix Makefiles" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_C_COMPILER=clang ^
    -DCMAKE_CXX_COMPILER=clang++ ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -Wno-dev --log-level=NOTICE ^
    ..

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b %ERRORLEVEL%
)

:: Build the project
echo Building project in Debug mode...
cmake --build .

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo Build completed successfully.
cd ..

