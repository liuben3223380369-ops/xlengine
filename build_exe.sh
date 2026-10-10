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

# ---- mingw 系统头文件 ----
#
# 有些环境下编译器（尤其是被复制到非标准 prefix 的 mingw）推导不出自己的
# 系统头文件目录，于是 #include <windows.h> 报 No such file or directory。
# 这里显式探测并补 -isystem：正常安装的 mingw 本来就找得到，加了也无害。
echo "-- 检查 mingw 系统头文件 --"
MINGW_INC=""
for d in "/usr/${TRIPLE}/include" "/usr/local/${TRIPLE}/include" \
         "/opt/${TRIPLE}/include" "$(dirname "$(command -v "$CXX")")/../${TRIPLE}/include"; do
    if [ -f "$d/windows.h" ]; then MINGW_INC="$d"; break; fi
done
if [ -n "$MINGW_INC" ]; then
    echo "   $MINGW_INC"
    # 必须是 -idirafter（追加到搜索链末尾），**不能**用 -isystem。
    # -isystem 会把目录插到 C++ 头目录之前，于是 C++ 的 stdlib.h 里那句
    # #include_next <stdlib.h> 会跳过它 —— 报 "stdlib.h: No such file"。
    SYSINC="-idirafter $MINGW_INC"
else
    echo "   （未显式指定，用编译器默认搜索路径）"
    SYSINC=""
fi

# ---- 编译 ----
echo "-- 编译 --"
mkdir -p .build-win
# 排除两个文件：
#   main.cpp  —— 命令行入口（控制台子系统用，GUI 不需要）
#   tui.cpp   —— 终端 UI，依赖 termios / Windows Console API
# 换成 winmain.cpp（wWinMain 入口）+ wingui.cpp（原生 Win32 界面，已被 ls 收录）。
# 不要再手动追加 src/winmain.cpp —— 上面的 ls 已经收录它了，
# 追加会让它被编译两次，链接时报 multiple definition of `wWinMain`。
SRCS=$(ls src/*.cpp | grep -v 'src/main.cpp' | grep -v 'src/tui.cpp' | tr '\n' ' ')

COMMON="-std=c++17 -O2 -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS \
        -D_WIN32_WINNT=0x0600 -I src $SYSINC"

# 并行编译成 .o 再链接。
# 一次性把所有 .cpp 交给 g++ 是单进程串行的，40 多个文件要好几分钟；
# 分开编译可以用满多核，而且中途失败时能一眼看出是哪个文件。
JOBS=$(nproc 2>/dev/null || echo 2)
echo "   并行度: $JOBS"
OBJS=""
FAIL=0
for f in $SRCS; do
    o=".build-win/$(basename "$f" .cpp).o"
    OBJS="$OBJS $o"
done

# 分批后台并行：每 JOBS 个一批，批内并发、批间等待。
# 比 xargs -P 好排查 —— 出错时能看到具体是哪个文件的报错。
n=0
for f in $SRCS; do
    o=".build-win/$(basename "$f" .cpp).o"
    "$CXX" $COMMON -c "$f" -o "$o" 2>&1 | sed "s|^|   [$f] |" &
    n=$((n + 1))
    if [ $((n % JOBS)) -eq 0 ]; then wait; fi
done
wait

# 校验：每个源文件都必须有对应的 .o，缺任一个就别往下链接
MISSING=""
for f in $SRCS; do
    o=".build-win/$(basename "$f" .cpp).o"
    [ -s "$o" ] || MISSING="$MISSING $f"
done
if [ -n "$MISSING" ]; then
    echo "错误: 以下文件编译失败:$MISSING"
    exit 1
fi

# -mwindows : GUI 子系统。不加的话链接器按控制台子系统处理，
#             入口是 main()，双击出来先弹一个黑窗口 —— 而且中文会因为
#             控制台代码页是 GBK(936) 变成乱码。
# -municode : wWinMain 是宽字符入口，不加会报 undefined reference to WinMain
# -static-* : 静态链接运行时，目标机器无需装 MinGW DLL
echo "   链接"
"$CXX" $COMMON \
    -o ".build-win/$NAME" \
    $OBJS \
    -mwindows -municode \
    -static -static-libgcc -static-libstdc++ \
    -lwinmm -lcomctl32 -lcomdlg32 -lgdi32 -luser32 \
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
