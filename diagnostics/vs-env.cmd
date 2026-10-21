@echo off
if defined VSCMD_VER exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DOCK_VS=%%i"
if not defined DOCK_VS exit /b 1
call "%DOCK_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
