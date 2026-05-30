@echo off
setlocal enabledelayedexpansion
rem Drag one or more .glb files onto this to convert them back to .bmd.
rem
rem The original .bmd next to the .glb is used as a template for the skeleton,
rem animations and texture names (our GLBs are bind-pose only), and is backed up
rem once to <name>.bmd.orig before being overwritten.

if "%~1"=="" (
    echo.
    echo   Drag a .glb ^(or several^) onto this Glb-to-BMD.bat to convert back to BMD.
    echo.
    pause
    exit /b 1
)

set "PY=python"
where python >nul 2>nul || set "PY=py"
set "SCRIPT=%~dp0convert_gltf_to_bmd.py"

:loop
"%PY%" "%SCRIPT%" "%~1"
shift
if not "%~1"=="" goto loop

echo.
pause
endlocal
