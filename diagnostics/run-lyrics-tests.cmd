@echo off
call "%~dp0vs-env.cmd"
if errorlevel 1 exit /b 1
if not exist diagnostics\lyrics-test-obj mkdir diagnostics\lyrics-test-obj
cl /nologo /std:c++20 /W4 /WX /EHsc /utf-8 /D NOMINMAX /D WIN32_LEAN_AND_MEAN diagnostics\lyrics-tests.cpp src\media\LyricsService.cpp /Fe:diagnostics\lyrics-tests.exe /Fo:diagnostics\lyrics-test-obj\ /link windowsapp.lib user32.lib /INCREMENTAL:NO
if errorlevel 1 exit /b 1
diagnostics\lyrics-tests.exe
