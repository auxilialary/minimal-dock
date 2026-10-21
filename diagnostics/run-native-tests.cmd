@echo off
call "%~dp0vs-env.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /W4 /WX /EHsc /D NOMINMAX /D WIN32_LEAN_AND_MEAN diagnostics\native-tests.cpp src\windows\IconBitmap.cpp src\windows\IconIdentity.cpp /Fe:diagnostics\native-tests.exe /Fo:diagnostics\ /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
diagnostics\native-tests.exe
