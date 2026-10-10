// Windows 图形界面（原生 Win32）
//
// 为什么要写这个：
//
//   之前编出来的 EXE 是**控制台程序**（终端 TUI）。用户双击出来一个黑窗口，
//   里面还是中文乱码 —— 那不是表格软件。
//
//   乱码的根因是控制台：程序输出 UTF-8，控制台代码页是 GBK（936），
//   于是 UTF-8 字节被按 GBK 解读。虽然设了 SetConsoleOutputCP(65001)，
//   但控制台字体（点阵字体）不支持时照样是乱码 —— 这个问题在控制台里
//   无法彻底解决。
//
//   做成 GUI 之后全程走 Unicode 宽字符，乱码自然消失。
//
// 为什么不用 Qt / Python：
//
//   环境里没有 Wine（跑不了 Windows 版 Python + PyInstaller），
//   而 mingw 能直接编出原生 EXE。原生 Win32 的产物只有几 MB 且零依赖，
//   不引入 Qt 运行时这个不可控环节。
//
// 绘制策略与 Qt 版一致：**虚拟化网格** —— 只画可见格子。
// 表格有 104 万行，全画出来必然卡死，而可见的永远只有几十个。

#ifdef _WIN32

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <string>
#include <vector>
#include <memory>
#include <cstdio>

