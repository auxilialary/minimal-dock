@echo off
call "%~dp0vs-env.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /W4 /WX /EHsc /D NOMINMAX /D WIN32_LEAN_AND_MEAN diagnostics\media-tests.cpp src\media\MediaService.cpp /Fe:diagnostics\media-tests.exe /Fo:diagnostics\ /link user32.lib /INCREMENTAL:NO
if errorlevel 1 exit /b 1
diagnostics\media-tests.exe
