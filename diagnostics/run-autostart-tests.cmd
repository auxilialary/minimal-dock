@echo off
call "%~dp0vs-env.cmd"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /W4 /WX /EHsc /D NOMINMAX /D WIN32_LEAN_AND_MEAN diagnostics\autostart-tests.cpp /Fe:diagnostics\autostart-tests.exe /Fo:diagnostics\ /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
diagnostics\autostart-tests.exe
