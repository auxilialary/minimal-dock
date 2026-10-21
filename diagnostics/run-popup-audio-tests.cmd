@echo off
call "%~dp0vs-env.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /W4 /WX /EHsc /D NOMINMAX /D WIN32_LEAN_AND_MEAN diagnostics\popup-audio-tests.cpp src\audio\AudioMixer.cpp src\windows\WindowTracker.cpp src\windows\IconBitmap.cpp src\windows\IconIdentity.cpp /Fe:diagnostics\popup-audio-tests.exe /Fo:diagnostics\ /link ole32.lib user32.lib gdi32.lib /INCREMENTAL:NO
if errorlevel 1 exit /b 1
diagnostics\popup-audio-tests.exe
