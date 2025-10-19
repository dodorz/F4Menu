@echo off
REM F4Menu Build Script - MSVC Static Linking Only
echo ========================================
echo F4Menu Build Script (MSVC Static)
echo ========================================
echo.

call "C:\Program Files\Microsoft Visual Studio\2022\VC\Auxiliary\Build\vcvars64.bat"

REM Check for MSVC (cl.exe)
where cl.exe >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: MSVC compiler not found!
    echo.
    echo Please run this script from \"Developer Command Prompt for VS\"
    echo.
    pause
    exit /b 1
)

echo Using MSVC Compiler with static runtime linking...
echo.

REM Compile resources
if exist F4Menu.rc (
    rc /nologo /fo F4Menu.res F4Menu.rc
)

REM MSVC compilation with static runtime (/MT)
cl.exe /nologo /W3 /O2 /MT /utf-8 /D_UNICODE /DUNICODE ^
    /Fe:F4Menu.exe F4Menu.c F4Menu.res ^
    /link /SUBSYSTEM:WINDOWS /MACHINE:X64 ^
    user32.lib gdi32.lib shell32.lib comctl32.lib comdlg32.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Build successful! F4Menu.exe created.
    echo ========================================
    
    REM Clean up intermediate files
    if exist F4Menu.obj del F4Menu.obj
    if exist F4Menu.res del F4Menu.res
) else (
    echo.
    echo ========================================
    echo Build failed!
    echo ========================================
    pause
    exit /b 1
)

echo.
pause
