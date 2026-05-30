@echo off
setlocal enabledelayedexpansion
rem Drag one or more image files (PNG/JPG/BMP/...) onto this file to
rem create a sibling .OZJ next to each one.

if "%~1"=="" (
    echo.
    echo   Drag a PNG ^(or several^) onto this Png-to-OZJ.bat to convert to OZJ.
    echo.
    pause
    exit /b 1
)

rem Prefer 'python', fall back to the 'py' launcher.
set "PY=python"
where python >nul 2>nul || set "PY=py"

set "SCRIPT=%~dp0png_to_ozj.py"

:loop
"%PY%" "%SCRIPT%" "%~1"
shift
if not "%~1"=="" goto loop

echo.
pause
endlocal
