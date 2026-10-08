// 最小 Windows API shim —— 只为在 Linux 上做语法检查。
// 不实现任何行为，只提供签名以验证类型/函数名正确。
#pragma once
#include <cstddef>
#include <cstdint>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned short WORD;
typedef void* HANDLE;
typedef const char* LPCSTR;
typedef char* LPSTR;
#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
#define ENABLE_ECHO_INPUT  0x0004
#define ENABLE_LINE_INPUT  0x0002
#define ENABLE_PROCESSED_INPUT 0x0001
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#define ENABLE_PROCESSED_OUTPUT 0x0001
#define KEY_EVENT_RECORD_KEY 0x0001
#define KEY_EVENT 0x0001

typedef struct { short X, Y; } COORD;
typedef struct { short Left, Top, Right, Bottom; } SMALL_RECT;
typedef struct {
    COORD dwSize; COORD dwCursorPosition; WORD wAttributes;
    SMALL_RECT srWindow; COORD dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO;
typedef struct { char AsciiChar; wchar_t UnicodeChar; } CHAR_INFO_A_CHAR;
typedef union { char AsciiChar; wchar_t UnicodeChar; } WIN_CHAR_UNION;
typedef struct {
    BOOL bKeyDown;
    WORD wRepeatCount;
    WORD wVirtualKeyCode;
    WORD wVirtualScanCode;
    WIN_CHAR_UNION uChar;
    DWORD dwControlKeyState;
} KEY_EVENT_RECORD;
typedef struct {
    WORD EventType;
    union { KEY_EVENT_RECORD KeyEvent; /* 省略其它事件类型 */ } Event;
} INPUT_RECORD;

extern "C" {
HANDLE GetStdHandle(DWORD n);
BOOL GetConsoleMode(HANDLE h, DWORD* mode);
BOOL SetConsoleMode(HANDLE h, DWORD mode);
BOOL SetConsoleCP(unsigned int cp);
BOOL SetConsoleOutputCP(unsigned int cp);
unsigned int GetConsoleCP(void);
BOOL GetConsoleScreenBufferInfo(HANDLE h, CONSOLE_SCREEN_BUFFER_INFO* ci);
BOOL GetNumberOfConsoleInputEvents(HANDLE h, DWORD* n);
BOOL PeekConsoleInputA(HANDLE h, INPUT_RECORD* buf, DWORD len, DWORD* n);
BOOL ReadConsoleInputA(HANDLE h, INPUT_RECORD* buf, DWORD len, DWORD* n);
BOOL WriteConsoleA(HANDLE h, const void* buf, DWORD n, DWORD* written, void* reserved);
BOOL SetConsoleCursorPosition(HANDLE h, COORD c);
}
