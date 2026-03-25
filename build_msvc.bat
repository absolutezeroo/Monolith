@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
set PATH=C:\Program Files\JetBrains\CLion 2025.3.4\bin\ninja\win\x64;%PATH%
cd /d C:\Users\Clayton\RiderProjects\Monolith
if exist build\msvc-debug\CMakeCache.txt del build\msvc-debug\CMakeCache.txt
if exist build\msvc-debug\CMakeFiles rmdir /s /q build\msvc-debug\CMakeFiles
cmake --preset msvc-debug 2>&1
if errorlevel 1 (
    echo CMAKE CONFIGURE FAILED
    exit /b 1
)
cmake --build --preset msvc-debug 2>&1
if errorlevel 1 (
    echo CMAKE BUILD FAILED
    exit /b 1
)
echo BUILD SUCCEEDED