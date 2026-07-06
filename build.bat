@echo off
setlocal enabledelayedexpansion

echo [1/3] Cleaning build...
rmdir /s /q build 2>nul
if errorlevel 1 (
    echo WARNING: Could not remove build folder (maybe it doesn't exist?)
)

echo [2/3] Configuring...
cmake -B build -A Win32 ^
    -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=x86-windows-static
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

echo [3/3] Building...
cmake --build build --config Release
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo All steps completed successfully.
exit /b 0