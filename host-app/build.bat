@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

echo [1/2] Compiling Win32 Icon Resource (ghost.rc)...
rc /nologo /fo src\ghost.res src\ghost.rc || exit /b 1

echo [2/2] Building portable GhostShareJor.exe...
if not exist ..\dist\GhostShareJor mkdir ..\dist\GhostShareJor
cl /nologo /O2 /EHsc /utf-8 /std:c++17 src\main.cpp src\ghost.res /Fe:..\dist\GhostShareJor\GhostShareJor.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib d2d1.lib dwrite.lib || exit /b 1

echo Build complete: ..\dist\GhostShareJor\GhostShareJor.exe
