@echo off
setlocal
cd /d "%~dp0"
call diagnostics\vs-env.cmd
if errorlevel 1 exit /b 1
where nuget >nul 2>nul
if errorlevel 1 (
  echo Install NuGet CLI and restore packages.config to packages before building.
  exit /b 1
)
nuget restore packages.config -PackagesDirectory packages -NonInteractive
if errorlevel 1 exit /b 1
msbuild Dock.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:WarningLevel=Level4 /p:TreatWarningAsError=true /nologo
exit /b %errorlevel%
