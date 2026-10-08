@echo off
REM ---------------------------------------------------------------------------
REM xlengine 一键打包 Windows EXE（在 Windows 本机编译）
REM
REM   双击运行，或在命令行执行：
REM     build.bat           64 位（默认）
REM     build.bat 32        32 位
REM     build.bat clean     只清理
REM
REM 产物: dist\xlengine.exe （单文件，静态链接，拷到别的机器也能用）
REM
REM 前提（任选其一）：
REM   1. MSYS2   : pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-zlib
REM                然后把 C:\msys64\mingw64\bin 加进 PATH（32 位用 mingw32）
REM   2. MinGW-w64 独立版 / TDM-GCC
REM   3. WinLibs（免安装，解压即用）
REM ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

cd /d "%~dp0"

set BITS=%1
if "%BITS%"=="" set BITS=64
set DIST=dist
set NAME=xlengine.exe

if "%BITS%"=="clean" (
    if exist "%DIST%" rmdir /s /q "%DIST%"
    if exist .build-win rmdir /s /q .build-win
    echo 已清理 %DIST% 与 .build-win
    goto :eof
)

if "%BITS%"=="32" (
    set PREFIX=i686-w64-mingw32
) else (
    set PREFIX=x86_64-w64-mingw32
)

echo == xlengine 打包 Windows EXE ==
echo 目标: %PREFIX% ^(%BITS% 位^)

REM ---- 定位编译器 ----
REM 常见布局:
REM   MSYS2 独立工具链 : g++            （PATH 已指向 mingw64\bin）
REM   带前缀的工具链    : x86_64-w64-mingw32-g++
set CXX=
where g++ >nul 2>&1
if !errorlevel!==0 (
    for /f "delims=" %%i in ('where g++') do (
        echo %%i | findstr /i "mingw" >nul
        if !errorlevel!==0 set CXX=g++
    )
)
if "!CXX!"=="" (
    where %PREFIX%-g++ >nul 2>&1
    if !errorlevel!==0 set CXX=%PREFIX%-g++
)

if "!CXX!"=="" (
    echo.
    echo 错误: 没找到 MinGW 的 g++。
    echo.
    echo 请先安装并把它加入 PATH，推荐 MSYS2：
    echo   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-zlib
    echo   然后把 C:\msys64\mingw64\bin 加入 PATH
    echo.
    echo 免安装方案：下载 WinLibs 解压后，把 mingw64\bin 临时加入 PATH 再运行本脚本。
    goto :fail
)

echo 编译器: !CXX!

REM ---- 收集源文件 ----
if not exist .build-win mkdir .build-win
set SRCS=
for %%f in (src\*.cpp) do set SRCS=!SRCS! %%f

echo -- 编译 --
REM -static* : 静态链接运行时，拷到没装 MinGW 的机器也能跑
!CXX! -std=c++17 -O2 -Wall -Wextra ^
    -D_CRT_SECURE_NO_WARNINGS ^
    -D_WIN32_WINNT=0x0600 ^
    -I src ^
    -o .build-win\%NAME% ^
    !SRCS! ^
    -static -static-libgcc -static-libstdc++ ^
    -lwinmm ^
    -lz

if !errorlevel! neq 0 (
    echo.
    echo 编译失败。若提示找不到 zlib，请先安装：
    echo   pacman -S mingw-w64-x86_64-zlib
    goto :fail
)

echo -- 精简符号 --
where strip >nul 2>&1
if !errorlevel!==0 strip --strip-all .build-win\%NAME%

if not exist %DIST% mkdir %DIST%
move /y .build-win\%NAME% %DIST%\%NAME% >nul
rmdir /q .build-win 2>nul

echo.
echo == 完成 ==
echo 产物: %DIST%\%NAME%
echo.
echo 用法：
echo   xlengine.exe                 启动终端表格界面
echo   xlengine.exe 文件.xlsx       打开 xlsx
echo   xlengine.exe --help          查看全部命令
echo.
echo 提示: 建议在 Windows Terminal 或 PowerShell 里运行。
goto :eof

:fail
endlocal & exit /b 1
