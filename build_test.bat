@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
cd /d C:\Users\Clayton\RiderProjects\Monolith
cmake --build build\msvc-debug --target VoxelTests 2>&1
