// Windows 入口：GUI 子系统（wWinMain，无控制台窗口）。
//
// 之前 EXE 用 main() + 控制台子系统，于是双击出来是一个黑窗口，
// 里面还是终端 TUI —— 而且中文因为控制台代码页是 GBK(936) 变成乱码。
// 改成 GUI 子系统后全程 Unicode 宽字符，中文正常，也没有黑窗口。
//
// 命令行（--test / --pdf 等）在 Linux 上跑，Windows 这边只出 GUI。

#ifdef _WIN32

#include <windows.h>

int winguiMain(HINSTANCE hInst, int nCmdShow);

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    return winguiMain(hInst, nCmdShow);
}

#endif  // _WIN32
