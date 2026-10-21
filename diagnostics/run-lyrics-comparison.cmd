@echo off
call "%~dp0vs-env.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /W4 /WX /EHsc /utf-8 /D NOMINMAX /D WIN32_LEAN_AND_MEAN /I src\media diagnostics\lyrics-comparison.cpp /Fe:diagnostics\lyrics-comparison.exe /Fo:diagnostics\lyrics-test-obj\ /link windowsapp.lib user32.lib /INCREMENTAL:NO
if errorlevel 1 exit /b 1
diagnostics\lyrics-comparison.exe
