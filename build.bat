@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VCVARS="

rem Preferred: ask the VS installer (any edition/version, incl. prerelease).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if defined VCVARS if not exist "%VCVARS%" set "VCVARS="

rem Fallback: explicit well-known install locations.
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
  echo Could not find vcvars64.bat
  exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

if not exist build mkdir build

cl /nologo /W3 /O2 /TC /std:c11 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Isrc /Fo:build\ /Fe:build\xde_test.exe src\xde.c src\xdetbl.c src\xde_text.c tests\xde_test.c
if errorlevel 1 exit /b 1

echo.
build\xde_test.exe
exit /b %ERRORLEVEL%