#include "xlsx.hpp"
#include "sheet.hpp"
#include "value.hpp"

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16
//
// 引擎内部一律 UTF-8（std::string），Win32 的 W 系列 API 要 UTF-16。
// 整个文件里凡是跨边界的字符串都必须经过这两个函数。
// ---------------------------------------------------------------------------
namespace {

std::wstring u8(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string u8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::string colToNameStr(int col) { return xl::colToName(col); }

std::string addrName(int c, int r) {
    return colToNameStr(c) + std::to_string(r + 1);
}

}  // namespace

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------
namespace {

const int COLW = 92;
const int ROWH = 24;
const int HDRW = 42;        // 行号列宽
const int HDRH = 22;        // 列标行高
const int MAXC = 400;
const int MAXR = 5000;

std::unique_ptr<xl::Workbook> g_wb;
int g_sheet = 0;

HWND g_hwnd = nullptr;      // 主窗口
HWND g_grid = nullptr;      // 网格（自绘子窗口）
HWND g_edit = nullptr;      // 就地编辑框
HWND g_fx   = nullptr;      // 公式栏输入框
HWND g_addr = nullptr;      // 地址框（静态文本）

int g_curC = 0, g_curR = 0;
int g_scrollX = 0, g_scrollY = 0;
std::wstring g_path;        // 当前文件路径，空 = 未保存
bool g_dirty = false;

xl::Workbook& wb() {
    if (!g_wb) {
        g_wb.reset(new xl::Workbook());
        if (g_wb->sheetCount() == 0) g_wb->addSheet("Sheet1");
        g_sheet = 0;
    }
    return *g_wb;
}

xl::Sheet* cur() {
    xl::Workbook& w = wb();
    if (g_sheet < 0) g_sheet = 0;
    if ((size_t)g_sheet >= w.sheetCount()) g_sheet = (int)w.sheetCount() - 1;
    if (g_sheet < 0) return nullptr;
    return &w.sheet((size_t)g_sheet);
}

// 单元格原文（公式带 =），用于回填编辑框
std::string cellRaw(int c, int r) {
    xl::Sheet* s = cur();
    if (!s) return "";
    std::string f;
    if (s->cellFormula(c, r, f) && !f.empty()) return "=" + f;
    return s->displayRaw(c, r);
}

// 判断输入该当数字、公式还是文本
//
// 这一步不能省：直接存成字符串的话，输入 123 得到文本 "123"，
// 之后 =A1+1 算不出来。Excel 的行为就是"看着像数字就当数字"。
void setCellSmart(int c, int r, const std::string& raw) {
    xl::Sheet* s = cur();
    if (!s) return;

    if (raw.empty()) { s->setValue(c, r, xl::Value::empty()); return; }

    size_t b = raw.find_first_not_of(" \t");
    if (b == std::string::npos) { s->setValue(c, r, xl::Value::empty()); return; }
    std::string t = raw.substr(b);

    if (t[0] == '=') {
        // 引擎存的是不含 '=' 的公式原文，写出 xlsx 时再补 ——
        // 存了 '=' 会变成 ==SUM(...)，Excel 直接打不开
        s->setFormula(c, r, t.substr(1));
        return;
    }

    char* endp = nullptr;
    double d = ::strtod(t.c_str(), &endp);
    if (endp && *endp == '\0') {
        std::string low = t;
        for (char& ch : low) if (ch >= 'A' && ch <= 'Z') ch += 32;
        if (low != "nan" && low != "inf" && low != "infinity" && low != "-inf") {
            s->setValue(c, r, xl::Value::num(d));
            return;
        }
    }
    s->setValue(c, r, xl::Value::str(raw));
}

void afterEdit() {
    xl::Sheet* s = cur();
    if (s) s->recalcDirty();
    g_dirty = true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 网格绘制
// ---------------------------------------------------------------------------
namespace {

void drawGrid(HDC hdc, const RECT& rc) {
    xl::Sheet* s = cur();

    // 背景
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    int vw = rc.right - rc.left;
    int vh = rc.bottom - rc.top;

    int c0 = g_scrollX / COLW;
    int r0 = g_scrollY / ROWH;
    int c1 = c0 + vw / COLW + 1;
    int r1 = r0 + vh / ROWH + 1;
    if (c1 >= MAXC) c1 = MAXC - 1;
    if (r1 >= MAXR) r1 = MAXR - 1;

    HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ old = SelectObject(hdc, hf);
    SetBkMode(hdc, TRANSPARENT);

    // ---- 表头 ----
    HBRUSH hdr = CreateSolidBrush(RGB(240, 240, 240));
    RECT hr;
    // 左上角
    SetRect(&hr, rc.left, rc.top, rc.left + HDRW, rc.top + HDRH);
    FillRect(hdc, &hr, hdr);
    // 列标
    int x = rc.left + HDRW - g_scrollX;
    for (int c = c0; c <= c1; c++, x += COLW) {
        SetRect(&hr, x, rc.top, x + COLW, rc.top + HDRH);
        FillRect(hdc, &hr, hdr);
        std::wstring t = u8(colToNameStr(c));
        DrawTextW(hdc, t.c_str(), (int)t.size(), &hr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // 行号
    int y = rc.top + HDRH - g_scrollY;
    for (int r = r0; r <= r1; r++, y += ROWH) {
        SetRect(&hr, rc.left, y, rc.left + HDRW, y + ROWH);
        FillRect(hdc, &hr, hdr);
        std::wstring t = u8(std::to_string(r + 1));
        DrawTextW(hdc, t.c_str(), (int)t.size(), &hr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    DeleteObject(hdr);

    // ---- 网格线 ----
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(226, 226, 226));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    x = rc.left + HDRW - g_scrollX;
    for (int c = c0; c <= c1; c++, x += COLW) {
        MoveToEx(hdc, x, rc.top, nullptr);
        LineTo(hdc, x, rc.bottom);
    }
    y = rc.top + HDRH - g_scrollY;
    for (int r = r0; r <= r1; r++, y += ROWH) {
        MoveToEx(hdc, rc.left, y, nullptr);
        LineTo(hdc, rc.right, y);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    // ---- 单元格 ----
    y = rc.top + HDRH - g_scrollY;
    for (int r = r0; r <= r1; r++, y += ROWH) {
        x = rc.left + HDRW - g_scrollX;
        for (int c = c0; c <= c1; c++, x += COLW) {
            std::string v = s ? s->display(c, r) : std::string();
            bool sel = (c == g_curC && r == g_curR);

            if (sel) {
                HBRUSH sb = CreateSolidBrush(RGB(232, 240, 254));
                RECT cr = {x, y, x + COLW, y + ROWH};
                FillRect(hdc, &cr, sb);
                DeleteObject(sb);
            }
            if (!v.empty()) {
                RECT tr = {x + 3, y, x + COLW - 3, y + ROWH};
                std::wstring t = u8(v);
                DrawTextW(hdc, t.c_str(), (int)t.size(), &tr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
    }

    // 当前格加粗边框
    {
        int sx = rc.left + HDRW - g_scrollX + g_curC * COLW;
        int sy = rc.top + HDRH - g_scrollY + g_curR * ROWH;
        HPEN p2 = CreatePen(PS_SOLID, 2, RGB(26, 115, 232));
        HGDIOBJ op = SelectObject(hdc, p2);
        MoveToEx(hdc, sx, sy, nullptr);
        LineTo(hdc, sx + COLW, sy);
        LineTo(hdc, sx + COLW, sy + ROWH);
        LineTo(hdc, sx, sy + ROWH);
        LineTo(hdc, sx, sy);
        SelectObject(hdc, op);
        DeleteObject(p2);
    }

    SelectObject(hdc, old);
}

void ensureVisible() {
    RECT rc;
    GetClientRect(g_grid, &rc);
    int vw = rc.right - rc.left - HDRW;
    int vh = rc.bottom - rc.top - HDRH;

    int cx = g_curC * COLW;
    int cy = g_curR * ROWH;
    if (cx < g_scrollX) g_scrollX = cx;
    if (cx + COLW > g_scrollX + vw) g_scrollX = cx + COLW - vw;
    if (cy < g_scrollY) g_scrollY = cy;
    if (cy + ROWH > g_scrollY + vh) g_scrollY = cy + ROWH - vh;
    if (g_scrollX < 0) g_scrollX = 0;
    if (g_scrollY < 0) g_scrollY = 0;
}

void selectCell(int c, int r) {
    if (c < 0) c = 0;
    if (r < 0) r = 0;
    if (c >= MAXC) c = MAXC - 1;
    if (r >= MAXR) r = MAXR - 1;
    g_curC = c; g_curR = r;
    ensureVisible();
    SetWindowTextW(g_addr, u8(addrName(c, r)).c_str());
    SetWindowTextW(g_fx, u8(cellRaw(c, r)).c_str());
    InvalidateRect(g_grid, nullptr, FALSE);
}

}  // namespace

// ---------------------------------------------------------------------------
// 就地编辑
// ---------------------------------------------------------------------------
namespace {

void openEdit() {
    RECT rc;
    GetClientRect(g_grid, &rc);
    int x = HDRW - g_scrollX + g_curC * COLW;
    int y = HDRH - g_scrollY + g_curR * ROWH;
    // 被表头遮住时不弹编辑框（滚过头了）
    if (x < HDRW || y < HDRH) { ensureVisible(); return; }
    SetWindowPos(g_edit, nullptr, x, y, COLW + 1, ROWH + 1,
                 SWP_NOZORDER | SWP_SHOWWINDOW);
    SetWindowTextW(g_edit, u8(cellRaw(g_curC, g_curR)).c_str());
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
}

void commitEdit(bool advance) {
    if (!IsWindowVisible(g_edit)) return;
    wchar_t buf[4096];
    GetWindowTextW(g_edit, buf, 4096);
    ShowWindow(g_edit, SW_HIDE);
    setCellSmart(g_curC, g_curR, u8(std::wstring(buf)));
    afterEdit();
    if (advance) selectCell(g_curC, g_curR + 1);
    else selectCell(g_curC, g_curR);
    InvalidateRect(g_grid, nullptr, FALSE);
}

void cancelEdit() {
    ShowWindow(g_edit, SW_HIDE);
    SetFocus(g_grid);
}

void commitFx() {
    wchar_t buf[4096];
    GetWindowTextW(g_fx, buf, 4096);
    setCellSmart(g_curC, g_curR, u8(std::wstring(buf)));
    afterEdit();
    selectCell(g_curC, g_curR);
    InvalidateRect(g_grid, nullptr, FALSE);
}

}  // namespace

// ---------------------------------------------------------------------------
// 文件操作
// ---------------------------------------------------------------------------
namespace {

bool openFileDlg(std::wstring& out, bool saveAs) {
    wchar_t buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Excel 工作簿 (*.xlsx)\0*.xlsx\0所有文件\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (saveAs) {
        ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"xlsx";
    }
    BOOL ok = saveAs ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return false;
    out = buf;
    return true;
}

void doOpen() {
    std::wstring p;
    if (!openFileDlg(p, false)) return;
    std::string err;
    std::unique_ptr<xl::Workbook> nb(new xl::Workbook());
    if (!nb->load(u8(p), err)) {
        MessageBoxW(g_hwnd, u8("打开失败: " + err).c_str(), L"xlengine", MB_ICONERROR);
        return;
    }
    g_wb = std::move(nb);
    g_sheet = 0;
    g_path = p;
    g_dirty = false;
    g_curC = g_curR = 0;
    g_scrollX = g_scrollY = 0;
    InvalidateRect(g_grid, nullptr, FALSE);
    selectCell(0, 0);
}

void doSave(bool saveAs) {
    std::wstring p = g_path;
    if (saveAs || p.empty()) {
        if (!openFileDlg(p, true)) return;
    }
    std::string err;
    if (!wb().save(u8(p), err)) {
        MessageBoxW(g_hwnd, u8("保存失败: " + (err.empty() ? std::string("未知错误") : err)).c_str(),
                    L"xlengine", MB_ICONERROR);
        return;
    }
    g_path = p;
    g_dirty = false;
}

void doNew() {
    g_wb.reset(new xl::Workbook());
    if (g_wb->sheetCount() == 0) g_wb->addSheet("Sheet1");
    g_sheet = 0;
    g_path.clear();
    g_dirty = false;
    g_curC = g_curR = 0;
    g_scrollX = g_scrollY = 0;
    InvalidateRect(g_grid, nullptr, FALSE);
    selectCell(0, 0);
}

void updateTitle() {
    std::string t = "xlengine";
    if (!g_path.empty()) {
        std::string p = u8(g_path);
        size_t sl = p.find_last_of("\\/");
        t += " - " + (sl == std::string::npos ? p : p.substr(sl + 1));
    }
    if (g_dirty) t += " *";
    SetWindowTextW(g_hwnd, u8(t).c_str());
}

}  // namespace

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
namespace {

const int ID_FX = 1001;
const int ID_EDIT = 1002;
const int IDM_OPEN = 2001, IDM_SAVE = 2002, IDM_SAVEAS = 2003, IDM_NEW = 2004;
const int IDM_INSROW = 2011, IDM_DELROW = 2012, IDM_INSCOL = 2013, IDM_DELCOL = 2014;
const int IDM_SUM = 2021;

LRESULT CALLBACK gridProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            // 双缓冲：直接在窗口 DC 上画会闪，尤其滚动时
            RECT rc;
            GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ ob = SelectObject(mem, bm);
            drawGrid(mem, rc);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bm);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;   // 交给 WM_PAINT 画背景，避免闪烁
        case WM_LBUTTONDOWN: {
            int px = LOWORD(l), py = HIWORD(l);
            int c = (px - HDRW + g_scrollX) / COLW;
            int r = (py - HDRH + g_scrollY) / ROWH;
            if (c >= 0 && r >= 0) {
                commitEdit(false);
                selectCell(c, r);
                SetFocus(h);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int px = LOWORD(l), py = HIWORD(l);
            int c = (px - HDRW + g_scrollX) / COLW;
            int r = (py - HDRH + g_scrollY) / ROWH;
            if (c >= 0 && r >= 0) { selectCell(c, r); openEdit(); }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int d = GET_WHEEL_DELTA_WPARAM(w);
            g_scrollY -= d / WHEEL_DELTA * 3 * ROWH;
            if (g_scrollY < 0) g_scrollY = 0;
            commitEdit(false);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_VSCROLL: {
            int code = LOWORD(w);
            if (code == SB_LINEUP) g_scrollY -= ROWH;
            else if (code == SB_LINEDOWN) g_scrollY += ROWH;
            else if (code == SB_PAGEUP) g_scrollY -= 10 * ROWH;
            else if (code == SB_PAGEDOWN) g_scrollY += 10 * ROWH;
            if (g_scrollY < 0) g_scrollY = 0;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_HSCROLL: {
            int code = LOWORD(w);
            if (code == SB_LINELEFT) g_scrollX -= COLW;
            else if (code == SB_LINERIGHT) g_scrollX += COLW;
            if (g_scrollX < 0) g_scrollX = 0;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN: {
            // 直接打字进入编辑（Excel 的行为），不必先双击
            if (w >= 0x20 && w <= 0x7E) {
                openEdit();
                if (IsWindowVisible(g_edit)) {
                    wchar_t ch[2] = {(wchar_t)w, 0};
                    SetWindowTextW(g_edit, ch);
                    SendMessageW(g_edit, EM_SETSEL, 1, 1);
                }
                return 0;
            }
            if (w == VK_F2) { openEdit(); return 0; }
            commitEdit(false);
            int dc = 0, dr = 0;
            if (w == VK_LEFT) dc = -1;
            else if (w == VK_RIGHT) dc = 1;
            else if (w == VK_UP) dr = -1;
            else if (w == VK_DOWN) dr = 1;
            else if (w == VK_RETURN) { openEdit(); return 0; }
            else if (w == VK_DELETE) {
                setCellSmart(g_curC, g_curR, "");
                afterEdit();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            } else return 0;
            selectCell(g_curC + dc, g_curR + dr);
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK mainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE:
            return 0;
        case WM_SIZE: {
            int ww = LOWORD(l), wh = HIWORD(l);
            // 布局：工具栏(地址+公式栏) / 网格 / 底部提示
            int barH = 30, botH = 26;
            if (g_addr) SetWindowPos(g_addr, nullptr, 4, 4, 70, barH - 8, SWP_NOZORDER);
            if (g_fx) SetWindowPos(g_fx, nullptr, 78, 3, ww - 90, barH - 8, SWP_NOZORDER);
            if (g_grid) SetWindowPos(g_grid, nullptr, 0, barH, ww, wh - barH - botH, SWP_NOZORDER);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(w);
            if (id == IDM_OPEN) { doOpen(); updateTitle(); return 0; }
            if (id == IDM_SAVE) { doSave(false); updateTitle(); return 0; }
            if (id == IDM_SAVEAS) { doSave(true); updateTitle(); return 0; }
            if (id == IDM_NEW) { doNew(); updateTitle(); return 0; }
            if (id == IDM_INSROW) {
                if (cur()) { cur()->insertRows(g_curR, 1); afterEdit(); InvalidateRect(g_grid, nullptr, FALSE); }
                return 0;
            }
            if (id == IDM_DELROW) {
                if (cur()) { cur()->deleteRows(g_curR, 1); afterEdit(); InvalidateRect(g_grid, nullptr, FALSE); }
                return 0;
            }
            if (id == IDM_INSCOL) {
                if (cur()) { cur()->insertCols(g_curC, 1); afterEdit(); InvalidateRect(g_grid, nullptr, FALSE); }
                return 0;
            }
            if (id == IDM_DELCOL) {
                if (cur()) { cur()->deleteCols(g_curC, 1); afterEdit(); InvalidateRect(g_grid, nullptr, FALSE); }
                return 0;
            }
            if (id == IDM_SUM) {
                std::string f = "=SUM(" + colToNameStr(g_curC) + "1:" +
                                colToNameStr(g_curC) + std::to_string(g_curR) + ")";
                if (g_curR == 0) f = "=SUM(" + colToNameStr(g_curC) + "1:" +
                                     colToNameStr(g_curC) + "1)";
                setCellSmart(g_curC, g_curR, f);
                afterEdit();
                InvalidateRect(g_grid, nullptr, FALSE);
                selectCell(g_curC, g_curR);
                return 0;
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

// ---------------------------------------------------------------------------
int winguiMain(HINSTANCE hInst, int nCmdShow) {
    InitCommonControls();

    // ---- 注册窗口类 ----
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = mainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"xlengineMain";
    if (!RegisterClassW(&wc)) return 1;

    WNDCLASSW gc;
    ZeroMemory(&gc, sizeof(gc));
    gc.lpfnWndProc = gridProc;
    gc.hInstance = hInst;
    gc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    gc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    gc.style = CS_DBLCLKS;
    gc.lpszClassName = L"xlengineGrid";
    if (!RegisterClassW(&gc)) return 1;

    // ---- 主窗口 ----
    g_hwnd = CreateWindowExW(0, L"xlengineMain", L"xlengine",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    // ---- 菜单 ----
    HMENU mb = CreateMenu();
    HMENU mf = CreatePopupMenu();
    AppendMenuW(mf, MF_STRING, IDM_NEW, L"新建\tCtrl+N");
    AppendMenuW(mf, MF_STRING, IDM_OPEN, L"打开...\tCtrl+O");
    AppendMenuW(mf, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mf, MF_STRING, IDM_SAVE, L"保存\tCtrl+S");
    AppendMenuW(mf, MF_STRING, IDM_SAVEAS, L"另存为...");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mf, L"文件");

    HMENU me = CreatePopupMenu();
    AppendMenuW(me, MF_STRING, IDM_INSROW, L"插入行");
    AppendMenuW(me, MF_STRING, IDM_DELROW, L"删除行");
    AppendMenuW(me, MF_STRING, IDM_INSCOL, L"插入列");
    AppendMenuW(me, MF_STRING, IDM_DELCOL, L"删除列");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)me, L"编辑");

    HMENU mi = CreatePopupMenu();
    AppendMenuW(mi, MF_STRING, IDM_SUM, L"求和");
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mi, L"插入");

    SetMenu(g_hwnd, mb);

    // ---- 控件 ----
    g_addr = CreateWindowExW(0, L"STATIC", L"A1",
                             WS_CHILD | WS_VISIBLE | SS_CENTER,
                             0, 0, 70, 22, g_hwnd, nullptr, hInst, nullptr);
    g_fx = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           0, 0, 200, 22, g_hwnd, (HMENU)(INT_PTR)ID_FX, hInst, nullptr);
    g_grid = CreateWindowExW(WS_EX_CLIENTEDGE, L"xlengineGrid", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                             0, 30, 900, 500, g_hwnd, nullptr, hInst, nullptr);
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | ES_AUTOHSCROLL,
                             0, 0, COLW, ROWH, g_grid, (HMENU)(INT_PTR)ID_EDIT, hInst, nullptr);

    // 滚动范围
    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0; si.nMax = MAXR * ROWH; si.nPage = 500;
    SetScrollInfo(g_grid, SB_VERT, &si, TRUE);
    si.nMax = MAXC * COLW; si.nPage = 800;
    SetScrollInfo(g_grid, SB_HORZ, &si, TRUE);

    // 示例数据，打开就能看到东西
    {
        xl::Sheet& sh = wb().sheet(0);
        const char* hdr[] = {"产品", "一月", "二月", "三月", "合计"};
        for (int c = 0; c < 5; c++) sh.setValue(c, 0, xl::Value::str(hdr[c]));
        const char* nm[] = {"键盘", "鼠标", "显示器"};
        double v[3][3] = {{120, 135, 150}, {80, 92, 88}, {900, 950, 1020}};
        for (int r = 0; r < 3; r++) {
            sh.setValue(0, r + 1, xl::Value::str(nm[r]));
            for (int c = 0; c < 3; c++) sh.setValue(c + 1, r + 1, xl::Value::num(v[r][c]));
            sh.setFormula(4, r + 1, "SUM(B" + std::to_string(r + 2) +
                                    ":D" + std::to_string(r + 2) + ")");
        }
        sh.recalcDirty();
    }

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    selectCell(0, 0);
    updateTitle();
    SetFocus(g_grid);

    // ---- 消息循环 ----
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // 方向键/回车交给网格处理，否则会被当成对话框导航键吃掉
        if (msg.message == WM_KEYDOWN && g_grid &&
            (msg.hwnd == g_grid || msg.hwnd == g_edit)) {
            if (msg.wParam == VK_RETURN && msg.hwnd == g_edit) {
                commitEdit(true);
                continue;
            }
            if (msg.wParam == VK_ESCAPE && msg.hwnd == g_edit) {
                cancelEdit();
                continue;
            }
            if (msg.wParam == VK_RETURN && msg.hwnd == g_fx) {
                commitFx();
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

#endif  // _WIN32
