@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if "%~1"=="" (
    set TARGET=all
) else (
    set TARGET=%~1
)
set PRESET=msvc-debug
set BUILD_DIR=build\%PRESET%
if not exist "%BUILD_DIR%\build.ninja" (
    echo === Configuring %PRESET% ===
    cmake --preset %PRESET% -DVCPKG_INSTALL_OPTIONS="--no-binarycaching;--x-buildtrees-root=%CD%/build/%PRESET%/vcpkg_installed/vcpkg/blds"
)
echo === Building target: %TARGET% ===
cmake --build %BUILD_DIR% --target %TARGET%
endlocal
