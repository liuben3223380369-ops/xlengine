cmake_minimum_required(VERSION 3.22.1)
project(xlengine-android CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 引擎源码目录：android/app/src/main/cpp -> 仓库根/src
#   cpp -> main -> src -> app -> android -> 根
set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src")

file(GLOB ENGINE_SRCS "${ENGINE_DIR}/*.cpp")

# 排除两个依赖终端的入口：
#   main.cpp —— 命令行入口
#   tui.cpp  —— 终端 UI（依赖 termios / Windows Console API，Android 上没有）
# 引擎核心（词法/语法/求值/函数/xlsx/PDF/图表）与终端无关，可直接复用。
list(REMOVE_ITEM ENGINE_SRCS
     "${ENGINE_DIR}/main.cpp"
     "${ENGINE_DIR}/tui.cpp")

# Android 平台自带 zlib（PNG 与 xlsx 压缩需要）
find_library(ZLIB_LIB z)

add_library(xlengine SHARED
        jni_bridge.cpp
        ${ENGINE_SRCS})

target_include_directories(xlengine PRIVATE "${ENGINE_DIR}")

target_link_libraries(xlengine
        ${ZLIB_LIB}
        log)
