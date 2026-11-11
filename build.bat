@echo off
rem TaiCH Game Filter - build script
rem Licensed under the GNU General Public License v3.0 - see LICENSE.
rem Requires MinGW-w64 (g++, windres). Override the toolchain root with the MINGW variable.
setlocal
cd /d "%~dp0"
if not defined MINGW set "MINGW=D:\toolchains\mingw64"
if not exist "%MINGW%\bin\g++.exe" (
    echo [ERROR] MinGW not found at %MINGW%
    exit /b 1
)
set "PATH=%MINGW%\bin;%PATH%"
if not exist build mkdir build
echo Compiling TaiCH Assistant...
windres -c 65001 src\TaiCH.rc -O coff -o build\TaiCH.res.o
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -mwindows -DUNICODE -D_UNICODE ^
    src\main.cpp build\TaiCH.res.o ^
    third_party\imgui-1.91.5\imgui.cpp ^
    third_party\imgui-1.91.5\imgui_draw.cpp ^
    third_party\imgui-1.91.5\imgui_tables.cpp ^
    third_party\imgui-1.91.5\imgui_widgets.cpp ^
    third_party\imgui-1.91.5\backends\imgui_impl_win32.cpp ^
    third_party\imgui-1.91.5\backends\imgui_impl_dx11.cpp ^
    -I third_party\imgui-1.91.5 ^
    -I third_party\imgui-1.91.5\backends ^
    -o build\TaiCH.exe ^
    -static -static-libgcc -static-libstdc++ ^
    -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lgdiplus -lgdi32 -luser32 -lole32 -lshell32 -limm32
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)
if not exist build\assets mkdir build\assets
copy /Y assets\preview_*.png build\assets\ >nul
if errorlevel 1 exit /b 1
echo [OK] build\TaiCH.exe
