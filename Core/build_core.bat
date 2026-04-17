@echo off
setlocal

pushd "%~dp0" >nul

where g++ >nul 2>nul
if %errorlevel%==0 (
    echo [TitanLab] Building core.exe with MinGW g++...
    g++ -std=c++17 -Wall -Wextra -O2 -mwindows -o core.exe main.cpp persistence.cpp network.cpp -luser32 -lshell32 -lws2_32
    if errorlevel 1 (
        echo [TitanLab] MinGW build failed.
        popd >nul
        exit /b 1
    )

    echo [TitanLab] Build completed: %cd%\core.exe
    popd >nul
    exit /b 0
)

where cl >nul 2>nul
if %errorlevel%==0 (
    echo [TitanLab] Building core.exe with MSVC cl.exe...
    cl /nologo /EHsc /std:c++17 main.cpp persistence.cpp network.cpp /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib ws2_32.lib /OUT:core.exe
    if errorlevel 1 (
        echo [TitanLab] MSVC build failed.
        popd >nul
        exit /b 1
    )

    echo [TitanLab] Build completed: %cd%\core.exe
    popd >nul
    exit /b 0
)

echo [TitanLab] No supported compiler was found.
echo [TitanLab] Install MinGW and add g++ to PATH, or run this script from Developer Command Prompt for Visual Studio.

popd >nul
exit /b 1
