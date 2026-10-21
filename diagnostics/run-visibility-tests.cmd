@echo off
call "%~dp0vs-env.cmd"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /W4 /WX /EHsc /D NOMINMAX /D WIN32_LEAN_AND_MEAN diagnostics\visibility-tests.cpp /Fe:diagnostics\visibility-tests.exe /Fo:diagnostics\ /link user32.lib /INCREMENTAL:NO
if errorlevel 1 exit /b 1
diagnostics\visibility-tests.exe
