@echo off
setlocal enabledelayedexpansion

REM Set up MSYS2 MinGW environment
set MSYSTEM=MINGW32

REM Run cmake configure and build through MSYS2
C:\msys64\msys2_shell.cmd -defterm -no-start -here -c "cd /g/Files/Mu/MuMain && cmake -B build-mingw -G Ninja && cmake --build build-mingw --config Release --target Main"

echo.
echo Build script completed.
pause
