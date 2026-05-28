@echo off
set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer
set PATH=%PATH%;C:\Users\Pablo\AppData\Local\Microsoft\WinGet\Links
C:\msys64\mingw32\bin\cmake.exe --build G:\Files\Mu\MuMain\build-mingw -j4
