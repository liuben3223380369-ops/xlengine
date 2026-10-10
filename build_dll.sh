#!/usr/bin/env bash
#
# 用 mingw-w64 交叉编译出 Windows 用的 xlengine.dll。
#
# 这个 DLL 导出 gui_bridge.cpp 里的 extern "C" 接口（xl_* 一族），
# Python 界面通过 ctypes 调用它 —— 计算全在 C++ 里，Python 只管画界面。
#
# 为什么要单独出 DLL、而不是让 Windows 上现编：
#   Windows 的 CI runner 上装 mingw 不稳定（Chocolatey 源时常超时），
#   而在 Linux 上交叉编译已经在 build-exe.yml 里验证过多年是稳定的。
#   所以分工：Linux 编 DLL -> 当 artifact 传给 Windows job -> 那边只做打包。
#
# 产物: dist/xlengine.dll

set -e

NAME="xlengine.dll"
DIST="dist"

BITS="${1:-64}"
if [ "$BITS" = "32" ]; then
    TRIPLE="i686-w64-mingw32"
else
    TRIPLE="x86_64-w64-mingw32"
fi

CXX="${TRIPLE}-g++"

echo "== 编译 Windows DLL =="
echo "目标: ${TRIPLE}（${BITS} 位）"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "错误: 找不到 $CXX"
    echo "  Debian/Ubuntu : sudo apt install mingw-w64 libz-mingw-w64-dev"
    exit 1
fi

# ---- zlib ----
ZLIB_FLAG="-lz"
if command -v "${TRIPLE}-pkg-config" >/dev/null 2>&1; then
    if "${TRIPLE}-pkg-config" --exists zlib 2>/dev/null; then
        ZLIB_FLAG="$("${TRIPLE}-pkg-config" --libs --static zlib)"
    fi
else
    for d in /usr/"${TRIPLE}"/lib /usr/lib/gcc/"${TRIPLE}"/*/; do
        if ls "$d"/libz.a >/dev/null 2>&1; then
            ZLIB_FLAG="-L$d -lz"
            break
        fi
    done
fi
echo "   zlib: $ZLIB_FLAG"

# ---- mingw 系统头文件 ----
MINGW_INC=""
for d in "/usr/${TRIPLE}/include" "/usr/local/${TRIPLE}/include" "/opt/${TRIPLE}/include"; do
    if [ -f "$d/windows.h" ]; then MINGW_INC="$d"; break; fi
done
# -idirafter（追加到搜索链末尾），不能用 -isystem：
# -isystem 会插到 C++ 头目录之前，于是 stdlib.h 的 #include_next 会跳过它
SYSINC=""
[ -n "$MINGW_INC" ] && SYSINC="-idirafter $MINGW_INC"

# ---- 编译 ----
mkdir -p .build-dll
# 排除四个文件 —— 少排除任何一个都会在链接期报 undefined reference：
#   main.cpp    : 命令行入口（控制台子系统用）
#   tui.cpp     : 终端 UI（依赖 termios / Windows Console API）
#   wingui.cpp  : 原生 Win32 界面，**EXE 专用**。它调 CreateWindowExW 等，
#                 编进 DLL 但不链 -lgdi32/-luser32 就会链接失败
#   winmain.cpp : wWinMain 入口，与 DLL 无关（重复还会 multiple definition）
SRCS=$(ls src/*.cpp \
        | grep -v -e 'src/main.cpp' -e 'src/tui.cpp' \
              -e 'src/wingui.cpp' -e 'src/winmain.cpp' \
        | tr '\n' ' ')

COMMON="-std=c++17 -O2 -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS \
        -D_WIN32_WINNT=0x0600 -I src $SYSINC -fPIC"

JOBS=$(nproc 2>/dev/null || echo 2)
echo "   并行度: $JOBS"
n=0
for f in $SRCS; do
    o=".build-dll/$(basename "$f" .cpp).o"
    "$CXX" $COMMON -c "$f" -o "$o" 2>&1 | sed "s|^|   [$f] |" &
    n=$((n + 1))
    if [ $((n % JOBS)) -eq 0 ]; then wait; fi
done
wait

MISSING=""
for f in $SRCS; do
    o=".build-dll/$(basename "$f" .cpp).o"
    [ -s "$o" ] || MISSING="$MISSING $f"
done
if [ -n "$MISSING" ]; then
    echo "错误: 编译失败:$MISSING"
    exit 1
fi

echo "   链接 DLL"
OBJS=$(ls .build-dll/*.o | tr '\n' ' ')
"$CXX" $COMMON -shared -o ".build-dll/$NAME" \
    -Wl,--out-implib,.build-dll/libxlengine.a \
    $OBJS \
    -static -static-libgcc -static-libstdc++ \
    -lwinmm -lgdi32 -luser32 $ZLIB_FLAG

mkdir -p "$DIST"
mv ".build-dll/$NAME" "$DIST/$NAME"
rm -rf .build-dll

echo
echo "== 完成 =="
ls -la "$DIST/$NAME" | awk '{printf "   %s  %.1f MB\n", $NF, $5/1048576}'
