#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# xlengine 一键打包 Windows EXE（Linux 上用 mingw-w64 交叉编译）
#
#   ./build_exe.sh            # 64 位（默认）
#   ./build_exe.sh 32         # 32 位
#   ./build_exe.sh clean      # 只清理
#
# 产物：dist/xlengine.exe（单文件，静态链接，拷到 Windows 双击即可用）
#
# 前提：装 mingw-w64
#   Debian/Ubuntu : sudo apt install mingw-w64
#   Fedora        : sudo dnf install mingw64-gcc-c++ mingw64-zlib-static
#   Arch          : sudo pacman -S mingw-w64-gcc
#   macOS         : brew install mingw-w64
# ---------------------------------------------------------------------------
set -euo pipefail

cd "$(dirname "$0")"

BITS="${1:-64}"
DIST="dist"
NAME="xlengine.exe"

if [ "$BITS" = "clean" ]; then
    rm -rf "$DIST" .build-win
    echo "已清理 $DIST 与 .build-win"
    exit 0
fi

if [ "$BITS" = "32" ]; then
    TRIPLE="i686-w64-mingw32"
else
    TRIPLE="x86_64-w64-mingw32"
fi

CXX="${TRIPLE}-g++"
STRIP="${TRIPLE}-strip"

echo "== xlengine 打包 Windows EXE =="
echo "目标: ${TRIPLE}（${BITS} 位）"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo
    echo "错误: 找不到 $CXX"
    echo
    echo "请先安装 mingw-w64 交叉编译器："
    echo "  Debian/Ubuntu : sudo apt install mingw-w64 libz-mingw-w64-dev"
    echo "  Fedora        : sudo dnf install mingw64-gcc-c++ mingw64-zlib-static"
    echo "  Arch          : sudo pacman -S mingw-w64-gcc"
    echo "  macOS         : brew install mingw-w64"
    echo
    echo "注意：Debian/Ubuntu 的 mingw-w64 只是元包，**不含** zlib 头文件。"
    echo "      少了 libz-mingw-w64-dev，编译 canvas.cpp / zip.cpp 时会报："
    echo "      zlib.h: No such file or directory"
    echo
    echo "如果你在 Windows 本机上编译，改用 build.bat（见同目录）。"
    exit 1
fi

# ---- zlib ----
# 优先用 mingw 自带的静态 zlib；找不到就退回 -lz（多数发行版打包了）。
echo "-- 检查 zlib --"
ZLIB_FLAG="-lz"
if command -v "${TRIPLE}-pkg-config" >/dev/null 2>&1; then
    if "${TRIPLE}-pkg-config" --exists zlib 2>/dev/null; then
        ZLIB_FLAG="$("${TRIPLE}-pkg-config" --libs --static zlib)"
        echo "   用 pkg-config: $ZLIB_FLAG"
    fi
else
    # 直接找静态库，尽量静态链接，避免运行时依赖 libz dll
    for d in /usr/"${TRIPLE}"/lib /usr/lib/gcc/"${TRIPLE}"/*/ /usr/"${TRIPLE}"/lib/*/; do
        if ls "$d"/libz.a >/dev/null 2>&1; then
            ZLIB_FLAG="-L$d -lz"
            echo "   用静态库: $d/libz.a"
            break
        fi
    done
fi
if ! echo "$ZLIB_FLAG" | grep -q "z"; then
    echo
    echo "错误: 找不到跨平台的 zlib（libz.a 或 pkg-config 条目）。"
    echo "  Debian/Ubuntu : sudo apt install libz-mingw-w64-dev"
    echo "  Fedora        : sudo dnf install mingw64-zlib-static"
    exit 1
fi
echo "   链接参数: $ZLIB_FLAG"

# ---- 编译 ----
echo "-- 编译 --"
mkdir -p .build-win
SRCS=$(ls src/*.cpp | grep -v 'src/main.cpp' | tr '\n' ' ')
# 单独列 main.cpp，避免上面的 grep 把命令行截断
SRCS="$SRCS src/main.cpp"

# -D_CRT_SECURE_NO_WARNINGS : MSVC 风格的安全警告（这里不用 *_s，但保留以防运行环境差异）
# -static-*                 : 静态链接运行时，目标机器无需装 MinGW DLL
# -municode 不用            : 我们用 main() 不是 wmain()
"$CXX" -std=c++17 -O2 -Wall -Wextra \
    -D_CRT_SECURE_NO_WARNINGS \
    -D_WIN32_WINNT=0x0600 \
    -I src \
    -o ".build-win/$NAME" \
    $SRCS \
    -static -static-libgcc -static-libstdc++ \
    -lwinmm \
    $ZLIB_FLAG

echo "-- 精简符号 --"
if command -v "$STRIP" >/dev/null 2>&1; then
    "$STRIP" --strip-all ".build-win/$NAME" || true
fi

mkdir -p "$DIST"
mv ".build-win/$NAME" "$DIST/$NAME"
rm -rf .build-win

echo
echo "== 完成 =="
SIZE=$(du -h "$DIST/$NAME" 2>/dev/null | cut -f1)
echo "产物: $DIST/$NAME  （${SIZE:-未知}）"
echo
echo "用法（Windows 上）："
echo "  xlengine.exe                 启动终端表格界面"
echo "  xlengine.exe 文件.xlsx       打开 xlsx"
echo "  xlengine.exe --help          查看全部命令"
echo
echo "提示: 建议在 Windows Terminal 或 PowerShell 里运行，"
echo "      并确认控制台字体支持中文（否则中文显示为方框）。"
