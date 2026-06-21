@echo off
REM F4Menu Build Script (MSBuild) - Wrapper
REM Usage: build.bat [x64|Win32] [clean]

set PLATFORM=x64
set CLEAN=

if /I "%~1"=="Win32" set PLATFORM=Win32
if /I "%~2"=="Win32" set PLATFORM=Win32
if /I "%~1"=="clean" set CLEAN=-Clean
if /I "%~2"=="clean" set CLEAN=-Clean

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Platform %PLATFORM% %CLEAN%
