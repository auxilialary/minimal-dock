@echo off
if defined VSCMD_VER goto validate
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -version 18 -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DOCK_VS=%%i"
if not defined DOCK_VS (
  echo Visual Studio 2026 with MSVC v145 is required.
  exit /b 1
)
call "%DOCK_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:validate
if not "%VSCMD_VER:~0,3%"=="18." (
  echo Visual Studio 2026 is required. Open a fresh command prompt.
  exit /b 1
)
if not "%VCToolsVersion:~0,4%"=="14.5" (
  echo MSVC v145 is required.
  exit /b 1
)
where cl >nul 2>nul
if errorlevel 1 exit /b 1
where msbuild >nul 2>nul
exit /b %errorlevel%
