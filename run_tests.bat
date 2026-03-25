@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
set PATH=C:\Program Files\JetBrains\CLion 2025.3.4\bin\ninja\win\x64;%PATH%
cd /d C:\Users\Clayton\RiderProjects\Monolith
ctest --preset msvc-debug --output-on-failure