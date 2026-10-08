#include "tui.hpp"
#include "xlsx.hpp"
#include "refshift.hpp"
#include "numfmt.hpp"
#include "style.hpp"
#include "cf.hpp"
#include "pdf.hpp"
#include "sheetpdf.hpp"
#include "chart.hpp"
#include "functions.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <cctype>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/select.h>
#endif

// 仅在有 termios 的平台启用真实终端控制
#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#define XL_HAVE_TERMIOS 1
#endif

// Windows：用原生控制台 API。
// 不能靠 termios —— 没有的话 enterRaw() 直接返回 false，
// 打出来的 EXE 就只能渲染一帧、完全不能交互，等于没用。
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <conio.h>
#define XL_HAVE_WINCON 1
#endif

// Windows：用原生控制台 API。
// 不能靠 termios —— 没有的话 enterRaw() 直接返回 false，
// 打出来的 EXE 就只能渲染一帧、完全不能交互，等于没用。
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <conio.h>
#define XL_HAVE_WINCON 1
#endif

namespace xl {

// ---------------------------------------------------------------------------
// UTF-8 与宽度
// ---------------------------------------------------------------------------
std::vector<uint32_t> decodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int n; uint32_t cp;
        if      (c < 0x80)           { n = 0; cp = c; }
        else if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07u; }
        else { out.push_back(c); i++; continue; }
        if (i + (size_t)n >= s.size()) { out.push_back(c); i++; continue; }
        bool ok = true;
        for (int k = 1; k <= n; k++)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) { ok = false; break; }
        if (!ok) { out.push_back(c); i++; continue; }
        for (int k = 1; k <= n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3Fu);
        i += (size_t)n + 1;
        out.push_back(cp);
    }
    return out;
}

int codePointWidth(uint32_t cp) {
    // 东亚宽/全角区间（与 sheetpdf.cpp 里的口径一致）
    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2E80 && cp <= 0x303E) ||
        (cp >= 0x3041 && cp <= 0x33FF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFF00 && cp <= 0xFF60)) return 2;
    if (cp == 0) return 0;
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
    // 组合记号（附加在前面的字符上，不占列）
    if (cp >= 0x0300 && cp <= 0x036F) return 0;
    return 1;
}

int displayWidth(const std::string& s) {
    int w = 0;
    for (uint32_t cp : decodeUtf8(s)) w += codePointWidth(cp);
    return w;
}

std::string fitToWidth(const std::string& s, int width) {
    if (width <= 0) return std::string();
    if (displayWidth(s) <= width) return s;
    std::string out;
    int w = 0;
    const int ellipsisW = 1;                  // "…" 占 1 列
    for (uint32_t cp : decodeUtf8(s)) {
        int cw = codePointWidth(cp);
        if (w + cw > width - ellipsisW) break;
        // 重新编码
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        w += cw;
    }
    out += "\xE2\x80\xA6";                    // U+2026
    return out;
}

std::string padLeftTo(const std::string& s, int width) {
    int pad = width - displayWidth(s);
    if (pad <= 0) return s;
    return std::string((size_t)pad, ' ') + s;
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------
Screen::Screen(int w, int h) : w_(w), h_(h), ch_((size_t)w * h, U' '), at_((size_t)w * h) {}

void Screen::clear(Attr a) {
    for (size_t i = 0; i < ch_.size(); i++) { ch_[i] = U' '; at_[i] = a; }
}

void Screen::put(int x, int y, char32_t c, Attr a) {
    if (!inBounds(x, y)) return;
    size_t i = (size_t)y * w_ + x;
    ch_[i] = c; at_[i] = a;
}

void Screen::putStr(int x, int y, const std::string& utf8, Attr a) {
    int cx = x;
    for (uint32_t cp : decodeUtf8(utf8)) {
        int cw = codePointWidth(cp);
        if (cx >= w_) break;
        put(cx, y, cp, a);
        cx += cw;
        // 宽字符右侧补一个占位，避免半个字被覆盖后留下残影
        if (cw == 2 && cx < w_) put(cx - 1, y, 0, a);
    }
}

void Screen::fillRow(int y, const std::string& utf8, Attr a) {
    for (int x = 0; x < w_; x++) put(x, y, U' ', a);
    putStr(0, y, utf8, a);
}

void Screen::fillCell(int x, int y, int width, const std::string& utf8, Attr a) {
    for (int i = 0; i < width && x + i < w_; i++) put(x + i, y, U' ', a);
    putStr(x, y, utf8, a);
}

bool Screen::at(int x, int y, char32_t& c, Attr& a) const {
    if (!inBounds(x, y)) return false;
    size_t i = (size_t)y * w_ + x;
    c = ch_[i]; a = at_[i];
    return true;
}

int Screen::ansiColor(TuiColor c, bool bg) const {
    int base = bg ? 40 : 30;
    switch (c) {
        case TuiColor::Black:   return base + 0;
        case TuiColor::Red:     return base + 1;
        case TuiColor::Green:   return base + 2;
        case TuiColor::Yellow:  return base + 3;
        case TuiColor::Blue:    return base + 4;
        case TuiColor::Magenta: return base + 5;
        case TuiColor::Cyan:    return base + 6;
        case TuiColor::White:   return base + 7;
        case TuiColor::Gray:    return base + 60 + (bg ? 10 : 0);  // 亮黑=灰
        default:                return base + 9;                    // 默认色
    }
}

std::string Screen::toPlainText() const {
    std::string out;
    for (int y = 0; y < h_; y++) {
        std::string line;
        for (int x = 0; x < w_; x++) {
            char32_t c = ch_[(size_t)y * w_ + x];
            if (c == 0) continue;                 // 宽字符的右半占位
            if (c < 0x80) line.push_back((char)c);
            else if (c < 0x800) {
                line.push_back((char)(0xC0 | (c >> 6)));
                line.push_back((char)(0x80 | (c & 0x3F)));
            } else {
                line.push_back((char)(0xE0 | (c >> 12)));
                line.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
                line.push_back((char)(0x80 | (c & 0x3F)));
            }
        }
        // 去尾部空格
        size_t e = line.size();
        while (e > 0 && (line[e - 1] == ' ' || line[e - 1] == '\0')) e--;
        out += line.substr(0, e);
        if (y + 1 < h_) out += "\n";
    }
    return out;
}

std::string Screen::toAnsi() const {
    std::ostringstream o;
    Attr cur{TuiColor::Default, TuiColor::Default, false, false};
    bool first = true;
    for (int y = 0; y < h_; y++) {
        if (!first) o << "\r\n";
        first = false;
        for (int x = 0; x < w_; x++) {
            char32_t c = ch_[(size_t)y * w_ + x];
            const Attr& a = at_[(size_t)y * w_ + x];
            if (c == 0) continue;
            if (a.fg != cur.fg || a.bg != cur.bg || a.bold != cur.bold || a.reverse != cur.reverse) {
                o << "\x1b[";
                bool sep = false;
                if (a.bold)                  { o << "1";  sep = true; }
                if (a.reverse)               { o << (sep ? ";7" : "7"); sep = true; }
                o << (sep ? ";" : "") << ansiColor(a.fg, false);
                o << ";" << ansiColor(a.bg, true);
                o << "m";
                cur = a;
            }
            if (c < 0x80) o << (char)c;
            else if (c < 0x800) { o << (char)(0xC0 | (c >> 6)) << (char)(0x80 | (c & 0x3F)); }
            else { o << (char)(0xE0 | (c >> 12)) << (char)(0x80 | ((c >> 6) & 0x3F)) << (char)(0x80 | (c & 0x3F)); }
        }
    }
    o << "\x1b[0m";
    return o.str();
}

// ---------------------------------------------------------------------------
// 按键解析
// ---------------------------------------------------------------------------
std::vector<KeyEvent> parseKeys(const std::string& bytes) {
    std::vector<KeyEvent> out;
    size_t i = 0;
    while (i < bytes.size()) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == 0x1B) {
            // CSI 序列：ESC [ [Pn] [; Pm] final
            // 修饰键藏在中间参数里：2 = Shift，5 = Ctrl。
            // 只认最终字符的话 Shift+方向键会和普通方向键混为一谈，选区就没法用了。
            if (i + 1 < bytes.size() && bytes[i + 1] == '[') {
                size_t j = i + 2;
                std::string p1, p2;
                while (j < bytes.size() && bytes[j] >= '0' && bytes[j] <= '9') p1 += bytes[j++];
                if (j < bytes.size() && bytes[j] == ';') {
                    j++;
                    while (j < bytes.size() && bytes[j] >= '0' && bytes[j] <= '9') p2 += bytes[j++];
                }
                if (j < bytes.size()) {
                    char f = bytes[j];
                    int n1 = p1.empty() ? 0 : std::atoi(p1.c_str());
                    int mod = p2.empty() ? 0 : std::atoi(p2.c_str());
                    KeyEvent e;
                    e.shift = (mod == 2);
                    e.ctrl  = (mod == 5);
                    if (f >= 'A' && f <= 'D') {
                        if      (f == 'A') e.key = Key::Up;
                        else if (f == 'B') e.key = Key::Down;
                        else if (f == 'C') e.key = Key::Right;
                        else               e.key = Key::Left;
                    } else if (f == '~') {
                        if      (n1 == 3)  e.key = Key::Delete;
                        else if (n1 == 1)  e.key = Key::Home;
                        else if (n1 == 4)  e.key = Key::End;
                        else if (n1 == 5)  e.key = e.ctrl ? Key::SheetPrev : Key::PageUp;
                        else if (n1 == 6)  e.key = e.ctrl ? Key::SheetNext : Key::PageDown;
                        else if (n1 == 11) e.key = Key::F2;
                        else if (n1 == 15) e.key = Key::F5;
                        else               e.key = Key::Unknown;
                    } else if (f == 'H') e.key = Key::Home;
                    else if (f == 'F')   e.key = Key::End;
                    else                 e.key = Key::Unknown;
                    out.push_back(e);
                    i = j + 1;
                    continue;
                }
                i += 2; continue;
            }
            out.push_back(KeyEvent{Key::Escape, 0, false, false});
            i++; continue;
        }
        if (c == 0x0D || c == 0x0A) { out.push_back(KeyEvent{Key::Enter, 0, false, false}); i++; continue; }
        if (c == 0x09)              { out.push_back(KeyEvent{Key::Tab,   0, false, false}); i++; continue; }
        if (c == 0x7F || c == 0x08) { out.push_back(KeyEvent{Key::Backspace, 0, false, false}); i++; continue; }
        if (c < 0x20) {
            KeyEvent e{Key::Char, 0, true, false};
            e.ch = (char32_t)('a' + c - 1);
            out.push_back(e);
            i++; continue;
        }
        int n = 0;
        if      ((c & 0xE0) == 0xC0) n = 1;
        else if ((c & 0xF0) == 0xE0) n = 2;
        else if ((c & 0xF8) == 0xF0) n = 3;
        if (i + (size_t)n < bytes.size()) {
            uint32_t cp = 0;
            if (n == 0) cp = c;
            else if (n == 1) cp = ((uint32_t)(c & 0x1F) << 6) | (bytes[i+1] & 0x3F);
            else if (n == 2) cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(bytes[i+1] & 0x3F) << 6) | (bytes[i+2] & 0x3F);
            else cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(bytes[i+1] & 0x3F) << 12)
                    | ((uint32_t)(bytes[i+2] & 0x3F) << 6) | (bytes[i+3] & 0x3F);
            out.push_back(KeyEvent{Key::Char, cp, false, false});
            i += (size_t)n + 1;
        } else { i++; }
    }
    return out;
}

// 严格数值解析：只接受整串都是数字的形式。
// 不能用 strtod 的部分匹配 —— 那会把 "12abc" 当成 12，
// 用户在网格里输错内容时会静默变成另一个数。
static bool strictNumber(const std::string& t, Value& v) {
    if (t.empty()) return false;
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-') i++;
    bool digits = false, dot = false;
    for (; i < t.size(); i++) {
        char c = t[i];
        if (c >= '0' && c <= '9') { digits = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        return false;
    }
    if (!digits) return false;
    v = Value::num(std::strtod(t.c_str(), nullptr));
    return true;
}

// "B7" -> (1,6)。要求整串都是地址，不接受前后缀。
static bool cellParseAddr(const std::string& s, int& col, int& row) {
    std::string t;
    for (char c : s) if (!std::isspace((unsigned char)c)) t.push_back((char)std::toupper((unsigned char)c));
    size_t split = 0;
    while (split < t.size() && t[split] >= 'A' && t[split] <= 'Z') split++;
    if (split == 0 || split == t.size()) return false;
    for (size_t i = split; i < t.size(); i++)
        if (t[i] < '0' || t[i] > '9') return false;
    int c = 0;
    for (size_t i = 0; i < split; i++) c = c * 26 + (t[i] - 'A' + 1);
    int r = std::atoi(t.substr(split).c_str());
    if (c < 1 || r < 1) return false;
    col = c - 1; row = r - 1;
    return true;
}

// ---------------------------------------------------------------------------
// Range
// ---------------------------------------------------------------------------
void Range::normalize() {
    if (c0 > c1) std::swap(c0, c1);
    if (r0 > r1) std::swap(r0, r1);
}

std::string Range::addr() const {
    std::string a = addrToStr(c0, r0);
    if (isSingle()) return a;
    return a + ":" + addrToStr(c1, r1);
}

// ---------------------------------------------------------------------------
// GridUI
// ---------------------------------------------------------------------------
void GridUI::refreshTabs() {
    tabNames_.clear();
    if (!wb_) return;
    for (auto& n : wb_->sheetNames()) tabNames_.push_back(n);
}

void GridUI::attach(Workbook* wb, const std::string& path) {
    wb_ = wb;
    path_ = path;
    sheetIdx_ = 0;
    refreshTabs();
    if (wb_ && wb_->sheetCount() == 0) wb_->addSheet("Sheet1");
    refreshTabs();
    sel_ = Range{0, 0, 0, 0};
    anchorCol_ = anchorRow_ = 0;
    status_ = "就绪  — 方向键移动 / Shift+方向键选区 / Enter 编辑 / : 命令";
}

void GridUI::resize(int w, int h) { w_ = std::max(20, w); h_ = std::max(10, h); scrollIntoView(); }

int GridUI::visibleCols() const {
    return std::max(1, (w_ - kRowHeadW) / kColW);
}
int GridUI::visibleRows() const {
    // 顶部 3 行（标题/公式栏/列标），底部 3 行（表签/状态/提示）
    return std::max(1, h_ - 6);
}

Sheet* GridUI::currentSheet() {
    if (!wb_ || sheetIdx_ < 0 || sheetIdx_ >= (int)wb_->sheetCount()) return nullptr;
    return &wb_->sheet((size_t)sheetIdx_);
}

std::string GridUI::sheetName() const {
    if (!wb_ || sheetIdx_ < 0 || sheetIdx_ >= (int)wb_->sheetCount()) return "Sheet1";
    return wb_->sheetNames()[(size_t)sheetIdx_];
}

void GridUI::setSelection(int c0, int r0, int c1, int r1) {
    // sel_ 是唯一真源：光标停在选区的活动端（右下），锚点在起点。
    // 不能反过来由 anchor/cursor 推出 sel_ —— 那样显式设置的选区
    // 会被下一次 clamp 重算成一个格子。
    sel_ = Range{c0, r0, c1, r1};
    sel_.normalize();
    anchorCol_ = sel_.c0; anchorRow_ = sel_.r0;
    curCol_    = sel_.c1; curRow_    = sel_.r1;
    scrollIntoView();
}

void GridUI::collapseSelection() {
    anchorCol_ = curCol_; anchorRow_ = curRow_;
    sel_ = Range{curCol_, curRow_, curCol_, curRow_};
}

void GridUI::clampCursor() {
    Sheet* sh = currentSheet();
    int maxC = sh ? sh->maxCol : 0, maxR = sh ? sh->maxRow : 0;
    curCol_ = std::max(0, std::min(curCol_, maxC));
    curRow_ = std::max(0, std::min(curRow_, maxR));
    anchorCol_ = std::max(0, std::min(anchorCol_, maxC));
    anchorRow_ = std::max(0, std::min(anchorRow_, maxR));
    // 只把选区夹到边界内，不重算
    sel_.c0 = std::max(0, std::min(sel_.c0, maxC)); sel_.c1 = std::max(0, std::min(sel_.c1, maxC));
    sel_.r0 = std::max(0, std::min(sel_.r0, maxR)); sel_.r1 = std::max(0, std::min(sel_.r1, maxR));
}

void GridUI::scrollIntoView() {
    int vr = visibleRows(), vc = visibleCols();
    if (curRow_ < topRow_) topRow_ = curRow_;
    else if (curRow_ >= topRow_ + vr) topRow_ = curRow_ - vr + 1;
    if (curCol_ < leftCol_) leftCol_ = curCol_;
    else if (curCol_ >= leftCol_ + vc) leftCol_ = curCol_ - vc + 1;
    if (topRow_ < 0) topRow_ = 0;
    if (leftCol_ < 0) leftCol_ = 0;
}

void GridUI::moveCursor(int dc, int dr, bool extend) {
    Sheet* sh = currentSheet();
    int maxC = sh ? sh->maxCol : 0, maxR = sh ? sh->maxRow : 0;
    curCol_ = std::max(0, std::min(curCol_ + dc, maxC));
    curRow_ = std::max(0, std::min(curRow_ + dr, maxR));
    if (!extend) { anchorCol_ = curCol_; anchorRow_ = curRow_; }
    sel_.c0 = std::min(anchorCol_, curCol_); sel_.c1 = std::max(anchorCol_, curCol_);
    sel_.r0 = std::min(anchorRow_, curRow_); sel_.r1 = std::max(anchorRow_, curRow_);
    scrollIntoView();
}

std::string GridUI::currentContent() const {
    Sheet* sh = const_cast<GridUI*>(this)->currentSheet();
    if (!sh) return std::string();
    std::string f;
    if (sh->cellFormula(curCol_, curRow_, f) && !f.empty()) return "=" + f;
    const Sheet::CellRec* r = sh->find(curCol_, curRow_);
    if (!r) return std::string();
    return valueToText(r->value);
}

void GridUI::beginEdit(bool replace) {
    if (!currentSheet()) return;
    mode_ = Mode::Edit;
    if (replace) editBuf_ = currentContent();
}

static void encodeCp(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

// ---------------------------------------------------------------------------
// 撤销
// ---------------------------------------------------------------------------
CellSnap GridUI::snapCell(Sheet* sh, int col, int row) const {
    CellSnap s;
    s.col = col; s.row = row;
    const Sheet::CellRec* r = sh ? sh->find(col, row) : nullptr;
    s.existed = (r != nullptr);
    if (r) { s.formula = r->formula; s.hasFormula = r->hasFormula; s.value = r->value; }
    return s;
}

void GridUI::applySnap(Sheet* sh, const CellSnap& s) const {
    if (!s.existed) { sh->eraseCell(s.col, s.row); return; }
    if (s.hasFormula) sh->setFormula(s.col, s.row, s.formula);
    else              sh->setValue(s.col, s.row, s.value);
}

void GridUI::beginUndo(const std::string& label) {
    pending_ = UndoEntry{};
    pending_.label = label;
    pending_.sheet = sheetIdx_;
}

void GridUI::recordCell(Sheet* sh, int col, int row) {
    // 同一次操作里同一格可能被反复改动，只记第一次的"改动前"
    for (auto& p : pending_.diff)
        if (p.first.col == col && p.first.row == row) return;
    pending_.diff.push_back({snapCell(sh, col, row), CellSnap{}});
}

static bool sameValue(const Value& a, const Value& b) {
    if (a.t != b.t) return false;
    switch (a.t) {
        case Value::T::Num:   return a.n == b.n;
        case Value::T::Str:   return a.s == b.s;
        case Value::T::Bool:  return a.b == b.b;
        case Value::T::Error: return a.e == b.e;
        default:              return true;
    }
}

void GridUI::endUndo() {
    Sheet* sh = currentSheet();
    for (auto& p : pending_.diff) p.second = snapCell(sh, p.first.col, p.first.row);
    bool changed = false;
    for (auto& p : pending_.diff) {
        if (p.first.existed != p.second.existed) { changed = true; break; }
        if (p.first.hasFormula != p.second.hasFormula ||
            p.first.formula != p.second.formula) { changed = true; break; }
        if (!sameValue(p.first.value, p.second.value)) { changed = true; break; }
    }
    if (changed) {
        undoStack_.push_back(pending_);
        redoStack_.clear();            // 新操作会作废重做栈
        dirty_ = true;
    }
    pending_.diff.clear();
}

void GridUI::discardUndo() { pending_.diff.clear(); }

bool GridUI::undo(std::string& msg) {
    if (undoStack_.empty()) { msg = "没有可撤销的操作"; return false; }
    UndoEntry e = undoStack_.back();
    undoStack_.pop_back();
    // 目标表可能已被切换，撤销时要先切回去
    if (e.sheet != sheetIdx_) {
        if (e.sheet >= 0 && e.sheet < (int)wb_->sheetCount()) sheetIdx_ = e.sheet;
    }
    Sheet* sh = currentSheet();
    if (!sh) { msg = "撤销失败：表不存在"; return false; }
    for (auto& p : e.diff) applySnap(sh, p.first);
    if (e.fmtOnly) {
        for (int r = 0; r < e.fmtRows; r++)
            for (int cc = 0; cc < e.fmtCols; cc++)
                sh->setNumFmt(e.fmtC0 + cc, e.fmtR0 + r,
                              e.fmtBefore[(size_t)(r * e.fmtCols + cc)]);
        if (e.hasStyle)
            for (int r = 0; r < e.fmtRows; r++)
                for (int cc = 0; cc < e.fmtCols; cc++)
                    sh->setStyle(e.fmtC0 + cc, e.fmtR0 + r,
                                 e.fmtBeforeStyle[(size_t)(r * e.fmtCols + cc)]);
    }
    // 编辑后走增量重算：只算受影响的格子。
    // 全量 recalc() 在这里是 O(全部公式)，表一大每敲一个键都要等。
    if (wb_) wb_->recalcDirty(); else sh->recalcDirty();
    redoStack_.push_back(e);
    msg = "已撤销：" + e.label;
    return true;
}

bool GridUI::redo(std::string& msg) {
    if (redoStack_.empty()) { msg = "没有可重做的操作"; return false; }
    UndoEntry e = redoStack_.back();
    redoStack_.pop_back();
    if (e.sheet != sheetIdx_) {
        if (e.sheet >= 0 && e.sheet < (int)wb_->sheetCount()) sheetIdx_ = e.sheet;
    }
    Sheet* sh = currentSheet();
    if (!sh) { msg = "重做失败：表不存在"; return false; }
    for (auto& p : e.diff) applySnap(sh, p.second);
    if (e.fmtOnly) {
        for (int r = 0; r < e.fmtRows; r++)
            for (int cc = 0; cc < e.fmtCols; cc++)
                sh->setNumFmt(e.fmtC0 + cc, e.fmtR0 + r, e.fmtAfter);
        if (e.hasStyle)
            for (int r = 0; r < e.fmtRows; r++)
                for (int cc = 0; cc < e.fmtCols; cc++)
                    sh->setStyle(e.fmtC0 + cc, e.fmtR0 + r, e.fmtAfterStyle);
    }
    // 编辑后走增量重算：只算受影响的格子。
    // 全量 recalc() 在这里是 O(全部公式)，表一大每敲一个键都要等。
    if (wb_) wb_->recalcDirty(); else sh->recalcDirty();
    undoStack_.push_back(e);
    msg = "已重做：" + e.label;
    return true;
}

// ---------------------------------------------------------------------------
// 编辑
// ---------------------------------------------------------------------------
void GridUI::commitEdit() {
    Sheet* sh = currentSheet();
    if (!sh) { mode_ = Mode::Normal; return; }
    const std::string& t = editBuf_;

    beginUndo("编辑 " + addrToStr(curCol_, curRow_));
    recordCell(sh, curCol_, curRow_);

    std::string err;
    bool rejected = false;
    // 数据验证必须在写入前判定：写进去再判就晚了，非法值已经在表里了。
    // 判定对象是"用户输入的字面值"而不是公式结果 ——
    // 公式的结果要等重算才知道，而拦截发生在键入那一刻。
    if (t.empty()) {
        const DataValidation* dv = currentDv(curCol_, curRow_);
        if (dv && !dv->allowBlank) {
            DvVerdict vd = checkDataValidation(*sh, *dv, curCol_, curRow_, Value::empty());
            if (!vd.ok && vd.stop) {
                discardUndo();
                status_ = "拒绝: " + vd.message;
                mode_ = Mode::Normal;
                rejected = true;
            }
        }
        if (!rejected) sh->eraseCell(curCol_, curRow_);
    } else if (t[0] == '=') {
        err = sh->setFormula(curCol_, curRow_, t.substr(1));
    } else {
        Value v;
        if (strictNumber(t, v)) { /* 数值 */ }
        else if (t == "TRUE" || t == "FALSE") v = Value::boolean(t == "TRUE");
        else v = Value::str(t);

        const DataValidation* dv = currentDv(curCol_, curRow_);
        if (dv) {
            DvVerdict vd = checkDataValidation(*sh, *dv, curCol_, curRow_, v);
            if (!vd.ok && vd.stop) {
                discardUndo();
                status_ = "拒绝: " + vd.message;
                mode_ = Mode::Normal;
                rejected = true;
            }
            if (!vd.ok && !rejected) status_ = "警告: " + vd.message;   // warning/information 放行但提示
        }
        if (!rejected) sh->setValue(curCol_, curRow_, v);
    }
    // 编辑后走增量重算：只算受影响的格子。
    // 全量 recalc() 在这里是 O(全部公式)，表一大每敲一个键都要等。
    if (wb_) wb_->recalcDirty(); else sh->recalcDirty();
    if (rejected) {
        // 已经在上面设好"拒绝: ..."提示并丢弃了撤销记录。
        // 必须提前返回：否则下面的 else 分支会把状态覆盖成"已写入 A2"，
        // 用户看到的是写入成功，实际上什么都没写 —— 比直接崩溃更难发现。
        return;
    }
    if (!err.empty()) {
        discardUndo();
        status_ = "公式错误: " + err;
    } else {
        endUndo();
        status_ = "已写入 " + addrToStr(curCol_, curRow_);
    }
    mode_ = Mode::Normal;
    editBuf_.clear();
}

bool GridUI::editCell(int col, int row, const std::string& text, std::string& msg) {
    Sheet* sh = currentSheet();
    if (!sh) { msg = "没有工作表"; return false; }
    curCol_ = col; curRow_ = row;
    collapseSelection();
    scrollIntoView();
    editBuf_ = text;
    commitEdit();
    msg = status_;
    return status_.find("公式错误") == std::string::npos;
}

// ---------------------------------------------------------------------------
// 剪贴板
// ---------------------------------------------------------------------------
void GridUI::copySelection() {
    clip_.clear();
    Sheet* sh = currentSheet();
    if (!sh) return;
    clipW_ = sel_.cols(); clipH_ = sel_.rows();
    for (int r = sel_.r0; r <= sel_.r1; r++)
        for (int c = sel_.c0; c <= sel_.c1; c++) {
            const Sheet::CellRec* rec = sh->find(c, r);
            if (!rec) continue;
            ClipCell cc;
            cc.dc = c - sel_.c0; cc.dr = r - sel_.r0;
            cc.formula = rec->formula; cc.hasFormula = rec->hasFormula; cc.value = rec->value;
            clip_.push_back(cc);
        }
}

bool GridUI::paste(std::string& msg) {
    Sheet* sh = currentSheet();
    if (!sh) { msg = "没有工作表"; return false; }
    if (clip_.empty()) { msg = "剪贴板为空"; return false; }
    beginUndo("粘贴");
    // 目标取选区左上角，与 Excel 一致（粘贴到选区的起点，不是活动单元格）
    for (auto& cc : clip_) {
        int c = sel_.c0 + cc.dc, r = sel_.r0 + cc.dr;
        recordCell(sh, c, r);
        if (cc.hasFormula) sh->setFormula(c, r, cc.formula);
        else               sh->setValue(c, r, cc.value);
    }
    // 编辑后走增量重算：只算受影响的格子。
    // 全量 recalc() 在这里是 O(全部公式)，表一大每敲一个键都要等。
    if (wb_) wb_->recalcDirty(); else sh->recalcDirty();
    endUndo();
    msg = "已粘贴 " + std::to_string(clip_.size()) + " 格";
    return true;
}

// ---------------------------------------------------------------------------
// 多表
// ---------------------------------------------------------------------------
bool GridUI::switchSheet(int idx, std::string& msg) {
    if (!wb_) { msg = "没有工作簿"; return false; }
    if (idx < 0 || idx >= (int)wb_->sheetCount()) { msg = "表号超出范围"; return false; }
    sheetIdx_ = idx;
    curCol_ = std::min(curCol_, wb_->sheet((size_t)idx).maxCol);
    curRow_ = std::min(curRow_, wb_->sheet((size_t)idx).maxRow);
    collapseSelection();
    topRow_ = 0; leftCol_ = 0;
    scrollIntoView();
    msg = "切换到 " + sheetName();
    return true;
}

bool GridUI::newSheet(const std::string& name, std::string& msg) {
    if (!wb_) { msg = "没有工作簿"; return false; }
    std::string n = name.empty() ? ("Sheet" + std::to_string(wb_->sheetCount() + 1)) : name;
    if (wb_->sheetByName(n)) { msg = "同名表已存在: " + n; return false; }
    wb_->addSheet(n);
    refreshTabs();
    sheetIdx_ = (int)wb_->sheetCount() - 1;
    curCol_ = curRow_ = 0;
    collapseSelection();
    dirty_ = true;
    msg = "已新建表 " + n;
    return true;
}

// ---------------------------------------------------------------------------
// 命令
// ---------------------------------------------------------------------------
bool GridUI::cmdSave(const std::string& arg, std::string& msg) {
    if (!wb_) { msg = "没有工作簿"; return false; }
    std::string p = arg.empty() ? path_ : arg;
    if (p.empty()) { msg = "未指定文件名，用法 :w <路径.xlsx>"; return false; }
    std::string err;
    if (!wb_->save(p, err)) { msg = "保存失败: " + err; return false; }
    path_ = p; dirty_ = false;
    msg = "已保存 " + p + "（" + std::to_string(wb_->sheetCount()) + " 张表）";
    return true;
}

bool GridUI::cmdLoad(const std::string& arg, std::string& msg) {
    if (arg.empty()) { msg = "用法 :e <路径.xlsx>"; return false; }
    Workbook wb; std::string err;
    if (!wb.load(arg, err)) { msg = "加载失败: " + err; return false; }
    if (!wb_) { msg = "没有工作簿"; return false; }
    // 整体替换工作簿内容。
    // 注意：这会移动内部持有的 Sheet 对象，调用方此前拿到的 Sheet& 会立刻失效 ——
    // 上层若缓存了表的引用，必须在加载后重新取。
    *wb_ = std::move(wb);
    refreshTabs();
    sheetIdx_ = 0;
    if (wb_->sheetCount() == 0) wb_->addSheet("Sheet1");
    refreshTabs();
    curCol_ = curRow_ = 0;
    collapseSelection();
    path_ = arg; dirty_ = false;
    msg = "已加载 " + arg + "（" + std::to_string(wb_->sheetCount()) + " 张表）";
    return true;
}

bool GridUI::cmdPdf(const std::string& arg, std::string& msg) {
    Sheet* sh = currentSheet();
    if (!sh) { msg = "没有工作表"; return false; }
    std::string p = arg;
    if (p.empty()) { msg = "用法 :pdf <路径.pdf>"; return false; }
    SheetPdfOptions o;
    o.title = sheetName();
    const char* cand[] = {
        "/usr/share/fonts/truetype/alibaba-puhuiti/AlibabaPuHuiTi-2-35-Thin.ttf",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (auto c : cand) { std::ifstream t(c); if (t) { o.fontPath = c; break; } }
    std::string err;
    if (!sheetToPdf(*sh, p, o, err)) { msg = "导出失败: " + err; return false; }
    msg = "已导出 " + p;
    return true;
}

bool GridUI::cmdChart(std::string& msg) {
    Sheet* sh = currentSheet();
    if (!sh) { msg = "没有工作表"; return false; }
    Chart c;
    c.type = ChartType::Column;
    c.title = sheetName() + " 快速图表";
    DataSeries s; s.name = "列 " + colToName(curCol_);
    for (int r = 0; r < 12; r++) {
        Value v = sh->valueAt(curCol_, r);
        s.values.push_back(v.isNum() ? v.n : std::nan(""));
    }
    c.series.push_back(s);
    std::string a = chartToASCII(c);
    size_t nl = a.find('\n');
    msg = "图表已生成（" + std::to_string(c.series[0].values.size()) + " 点）"
        + (nl == std::string::npos ? "" : " " + a.substr(0, nl));
    return true;
}

bool GridUI::fillSelection(bool byColumn, std::string& msg) {
    Sheet* sh = currentSheet();
    if (!sh) { msg = "没有工作表"; return false; }
    // 单行或单列没法填 —— 源和目标重合，没有任何意义
    if (byColumn ? (sel_.cols() < 2) : (sel_.rows() < 2)) {
        msg = (byColumn ? "向右填充需要至少两列" : "向下填充需要至少两行");
        return false;
    }

    // 撤销：先把目标区域的旧内容记下来
    beginUndo(std::string(byColumn ? "向右填充 " : "向下填充 ") + sel_.addr());
    if (byColumn) {
        for (int r = sel_.r0; r <= sel_.r1; r++)
            for (int c = sel_.c0 + 1; c <= sel_.c1; c++) recordCell(sh, c, r);
    } else {
        for (int c = sel_.c0; c <= sel_.c1; c++)
            for (int r = sel_.r0 + 1; r <= sel_.r1; r++) recordCell(sh, c, r);
    }

    FillResult out;
    fillRect(*sh, sel_.c0, sel_.r0, sel_.c1, sel_.r1, byColumn, out);
    if (!out.error.empty()) { discardUndo(); msg = "填充失败: " + out.error; return false; }
    endUndo();

    msg = std::string(byColumn ? "向右填充 " : "向下填充 ") + std::to_string(out.written) + " 格";
    if (out.refErrors) msg += "（" + std::to_string(out.refErrors) + " 处越界变成 #REF!）";
    return true;
}

bool GridUI::applyNumFmt(const std::string& code, std::string& msg) {
    Sheet* sh = currentSheet();
    if (!sh) { msg = "没有工作表"; return false; }

    // 空串 / general / clear 表示清除
    std::string c = code;
    bool clear = c.empty();
    {
        std::string low;
        for (char ch : c) low += (char)std::tolower((unsigned char)ch);
        if (low == "general" || low == "clear") { clear = true; c.clear(); }
    }
    if (!clear) {
        // 先验证格式码能不能解析，别把非法码写进每个格子之后才发现
        NumFmt probe; std::string perr;
        if (!probe.parse(c, perr)) { msg = "格式码无效: " + perr; return false; }
    }

    // 格式码不在 Value 里，CellSnap 存不到它，所以单独走一条"纯格式"撤销记录
    UndoEntry e;
    e.label = std::string(clear ? "清除格式 " : "设置格式 ") + sel_.addr();
    e.sheet = sheetIdx_;
    e.fmtOnly = true;
    e.fmtC0 = sel_.c0; e.fmtR0 = sel_.r0;
    e.fmtCols = sel_.cols(); e.fmtRows = sel_.rows();
    e.fmtAfter = c;
    for (int r = sel_.r0; r <= sel_.r1; r++)
        for (int cc = sel_.c0; cc <= sel_.c1; cc++)
            e.fmtBefore.push_back(sh->numFmtAt(cc, r));

    for (int r = sel_.r0; r <= sel_.r1; r++)
        for (int cc = sel_.c0; cc <= sel_.c1; cc++)
            sh->setNumFmt(cc, r, c);
    // 编辑后走增量重算：只算受影响的格子。
    // 全量 recalc() 在这里是 O(全部公式)，表一大每敲一个键都要等。
    if (wb_) wb_->recalcDirty(); else sh->recalcDirty();

    undoStack_.push_back(e);
    redoStack_.clear();
    dirty_ = true;

    if (clear) { msg = "已清除格式 " + sel_.addr(); return true; }
    msg = "已设置格式 " + c + " 于 " + sel_.addr();
    // 顺手给出当前格子的显示效果，方便立刻确认格式对不对
    std::string shown = sh->display(curCol_, curRow_);
    if (!shown.empty()) msg += "（当前显示: " + shown + "）";
    return true;
}

bool GridUI::applyStyle(const std::string& spec, std::string& msg) {
    Sheet* sh = currentSheet();
    if (!sh) { msg = "没有工作表"; return false; }

    // 以现有样式为基础叠加，这样 ":style bold" 再 ":style center" 能同时生效
    CellStyle st = sh->styleAt(curCol_, curRow_);
    bool any = false, clearAll = false;
    std::string rest = spec;
    std::transform(rest.begin(), rest.end(), rest.begin(), ::tolower);

    std::vector<std::string> toks;
    {
        std::istringstream is(rest);
        std::string t;
        while (is >> t) toks.push_back(t);
    }
    for (const std::string& t : toks) {
        if (t == "clear" || t == "none") { clearAll = true; any = true; continue; }
        if (t == "bold" || t == "b")      { st.bold = true; any = true; continue; }
        if (t == "italic" || t == "i")    { st.italic = true; any = true; continue; }
        if (t == "underline" || t == "u") { st.underline = true; any = true; continue; }
        if (t == "strike")                { st.strike = true; any = true; continue; }
        if (t == "left")   { st.hAlign = HAlign::Left; any = true; continue; }
        if (t == "center") { st.hAlign = HAlign::Center; any = true; continue; }
        if (t == "right")  { st.hAlign = HAlign::Right; any = true; continue; }
        if (t == "top")    { st.vAlign = VAlign::Top; any = true; continue; }
        if (t == "middle") { st.vAlign = VAlign::Center; any = true; continue; }
        if (t == "wrap")   { st.wrapText = true; any = true; continue; }
        if (t == "border") { st.border = BorderStyle::Thin; any = true; continue; }
        // key=value 形式
        size_t eq = t.find('=');
        if (eq != std::string::npos) {
            std::string k = t.substr(0, eq), v = t.substr(eq + 1);
            if (k == "fill" || k == "bg") {
                std::string c = normalizeColor(v);
                if (c.empty()) { msg = "无法识别的颜色: " + v; return false; }
                st.fillColor = c; any = true; continue;
            }
            if (k == "color" || k == "fg") {
                std::string c = normalizeColor(v);
                if (c.empty()) { msg = "无法识别的颜色: " + v; return false; }
                st.fontColor = c; any = true; continue;
            }
            if (k == "size") {
                int n = std::atoi(v.c_str());
                if (n <= 0 || n > 409) { msg = "字号超出范围: " + v; return false; }
                st.fontSize = n; any = true; continue;
            }
            msg = "未知的样式项: " + k; return false;
        }
        msg = "未知的样式: " + t + "（可用: bold italic underline strike left center right top middle wrap border fill=色 color=色 size=N clear）";
        return false;
    }
    if (!any) { msg = "用法: :style bold center fill=FFFF00 size=14"; return false; }
    if (clearAll) st = CellStyle();

    // 与格式码一样：样式不在 Value 里，单独记一条撤销
    UndoEntry e;
    e.label = "设置样式 " + sel_.addr();
    e.sheet = sheetIdx_;
    e.fmtOnly = true;
    e.fmtC0 = sel_.c0; e.fmtR0 = sel_.r0;
    e.fmtCols = sel_.cols(); e.fmtRows = sel_.rows();
    // fmtAfter 存的是格式码，样式走 fmtAfterStyle
    e.fmtAfterStyle = st;
    for (int r = sel_.r0; r <= sel_.r1; r++)
        for (int cc = sel_.c0; cc <= sel_.c1; cc++)
            e.fmtBefore.push_back(sh->numFmtAt(cc, r));
    // 旧样式也要记，撤销时才能逐个还原
    e.fmtBeforeStyle.clear();
    for (int r = sel_.r0; r <= sel_.r1; r++)
        for (int cc = sel_.c0; cc <= sel_.c1; cc++)
            e.fmtBeforeStyle.push_back(sh->styleAt(cc, r));
    e.fmtAfter = sh->numFmtAt(sel_.c0, sel_.r0);
    e.hasStyle = true;

    for (int r = sel_.r0; r <= sel_.r1; r++)
        for (int cc = sel_.c0; cc <= sel_.c1; cc++)
            sh->setStyle(cc, r, st);

    undoStack_.push_back(e);
    redoStack_.clear();
    dirty_ = true;
    msg = "已设置样式 " + spec + " 于 " + sel_.addr();
    return true;
}

bool GridUI::mergeSelection(std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (sel_.cols() < 2 && sel_.rows() < 2) { msg = "合并需要至少两行或两列"; return false; }
    wb->addMerge(sheetIdx_, sel_.c0, sel_.r0, sel_.c1, sel_.r1);
    dirty_ = true;
    msg = "已合并 " + sel_.addr();
    return true;
}

bool GridUI::unmergeSelection(std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    // 取消时按"包含选区任意一格"来匹配，而不是要求选区完全一致 ——
    // 否则用户得精确选中整个区域才能取消，很难操作
    auto rects = wb->allMerges();
    if (sheetIdx_ >= (int)rects.size() || rects[(size_t)sheetIdx_].empty()) {
        msg = "当前表没有合并区域"; return false;
    }
    int n = 0;
    std::vector<std::array<int,4>> keep;
    for (auto& m : rects[(size_t)sheetIdx_]) {
        bool hit = !(m[0] > sel_.c1 || m[2] < sel_.c0 || m[1] > sel_.r1 || m[3] < sel_.r0);
        if (hit) n++; else keep.push_back(m);
    }
    if (n == 0) { msg = "选区内没有合并区域"; return false; }
    wb->clearMerges(sheetIdx_);
    for (auto& m : keep) wb->addMerge(sheetIdx_, m[0], m[1], m[2], m[3]);
    dirty_ = true;
    msg = "已取消 " + std::to_string(n) + " 个合并";
    return true;
}



// 把数字转成可写进 <formula> 的文本。
// 用最短表示（而非固定精度）：0.1 写成 "0.1" 而不是 "0.100000"，
// 否则往返一次公式就变丑，用户再编辑时会困惑。
static std::string numToFormulaText(double d) {
    if (d == (double)(long long)d && std::fabs(d) < 1e15)
        return std::to_string((long long)d);
    std::ostringstream o;
    o << std::setprecision(15) << d;
    return o.str();
}


// 剥掉一对包裹用的引号（err="x" 的 x 还带着引号，显示时会多出两个字符）
static std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// 一个 token 是否是纯数值（用于区分 ":dv whole 1 100" 与 ":dv whole >=0"）
static bool isNumericToken(const std::string& t) {
    if (t.empty()) return false;
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-') i++;
    bool any = false, dot = false;
    for (; i < t.size(); i++) {
        if (t[i] >= '0' && t[i] <= '9') { any = true; continue; }
        if (t[i] == '.' && !dot) { dot = true; continue; }
        return false;
    }
    return any;
}


// 命令参数分词：按空格切分，但引号内的空格不切。
//
// 用户会自然地写 err="请输入 1 到 100"，若按裸空格切开会得到
// 'err="请输入' / '1' / '到' / '100"' 四段，提示文本被拆碎且首尾引号不配对，
// 表现为"命令莫名其妙失败"。所以这里必须做引号感知。
std::vector<std::string> tokenizeSpec(const std::string& spec) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (char c : spec) {
        if (c == '"') { inQuotes = !inQuotes; cur += c; continue; }
        if (!inQuotes && (c == ' ' || c == '\t')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    // 剥离包裹用的引号，只保留内容
    for (auto& t : out) {
        if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
            t = t.substr(1, t.size() - 2);
    }
    return out;
}





// ---------------------------------------------------------------------------
// 定义名称命令
// ---------------------------------------------------------------------------
//   :name 销售额            把当前选区定义成名字（自动带表名与绝对引用）
//   :name 销售额 B2:B5      显式指定区域
//   :name                   列出
//   :name del 销售额        删除
bool GridUI::applyNameCmd(const std::string& body, std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }

    std::vector<std::string> toks = tokenizeSpec(body);

    // 列出
    if (toks.empty()) {
        const auto& dn = wb->definedNames();
        if (dn.empty()) { msg = "当前没有定义名称"; return true; }
        std::string out = "定义名称 " + std::to_string(dn.size()) + " 个:";
        for (const auto& kv : dn) out += " [" + kv.first + " = " + kv.second + "]";
        msg = out;
        return true;
    }
    // 删除
    if (toks[0] == "del" || toks[0] == "remove" || toks[0] == "clear") {
        if (toks.size() < 2) { msg = "用法: :name del 名称"; return false; }
        if (!wb->removeDefinedName(toks[1])) { msg = "没有这个定义名称: " + toks[1]; return false; }
        dirty_ = true;
        msg = "已删除定义名称 " + toks[1];
        return true;
    }

    const std::string& name = toks[0];

    // 解析区域：给了就用给的，没给就用当前选区。
    // 单格选区（起止相同）也能定义 —— Excel 允许名字指向单个单元格。
    int c0 = sel_.c0, r0 = sel_.r0, c1 = sel_.c1, r1 = sel_.r1;
    if (toks.size() >= 2) {
        std::string ref = toks[1];
        size_t colon = ref.find(':');
        std::string a = (colon == std::string::npos) ? ref : ref.substr(0, colon);
        std::string b = (colon == std::string::npos) ? ref : ref.substr(colon + 1);
        int ac, ar, bc, br;
        if (!parseCellRef(a, ac, ar) || !parseCellRef(b, bc, br)) {
            msg = "区域格式不对: " + ref + "（应形如 B2:B5）";
            return false;
        }
        c0 = std::min(ac, bc); r0 = std::min(ar, br);
        c1 = std::max(ac, bc); r1 = std::max(ar, br);
    }

    // 定义文本必须带表名 + 绝对引用。
    // 不带表名的相对引用在别的表的公式里会被解析到错误位置 ——
    // 这会静默算错，比报错更难发现。
    std::string sheetName = wb->sheet(sheetIdx_).name();
    std::string refersTo = sheetName + "!"
                         + "$" + colToName(c0) + "$" + std::to_string(r0 + 1);
    if (c0 != c1 || r0 != r1)
        refersTo += ":$" + colToName(c1) + "$" + std::to_string(r1 + 1);

    std::string why;
    if (!wb->addDefinedName(name, refersTo, why)) { msg = why; return false; }
    dirty_ = true;
    msg = "已定义 " + name + " = " + refersTo;
    return true;
}

// ---------------------------------------------------------------------------
// 嵌入图片命令
// ---------------------------------------------------------------------------
// :img          把当前表的第 1 个图表光栅化成 PNG 嵌入（锚点取当前选区）
// :img list     列出当前表已嵌入的图片
// :img clear    清除当前表的所有图片
//
// 为什么是"从图表生成"而不是"读外部文件"：终端界面没有文件选择器，
// 而引擎自带光栅化能力，把已有图表转成位图再嵌入是最自然的用法 ——
// 得到的 xlsx 里既有可编辑的矢量图表，也有一份位图快照。
bool GridUI::embedChartImage(int chartIndex, std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (wb->chartCount((size_t)sheetIdx_) == 0) { msg = "当前表没有图表可转图片"; return false; }
    if (chartIndex < 0 || chartIndex >= (int)wb->chartCount((size_t)sheetIdx_)) {
        msg = "图表序号超出范围（当前表共 " + std::to_string(wb->chartCount((size_t)sheetIdx_))
            + " 个图表）";
        return false;
    }

    Chart* c = wb->chart((size_t)sheetIdx_, (size_t)chartIndex);
    if (!c) { msg = "取不到图表"; return false; }

    // 直接复用图表光栅化通道，不手工搭画布 —— 同一份渲染逻辑，
    // 保证嵌入的位图与屏幕上看到的图表一致
    std::vector<uint8_t> png;
    if (!chartToPNG(*c, png) || png.empty()) { msg = "图表转 PNG 失败"; return false; }

    ImagePart im;
    im.data = std::move(png);
    im.ext = "png";
    im.name = c->title.empty() ? ("图表 " + std::to_string(chartIndex + 1)) : c->title;
    // 锚点：选区足够大就用选区，否则给一个默认尺寸（8 列 × 12 行）
    if (sel_.c1 > sel_.c0 && sel_.r1 > sel_.r0) {
        im.fromCol = sel_.c0; im.fromRow = sel_.r0;
        im.toCol   = sel_.c1; im.toRow   = sel_.r1;
    } else {
        im.fromCol = curCol_; im.fromRow = curRow_;
        im.toCol   = curCol_ + 7; im.toRow = curRow_ + 11;
    }
    wb->addImage(sheetIdx_, im);
    dirty_ = true;
    msg = "已嵌入图片（" + std::to_string(im.data.size()) + " 字节），锚点 "
        + addrToStr(im.fromCol, im.fromRow) + ":" + addrToStr(im.toCol, im.toRow);
    return true;
}

bool GridUI::listImages(std::string& msg) const {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (sheetIdx_ >= (int)wb->allImages().size() || wb->allImages()[(size_t)sheetIdx_].empty()) {
        msg = "当前表没有嵌入图片"; return true;
    }
    const auto& list = wb->allImages()[(size_t)sheetIdx_];
    std::string out = "嵌入图片 " + std::to_string(list.size()) + " 张:";
    for (const ImagePart& im : list)
        out += " [" + im.name + " " + std::to_string(im.data.size()) + "B "
             + addrToStr(im.fromCol, im.fromRow) + "-" + addrToStr(im.toCol, im.toRow) + "]";
    msg = out;
    return true;
}

// ---------------------------------------------------------------------------
// 视图属性命令
// ---------------------------------------------------------------------------
//   :colw 18        设置当前列宽（字符数）
//   :rowh 28        设置当前行高（磅）
//   :freeze         在当前格上方与左侧冻结（即冻结当前格之前的行列）
//   :freeze clear   取消冻结
//   :filter         对当前选区启用筛选
//   :filter clear   取消筛选
bool GridUI::applyViewCmd(const std::string& body, std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }

    std::vector<std::string> toks = tokenizeSpec(body);
    if (toks.empty()) { msg = "用法: :colw 18 / :rowh 28 / :freeze / :filter"; return false; }
    const std::string& kind = toks[0];

    auto numAt = [&](size_t i, double& out) -> bool {
        if (i >= toks.size()) return false;
        Value v;
        if (!strictNumber(toks[i], v) || !v.isNum()) return false;
        out = v.n;
        return true;
    };

    if (kind == "colw" || kind == "colwidth") {
        double w = 0;
        if (!numAt(1, w) || w <= 0) { msg = "用法: :colw 18"; return false; }
        // 选区是整列时一次设多列；否则只设当前列
        if (sel_.r1 - sel_.r0 >= 20 && sel_.c1 > sel_.c0) {
            for (int c = sel_.c0; c <= sel_.c1; c++) wb->setColWidth(sheetIdx_, c, w);
            msg = "已设置 " + std::to_string(sel_.c1 - sel_.c0 + 1) + " 列宽度 " + toks[1];
        } else {
            wb->setColWidth(sheetIdx_, curCol_, w);
            msg = "已设置列宽 " + toks[1];
        }
        dirty_ = true;
        return true;
    }
    if (kind == "rowh" || kind == "rowheight") {
        double h = 0;
        if (!numAt(1, h) || h <= 0) { msg = "用法: :rowh 28"; return false; }
        wb->setRowHeight(sheetIdx_, curRow_, h);
        dirty_ = true;
        msg = "已设置行高 " + toks[1];
        return true;
    }
    if (kind == "freeze") {
        if (toks.size() > 1 && (toks[1] == "clear" || toks[1] == "off")) {
            wb->setFreeze(sheetIdx_, 0, 0);
            dirty_ = true;
            msg = "已取消冻结";
            return true;
        }
        // 语义：冻结当前格"上方与左侧"的所有行列，与 Excel 的"冻结窗格"一致。
        // 选区为空时用光标位置；光标在 A1 时冻结 0 行 0 列是没有意义的，明确拒绝。
        int c = curCol_, r = curRow_;
        if (c <= 0 && r <= 0) { msg = "光标在 A1，没有可冻结的行列"; return false; }
        wb->setFreeze(sheetIdx_, c, r);
        dirty_ = true;
        msg = "已冻结 " + std::to_string(r) + " 行 " + std::to_string(c) + " 列";
        return true;
    }
    if (kind == "filter" || kind == "autofilter") {
        if (toks.size() > 1 && (toks[1] == "clear" || toks[1] == "off")) {
            wb->clearAutoFilter(sheetIdx_);
            dirty_ = true;
            msg = "已取消筛选";
            return true;
        }
        wb->setAutoFilter(sheetIdx_, sel_.c0, sel_.r0, sel_.c1, sel_.r1);
        dirty_ = true;
        msg = "已对 " + sel_.addr() + " 启用筛选";
        return true;
    }
    msg = "未知的视图命令: " + kind + "（可用 colw/rowh/freeze/filter）";
    return false;
}

// ---------------------------------------------------------------------------
// 批注命令
// ---------------------------------------------------------------------------
// 语法：:note 文本      给当前格加/改批注
//       :note          显示当前格的批注
//       :note clear    删除当前格的批注
bool GridUI::applyNote(const std::string& text, std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (text.empty()) { msg = "用法: :note 批注内容"; return false; }
    CellNote n;
    n.col = curCol_; n.row = curRow_;
    n.author = noteAuthor_.empty() ? std::string("我") : noteAuthor_;
    n.text = text;
    wb->addNote(sheetIdx_, n);
    dirty_ = true;
    msg = "已为 " + addrToStr(curCol_, curRow_) + " 添加批注";
    return true;
}

bool GridUI::clearNote(std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (!wb->removeNote(sheetIdx_, curCol_, curRow_)) {
        msg = addrToStr(curCol_, curRow_) + " 没有批注"; return false;
    }
    dirty_ = true;
    msg = "已删除 " + addrToStr(curCol_, curRow_) + " 的批注";
    return true;
}

bool GridUI::showNote(std::string& msg) const {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    const CellNote* n = wb->noteAt(sheetIdx_, curCol_, curRow_);
    if (!n) { msg = addrToStr(curCol_, curRow_) + " 没有批注"; return true; }
    std::string who = n->author.empty() ? std::string("匿名") : n->author;
    // 多行批注在状态栏里用 ⏎ 表示换行，否则终端会把内容挤成一行看不清
    std::string t = n->text;
    std::string flat;
    for (char c : t) { if (c == '\n') flat += " ⏎ "; else flat += c; }
    msg = who + ": " + flat;
    return true;
}

// ---------------------------------------------------------------------------
// 数据验证命令
// ---------------------------------------------------------------------------
const DataValidation* GridUI::currentDv(int col, int row) const {
    Workbook* wb = wb_;
    if (!wb) return nullptr;
    if (sheetIdx_ >= (int)wb->allDvs().size()) return nullptr;
    return dvForCell(wb->allDvs()[(size_t)sheetIdx_], col, row);
}

// 语法：:dv <类型> <参数...>      :dv 列出      :dv clear 清空
//   :dv whole 1 100         整数在 1..100
//   :dv decimal >=0         小数 >= 0
//   :dv list 是,否,待定      下拉序列
//   :dv len <=5             文本长度不超过 5
//   :dv custom "A1>0"       自定义公式
//   可加 err="提示文本" 或 warn / info
bool GridUI::applyDv(const std::string& spec, std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }

    std::vector<std::string> toks = tokenizeSpec(spec);
    if (toks.empty()) { msg = "用法: :dv whole 1 100 | :dv list 是,否 | :dv len <=5"; return false; }

    DataValidation dv;
    dv.rects.push_back({{sel_.c0, sel_.r0, sel_.c1, sel_.r1}});
    dv.showErrorMessage = true;
    dv.errorStyle = DvErrorStyle::Stop;

    const std::string& kind = toks[0];
    size_t i = 0;

    // 解析可选的运算符前缀（">=" "<=" ">" "<" "=" "<>"）
    auto takeOp = [&](const std::string& tok) -> std::pair<DvOperator, std::string> {
        static const struct { const char* s; DvOperator op; } ops[] = {
            {">=", DvOperator::GreaterThanOrEqual}, {"<=", DvOperator::LessThanOrEqual},
            {"<>", DvOperator::NotEqual}, {"!=", DvOperator::NotEqual},
            {"==", DvOperator::Equal}, {">", DvOperator::GreaterThan},
            {"<", DvOperator::LessThan}, {"=", DvOperator::Equal},
        };
        for (auto& o : ops)
            if (tok.compare(0, std::strlen(o.s), o.s) == 0)
                return {o.op, tok.substr(std::strlen(o.s))};
        return {DvOperator::None, tok};
    };

    if (kind == "whole" || kind == "int" || kind == "integer") {
        dv.type = DvType::Whole;
        // 两种写法：whole 1 100（区间）/ whole >=0（带运算符）
        if (i + 2 < toks.size() && isNumericToken(toks[i + 1]) && isNumericToken(toks[i + 2])) {
            dv.op = DvOperator::Between;
            dv.formula1 = toks[++i]; dv.formula2 = toks[++i];
        } else if (i + 1 < toks.size()) {
            auto pr = takeOp(toks[++i]);
            dv.op = pr.first;
            if (pr.second.empty() && i + 1 < toks.size()) dv.formula1 = toks[++i];
            else dv.formula1 = pr.second;
            if (dv.op == DvOperator::None) { msg = "需要运算符或区间"; return false; }
        } else { msg = "whole 需要数值"; return false; }
    } else if (kind == "decimal" || kind == "num" || kind == "number") {
        dv.type = DvType::Decimal;
        if (i + 2 < toks.size() && isNumericToken(toks[i + 1]) && isNumericToken(toks[i + 2])) {
            dv.op = DvOperator::Between;
            dv.formula1 = toks[++i]; dv.formula2 = toks[++i];
        } else if (i + 1 < toks.size()) {
            auto pr = takeOp(toks[++i]);
            dv.op = pr.first;
            dv.formula1 = pr.second.empty() ? (i + 1 < toks.size() ? toks[++i] : "") : pr.second;
            if (dv.op == DvOperator::None || dv.formula1.empty()) { msg = "decimal 需要运算符与数值"; return false; }
        } else { msg = "decimal 需要数值"; return false; }
    } else if (kind == "list" || kind == "enum") {
        dv.type = DvType::List;
        if (i + 1 >= toks.size()) { msg = "list 需要候选项，如 :dv list 是,否"; return false; }
        std::string items = toks[++i];
        // 允许用空格分隔的多个 token 拼起来（用户可能写 "是 否"）
        while (i + 1 < toks.size() && toks[i + 1].find('=') == std::string::npos
               && toks[i + 1] != "warn" && toks[i + 1] != "info")
            items += "," + toks[++i];
        dv.listItems = splitListFormula(items);
        if (dv.listItems.empty()) { msg = "list 候选项为空"; return false; }
        dv.formula1 = joinListFormula(dv.listItems);
    } else if (kind == "len" || kind == "length" || kind == "textlen") {
        dv.type = DvType::TextLength;
        if (i + 1 >= toks.size()) { msg = "len 需要长度条件"; return false; }
        auto pr = takeOp(toks[++i]);
        dv.op = pr.first;
        dv.formula1 = pr.second.empty() ? (i + 1 < toks.size() ? toks[++i] : "") : pr.second;
        if (dv.op == DvOperator::None || dv.formula1.empty()) { msg = "len 需要运算符与长度"; return false; }
    } else if (kind == "custom" || kind == "expr") {
        dv.type = DvType::Custom;
        if (i + 1 >= toks.size()) { msg = "custom 需要公式"; return false; }
        dv.formula1 = toks[++i];
    } else {
        msg = "未知的验证类型: " + kind + "（可用 whole/decimal/list/len/custom）";
        return false;
    }

    // 剩余 token：err="..." / warn / info
    for (size_t k = i + 1; k < toks.size(); k++) {
        const std::string& t = toks[k];
        if (t == "warn" || t == "warning")      dv.errorStyle = DvErrorStyle::Warning;
        else if (t == "info")                   dv.errorStyle = DvErrorStyle::Information;
        else if (t.compare(0, 4, "err=") == 0)  dv.error = stripQuotes(t.substr(4));
        else if (t.compare(0, 7, "prompt=") == 0) {
            dv.prompt = stripQuotes(t.substr(7)); dv.showInputMessage = true;
        } else { msg = "无法识别的选项: " + t; return false; }
    }
    if (dv.error.empty())
        dv.error = "输入不满足 " + kind + " 规则";

    wb->addDv(sheetIdx_, dv);
    dirty_ = true;
    msg = "已对 " + sel_.addr() + " 设置数据验证: " + kind;
    return true;
}

bool GridUI::clearDv(std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (sheetIdx_ >= (int)wb->allDvs().size() || wb->allDvs()[(size_t)sheetIdx_].empty()) {
        msg = "当前表没有数据验证"; return false;
    }
    size_t n = wb->allDvs()[(size_t)sheetIdx_].size();
    wb->clearDvs(sheetIdx_);
    dirty_ = true;
    msg = "已清除 " + std::to_string(n) + " 条数据验证";
    return true;
}

bool GridUI::listDv(std::string& msg) const {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (sheetIdx_ >= (int)wb->allDvs().size() || wb->allDvs()[(size_t)sheetIdx_].empty()) {
        msg = "当前表没有数据验证"; return true;
    }
    const auto& list = wb->allDvs()[(size_t)sheetIdx_];
    std::string out = "数据验证 " + std::to_string(list.size()) + " 条:";
    for (const auto& dv : list)
        out += " [" + std::string(dvTypeToOoxml(dv.type)) + " " + dv.formula1
             + (dv.formula2.empty() ? "" : "~" + dv.formula2) + "]";
    msg = out;
    return true;
}

// ---------------------------------------------------------------------------
// 条件格式命令
// ---------------------------------------------------------------------------
bool GridUI::applyCf(const std::string& spec, std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }

    // 语法：:cf <判定> [样式...]
    //   判定:  >N  >=N  <N  <=N  =N  <>N  between A B  top N  bottom N
    //          above  below  dup  uniq  expr "公式"
    //   样式:  fill=RRGGBB  color=RRGGBB  bold  italic
    std::vector<std::string> toks;
    {
        std::istringstream is(spec);
        std::string t;
        while (is >> t) toks.push_back(t);
    }
    if (toks.empty()) { msg = "用法: :cf >5 fill=FF0000 | :cf top 3 bold | :cf expr \"A1>0\""; return false; }

    CfRule rule;
    CfStyle st;
    size_t i = 0;
    const std::string& kind = toks[0];

    auto needNum = [&](size_t idx, double& out) -> bool {
        if (idx >= toks.size()) return false;
        Value v;
        if (!strictNumber(toks[idx], v) || !v.isNum()) return false;
        out = v.n;
        return true;
    };

    if (kind == "expr" || kind == "expression") {
        rule.type = CfType::Expression;
        if (i + 1 >= toks.size()) { msg = "expr 需要一条公式"; return false; }
        rule.formulas.push_back(toks[++i]);
    } else if (kind == "between" || kind == "nbetween") {
        rule.type = CfType::CellIs;
        rule.op = (kind == "between") ? CfOperator::Between : CfOperator::NotBetween;
        double a = 0, b = 0;
        if (!needNum(++i, a) || !needNum(++i, b)) { msg = "between 需要两个数值"; return false; }
        rule.formulas.push_back(numToFormulaText(a));
        rule.formulas.push_back(numToFormulaText(b));
    } else if (kind == "top" || kind == "bottom") {
        rule.type = CfType::Top10;
        rule.bottom = (kind == "bottom");
        double n = 10;
        if (!needNum(++i, n) || n < 1) { msg = "top/bottom 需要一个正整数"; return false; }
        rule.rank = (int)n;
    } else if (kind == "above" || kind == "below") {
        rule.type = CfType::AboveAverage;
        rule.above = (kind == "above");
    } else if (kind == "dup" || kind == "uniq") {
        rule.type = (kind == "dup") ? CfType::Duplicate : CfType::Unique;
    } else {
        // 比较运算符：">3" 和 "> 3" 两种写法都要支持。
        // 用户自然写 ">3"，但 shell 式的分词会把它粘成一个 token，
        // 只认独立 token 的话 ":cf >3" 会报"未知的判定"，很难发现原因。
        // 做法：从 token 前缀里截出最长的运算符，剩下的部分就是数值。
        static const struct { const char* s; CfOperator op; } ops[] = {
            {">=", CfOperator::GreaterThanOrEqual},
            {"<=", CfOperator::LessThanOrEqual},
            {"<>", CfOperator::NotEqual},
            {"!=", CfOperator::NotEqual},
            {"==", CfOperator::Equal},
            {">",  CfOperator::GreaterThan},
            {"<",  CfOperator::LessThan},
            {"=",  CfOperator::Equal},
        };
        CfOperator op = CfOperator::None;
        std::string rest;
        for (auto& o : ops) {
            if (kind.compare(0, std::strlen(o.s), o.s) == 0) {
                op = o.op;
                rest = kind.substr(std::strlen(o.s));
                break;
            }
        }
        if (op == CfOperator::None) { msg = "未知的判定: " + kind; return false; }
        rule.type = CfType::CellIs;
        rule.op = op;
        double v = 0;
        if (!rest.empty()) {
            Value vv;
            if (!strictNumber(rest, vv) || !vv.isNum()) {
                msg = "无法识别的数值: " + rest; return false;
            }
            v = vv.n;
        } else if (!needNum(++i, v)) {
            msg = kind + " 需要一个数值"; return false;
        }
        rule.formulas.push_back(numToFormulaText(v));
    }

    // 剩余 token 是样式
    bool any = false;
    for (size_t k = i + 1; k < toks.size(); k++) {
        const std::string& t = toks[k];
        size_t eq = t.find('=');
        if (eq != std::string::npos) {
            std::string key = t.substr(0, eq), val = t.substr(eq + 1);
            if (key == "fill" || key == "bg") {
                std::string c = normalizeColor(val);
                if (c.empty()) { msg = "无法识别的颜色: " + val; return false; }
                st.fillColor = c; any = true;
            } else if (key == "color" || key == "fg") {
                std::string c = normalizeColor(val);
                if (c.empty()) { msg = "无法识别的颜色: " + val; return false; }
                st.fontColor = c; any = true;
            } else { msg = "未知的样式项: " + key; return false; }
        } else if (t == "bold")        { st.bold = true; any = true; }
        else if (t == "italic")        { st.italic = true; any = true; }
        else { msg = "无法识别的样式: " + t; return false; }
    }
    // 没给样式时用红色填充，否则规则生效了也看不出来
    if (!any) st.fillColor = "FF0000";
    rule.style = st;
    rule.priority = 1;

    ConditionalFormat cf;
    cf.rects.push_back({{sel_.c0, sel_.r0, sel_.c1, sel_.r1}});
    cf.rules.push_back(rule);
    wb->addCf(sheetIdx_, cf);
    dirty_ = true;
    msg = "已对 " + sel_.addr() + " 设置条件格式: " + kind;
    return true;
}

bool GridUI::clearCf(std::string& msg) {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (sheetIdx_ >= (int)wb->allCfs().size() || wb->allCfs()[(size_t)sheetIdx_].empty()) {
        msg = "当前表没有条件格式"; return false;
    }
    size_t n = wb->allCfs()[(size_t)sheetIdx_].size();
    wb->clearCfs(sheetIdx_);
    dirty_ = true;
    msg = "已清除 " + std::to_string(n) + " 条条件格式";
    return true;
}

bool GridUI::listCf(std::string& msg) const {
    Workbook* wb = wb_;
    if (!wb) { msg = "没有工作簿"; return false; }
    if (sheetIdx_ >= (int)wb->allCfs().size() || wb->allCfs()[(size_t)sheetIdx_].empty()) {
        msg = "当前表没有条件格式"; return true;
    }
    const auto& list = wb->allCfs()[(size_t)sheetIdx_];
    std::string out = "条件格式 " + std::to_string(list.size()) + " 条:";
    for (const auto& cf : list) {
        for (const auto& rule : cf.rules) {
            out += " [" + std::string(cfTypeToOoxml(rule.type));
            if (rule.type == CfType::CellIs && rule.op != CfOperator::None)
                out += " " + std::string(cfOperatorToOoxml(rule.op));
            for (const std::string& f : rule.formulas) out += " " + f;
            if (!rule.style.fillColor.empty()) out += " fill=" + rule.style.fillColor;
            if (rule.style.bold) out += " bold";
            out += "]";
        }
    }
    msg = out;
    return true;
}

bool GridUI::runCommand(const std::string& cmd, std::string& msg) {
    std::string c = cmd;
    while (!c.empty() && (c.front() == ' ' || c.front() == ':')) c.erase(c.begin());
    std::string body;
    size_t sp = c.find(' ');
    std::string verb = (sp == std::string::npos) ? c : c.substr(0, sp);
    if (sp != std::string::npos) body = c.substr(sp + 1);
    while (!body.empty() && body.front() == ' ') body.erase(body.begin());
    while (!body.empty() && body.back() == ' ') body.pop_back();

    if (verb == "q" || verb == "quit") { quit_ = true; msg = "退出"; return true; }
    if (verb == "w" || verb == "save")  return cmdSave(body, msg);
    if (verb == "e" || verb == "open")  return cmdLoad(body, msg);
    if (verb == "pdf")                  return cmdPdf(body, msg);
    if (verb == "chart")                return cmdChart(msg);
    if (verb == "undo")                 return undo(msg);
    if (verb == "redo")                 return redo(msg);
    if (verb == "copy")                 { copySelection(); msg = "已复制 " + sel_.addr(); return true; }
    if (verb == "fmt" || verb == "format") {
        if (body.empty()) {
            msg = "当前格格式: " + (currentSheet() && !currentSheet()->numFmtAt(curCol_, curRow_).empty()
                    ? currentSheet()->numFmtAt(curCol_, curRow_) : "General")
                + "（用法 :fmt 0.00 / :fmt yyyy-mm-dd / :fmt clear）";
            return true;
        }
        return applyNumFmt(body, msg);
    }
    if (verb == "style") {
        if (body.empty()) {
            msg = "用法: :style bold center fill=FFFF00 size=14 clear";
            return true;
        }
        return applyStyle(body, msg);
    }
    if (verb == "cf") {
        if (body.empty()) return listCf(msg);
        if (body == "clear") return clearCf(msg);
        if (body == "list")  return listCf(msg);
        return applyCf(body, msg);
    }
    if (verb == "name" || verb == "dn" || verb == "definedname")
        return applyNameCmd(body, msg);
    if (verb == "img" || verb == "image" || verb == "pic") {
        if (body == "list" || body == "show") return listImages(msg);
        if (body == "clear") { wb_->clearImages(sheetIdx_); dirty_ = true; msg = "已清除图片"; return true; }
        int idx = 0;
        if (!body.empty()) {
            Value v;
            if (!strictNumber(body, v) || !v.isNum()) { msg = "用法: :img [序号]"; return false; }
            idx = (int)v.n;
        }
        return embedChartImage(idx, msg);
    }
    if (verb == "colw" || verb == "rowh" || verb == "freeze" || verb == "filter")
        return applyViewCmd(body.empty() ? verb : verb + " " + body, msg);
    if (verb == "note" || verb == "comment") {
        if (body.empty()) return showNote(msg);
        if (body == "clear" || body == "del") return clearNote(msg);
        return applyNote(body, msg);
    }
    if (verb == "dv" || verb == "valid") {
        // 注意："list" 既是数据验证的一种类型（下拉序列），
        // 又天然像是"列出规则"的命令。两者撞车时优先当类型处理 ——
        // 因为 ":dv list" 不带参数是残缺的类型命令，会给出明确报错；
        // 而列出规则用 ":dv" 或 ":dv show"，不需要额外记一个词。
        if (body.empty() || body == "show" || body == "ls") return listDv(msg);
        if (body == "clear") return clearDv(msg);
        return applyDv(body, msg);
    }
    if (verb == "merge")   return mergeSelection(msg);
    if (verb == "unmerge") return unmergeSelection(msg);
    if (verb == "fill" || verb == "filldown")  return fillSelection(false, msg);
    if (verb == "fillright")            return fillSelection(true, msg);
    if (verb == "paste")                return paste(msg);
    if (verb == "sheet") {
        if (body.empty()) { msg = "当前表 " + std::to_string(sheetIdx_ + 1) + "/" +
                                  std::to_string(sheetCount()) + " " + sheetName(); return true; }
        // 先按数字找，再按名字找
        if (!body.empty() && body[0] >= '0' && body[0] <= '9') {
            int n = std::atoi(body.c_str()) - 1;
            return switchSheet(n, msg);
        }
        if (wb_) {
            auto names = wb_->sheetNames();
            for (size_t i = 0; i < names.size(); i++)
                if (names[i] == body) return switchSheet((int)i, msg);
        }
        msg = "没有名为 " + body + " 的表"; return false;
    }
    if (verb == "newsheet")             return newSheet(body, msg);
    if (verb == "funcs")                { msg = "函数总数 " + std::to_string(functionNames().size()); return true; }
    if (verb == "goto") {
        int col, row;
        if (cellParseAddr(body, col, row)) {
            curCol_ = col; curRow_ = row; collapseSelection(); scrollIntoView();
            msg = "跳转到 " + addrToStr(col, row); return true;
        }
        msg = "地址无效: " + body; return false;
    }
    msg = "未知命令: " + verb + "（:w :e :pdf :sheet :newsheet :undo :redo :copy :paste :chart :goto :funcs :q）";
    return false;
}

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------
bool GridUI::handleKey(const KeyEvent& ev) {
    // 命令行模式
    if (mode_ == Mode::Command) {
        if (ev.key == Key::Escape) { mode_ = Mode::Normal; cmdBuf_.clear(); status_ = "就绪"; return true; }
        if (ev.key == Key::Enter) {
            std::string msg;
            (void)runCommand(cmdBuf_, msg);
            status_ = msg;
            cmdBuf_.clear();
            if (mode_ == Mode::Command) mode_ = Mode::Normal;
            return !quit_;
        }
        if (ev.key == Key::Backspace) { if (!cmdBuf_.empty()) cmdBuf_.pop_back(); return true; }
        if (ev.key == Key::Char) { encodeCp(cmdBuf_, ev.ch); return true; }
        return true;
    }
    // 编辑模式
    if (mode_ == Mode::Edit) {
        if (ev.key == Key::Escape) { mode_ = Mode::Normal; editBuf_.clear(); status_ = "已取消"; return true; }
        if (ev.key == Key::Enter)  { commitEdit(); return true; }
        if (ev.key == Key::Backspace) {
            if (!editBuf_.empty()) {
                // 按码点删除，不能按字节（UTF-8 会留下半个字）
                std::vector<uint32_t> v = decodeUtf8(editBuf_);
                if (!v.empty()) { v.pop_back(); editBuf_.clear(); for (uint32_t cp : v) encodeCp(editBuf_, cp); }
            }
            return true;
        }
        if (ev.key == Key::Char) { encodeCp(editBuf_, ev.ch); return true; }
        return true;
    }
    // 浏览模式
    Sheet* sh = currentSheet();
    switch (ev.key) {
        case Key::Up:    moveCursor(0, -1, ev.shift); break;
        case Key::Down:  moveCursor(0,  1, ev.shift); break;
        case Key::Left:  moveCursor(-1, 0, ev.shift); break;
        case Key::Right: moveCursor( 1, 0, ev.shift); break;
        case Key::PageUp:   moveCursor(0, -visibleRows(), ev.shift); break;
        case Key::PageDown: moveCursor(0,  visibleRows(), ev.shift); break;
        case Key::Home:  moveCursor(-curCol_, 0, ev.shift); break;
        case Key::Enter: beginEdit(true); break;
        case Key::F2:    beginEdit(true); break;
        case Key::SheetPrev: {
            std::string m; switchSheet(sheetIdx_ - 1, m); status_ = m; break;
        }
        case Key::SheetNext: {
            std::string m; switchSheet(sheetIdx_ + 1, m); status_ = m; break;
        }
        case Key::Delete: {
            if (!sh) break;
            beginUndo("清空 " + sel_.addr());
            for (int r = sel_.r0; r <= sel_.r1; r++)
                for (int c = sel_.c0; c <= sel_.c1; c++) {
                    recordCell(sh, c, r);
                    sh->eraseCell(c, r);
                }
            // 编辑后走增量重算：只算受影响的格子。
    // 全量 recalc() 在这里是 O(全部公式)，表一大每敲一个键都要等。
    if (wb_) wb_->recalcDirty(); else sh->recalcDirty();
            endUndo();
            status_ = "已清空 " + sel_.addr();
            break;
        }
        case Key::Char: {
            if (ev.ctrl && (ev.ch == 'z' || ev.ch == 'Z')) { std::string m; undo(m); status_ = m; break; }
            if (ev.ctrl && (ev.ch == 'y' || ev.ch == 'Y')) { std::string m; redo(m); status_ = m; break; }
            if (ev.ctrl && (ev.ch == 'c' || ev.ch == 'C')) { copySelection(); status_ = "已复制 " + sel_.addr(); break; }
            if (ev.ctrl && (ev.ch == 'v' || ev.ch == 'V')) { std::string m; paste(m); status_ = m; break; }
            if (ev.ctrl && (ev.ch == 'd' || ev.ch == 'D')) { std::string m; fillSelection(false, m); status_ = m; break; }
            if (ev.ctrl && (ev.ch == 'r' || ev.ch == 'R')) { std::string m; fillSelection(true, m); status_ = m; break; }
            if (ev.ctrl) break;                       // 其余组合键忽略
            if (ev.ch == ':') { mode_ = Mode::Command; cmdBuf_.clear(); status_ = ":"; }
            else if (ev.ch >= 0x20 && ev.ch != 0x7F) { beginEdit(false); editBuf_.clear(); encodeCp(editBuf_, ev.ch); }
            break;
        }
        default: break;
    }
    return true;
}


// ---------------------------------------------------------------------------
// 条件格式着色
// ---------------------------------------------------------------------------
namespace {

// 终端只有 8 种基本色，而单元格样式是 24 位 RGB。
// 这里按"亮度 + 主色分量"挑最接近的那个 —— 不追求准确，
// 只求"红底黄底的格子一眼能分辨"，这正是条件格式在终端里唯一的作用。
//
// 具体规则：先按亮度分到黑/白/灰，否则取 RGB 里最大的分量对应的基本色；
// 三个分量接近（低饱和度）时按亮度落到灰或白。
TuiColor nearestTuiColor(const std::string& rgb) {
    if (rgb.size() != 6) return TuiColor::Default;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    int r = hex(rgb[0]) * 16 + hex(rgb[1]);
    int g = hex(rgb[2]) * 16 + hex(rgb[3]);
    int b = hex(rgb[4]) * 16 + hex(rgb[5]);
    int mx = std::max({r, g, b});
    int mn = std::min({r, g, b});
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    if (mx - mn <= 30) {                       // 低饱和度：按亮度分灰阶
        if (lum < 60)  return TuiColor::Black;
        if (lum > 200) return TuiColor::White;
        return TuiColor::Gray;
    }
    if (mx == r) return (g > 100 && b < 100) ? TuiColor::Yellow : TuiColor::Red;
    if (mx == g) return (b > 100) ? TuiColor::Cyan : TuiColor::Green;
    return (r > 100) ? TuiColor::Magenta : TuiColor::Blue;
}

} // namespace

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------
void GridUI::render(Screen& s) const {
    s.clear();
    Attr titleA{TuiColor::White, TuiColor::Blue, true, false};
    Attr headA {TuiColor::Black, TuiColor::Gray, false, false};
    Attr cellA {TuiColor::Default, TuiColor::Default, false, false};
    Attr numA  {TuiColor::Cyan,  TuiColor::Default, false, false};
    Attr errA  {TuiColor::Red,   TuiColor::Default, false, false};
    Attr curA  {TuiColor::Black, TuiColor::Cyan, false, true};
    Attr selA  {TuiColor::Black, TuiColor::White, false, false};

    Sheet* sh = const_cast<GridUI*>(this)->currentSheet();

    // ---- 第 0 行：标题栏 ----
    std::string left = "xlengine  " + sheetName();
    if (dirty_) left += " *";
    s.fillRow(0, left, titleA);
    {
        std::string right = sh ? (std::to_string(sh->cellCount()) + " 格") : "无表";
        int x = s.width() - displayWidth(right);
        if (x > displayWidth(left) + 1) s.putStr(x, 0, right, titleA);
    }

    // ---- 第 1 行：公式栏 ----
    {
        std::string addr = sel_.addr();
        std::string content;
        if (mode_ == Mode::Edit) content = editBuf_;
        else if (mode_ == Mode::Command) content = ":" + cmdBuf_;
        else content = currentContent();
        std::string line = addr + " │ " + content;
        s.fillRow(1, fitToWidth(line, s.width()), {TuiColor::Yellow, TuiColor::Default, true, false});
    }

    // ---- 第 2 行：列标 ----
    {
        s.fillCell(0, 2, kRowHeadW, "", headA);
        int vc = visibleCols();
        for (int i = 0; i < vc; i++) {
            int col = leftCol_ + i;
            std::string nm = colToName(col);
            std::string txt = padLeftTo(nm, kColW - 1);
            int x = kRowHeadW + i * kColW;
            if (x >= s.width()) break;
            bool inSel = sel_.contains(col, 0) || (col >= sel_.c0 && col <= sel_.c1);
            Attr a = inSel ? selA : headA;
            s.fillCell(x, 2, kColW, txt, a);
        }
    }

    // ---- 条件格式：整表判定一次，渲染时按格子取用 ----
    //
    // 放在渲染循环外算，而不是每格算一次：Top10 / AboveAverage / Duplicate
    // 这几类要看整个区域的取值，逐格调用会把同一区域重复统计 O(n) 次。
    std::map<std::pair<int,int>, CfStyle> cfHits;
    if (sh && wb_ && sheetIdx_ < (int)wb_->allCfs().size())
        resolveCfStyles(*sh, wb_->allCfs()[(size_t)sheetIdx_], cfHits);

    // ---- 网格 ----
    int vr = visibleRows();
    for (int i = 0; i < vr; i++) {
        int row = topRow_ + i;
        int y = 3 + i;
        if (y >= s.height() - 3) break;
        bool rowInSel = sel_.contains(0, row) || (row >= sel_.r0 && row <= sel_.r1);
        Attr rh = rowInSel ? selA : headA;
        s.fillCell(0, y, kRowHeadW, padLeftTo(std::to_string(row + 1), kRowHeadW - 1), rh);

        int vc = visibleCols();
        for (int j = 0; j < vc; j++) {
            int col = leftCol_ + j;
            int x = kRowHeadW + j * kColW;
            if (x >= s.width()) break;
            bool isCur = (col == curCol_ && row == curRow_);
            bool inSel = sel_.contains(col, row);
            std::string txt;
            Attr a = inSel ? selA : cellA;
            if (sh && sh->hasCell(col, row)) {
                const Sheet::CellRec* r = sh->find(col, row);
                const Value& v = r->value;
                if (v.isError()) { txt = valueToText(v); a = errA; }
                else if (v.isNum()) { txt = sh->display(col, row); if (!inSel && !isCur) a = numA; }
                else { txt = sh->display(col, row); }
                if (r->hasFormula && !isCur) a.bold = true;
                // 条件格式叠加：填充色改背景，字体色改前景，粗斜体叠加
                if (!isCur && !inSel) {
                    auto cfit = cfHits.find({col, row});
                    if (cfit != cfHits.end()) {
                        const CfStyle& cs = cfit->second;
                        if (!cs.fillColor.empty()) a.bg = nearestTuiColor(cs.fillColor);
                        if (!cs.fontColor.empty()) a.fg = nearestTuiColor(cs.fontColor);
                        if (cs.bold)   a.bold = true;
                        if (cs.italic) a.reverse = true;   // 终端无斜体，用反显近似
                    }
                }
                // 批注标记：终端没有红色三角，用一个角标字符补位。
                // 它要塞进列宽里，所以先按"已占 1 列"裁切内容再追加，
                // 否则标记会把最后一个字符顶出去，看上去像内容被截断。
                bool hasNote = (wb_ && sheetIdx_ < (int)wb_->allNotes().size())
                               && wb_->noteAt(sheetIdx_, col, row) != nullptr;
                if (hasNote) txt = fitToWidth(txt, kColW - 2);
                else          txt = fitToWidth(txt, kColW - 1);
                if (hasNote) txt += "\u25c2";               // ◆ 左指三角
                if (v.isNum()) txt = padLeftTo(txt, kColW - 1);
            }
            if (isCur) a = (mode_ == Mode::Edit)
                         ? Attr{TuiColor::Black, TuiColor::Yellow, true, false}
                         : curA;
            s.fillCell(x, y, kColW, txt, a);
        }
    }

    // ---- 表签 ----
    {
        int y = s.height() - 3;
        std::string tabs;
        for (size_t i = 0; i < tabNames_.size(); i++) {
            if (i) tabs += " ";
            tabs += (i == (size_t)sheetIdx_ ? "[" : " ");
            tabs += tabNames_[i];
            tabs += (i == (size_t)sheetIdx_ ? "]" : " ");
        }
        if (tabs.empty()) tabs = "[Sheet1]";
        if (displayWidth(tabs) > s.width()) tabs = fitToWidth(tabs, s.width());
        Attr tabA{TuiColor::Black, TuiColor::Gray, false, false};
        s.fillRow(y, tabs, tabA);
        // 当前表签高亮
        {
            int x = 0;
            for (size_t i = 0; i < tabNames_.size(); i++) {
                if (i) x += 1;
                if (i == (size_t)sheetIdx_) {
                    std::string nm = "[" + tabNames_[i] + "]";
                    for (size_t k = 0; k < nm.size() && x + (int)k < s.width(); k++)
                        s.put(x + (int)k, y, (char32_t)(unsigned char)nm[k],
                              Attr{TuiColor::White, TuiColor::Blue, true, false});
                }
                x += displayWidth(tabNames_[i]) + 2;
            }
        }
    }

    // ---- 状态 ----
    s.fillRow(s.height() - 2, fitToWidth(status_, s.width()),
              {TuiColor::White, TuiColor::Default, false, true});

    // ---- 按键提示 ----
    {
        std::string hint = "↑↓←→ 移动 │ Shift+方向 选区 │ Enter 编辑 │ ^D 向下填充 │ ^R 向右填充 │ Del 清空 │ ^Z/^Y │ ^C/^V │ ^PgUp 切表 │ : 命令";
        s.fillRow(s.height() - 1, fitToWidth(hint, s.width()), headA);
    }
}
// Terminal
// ---------------------------------------------------------------------------
bool Terminal::enterRaw() {
#if defined(XL_HAVE_WINCON)
    // 三件事缺一不可：
    //   SetConsoleCP/OutputCP(65001)  —— 否则中文全是乱码
    //   ENABLE_VIRTUAL_TERMINAL_PROCESSING —— 否则 ANSI 颜色序列被当成字面文本
    //   ENABLE_VIRTUAL_TERMINAL_INPUT —— 让方向键以 ESC[A 形式送来，
    //        这样现有的 parseKeys 可以原样复用，不用再写一套扫描码映射
    hIn_  = (unsigned long)(uintptr_t)GetStdHandle(STD_INPUT_HANDLE);
    hOut_ = (unsigned long)(uintptr_t)GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hIn_ || hIn_ == (unsigned long)(uintptr_t)INVALID_HANDLE_VALUE) return false;
    if (!hOut_ || hOut_ == (unsigned long)(uintptr_t)INVALID_HANDLE_VALUE) return false;

    DWORD mode = 0;
    if (!GetConsoleMode((HANDLE)(uintptr_t)hIn_, &mode)) return false;   // 不是控制台（重定向到文件）
    origMode_ = (long long)mode;

    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    DWORD inMode = mode;
    inMode &= ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    inMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode((HANDLE)(uintptr_t)hIn_, inMode);

    DWORD outMode = 0;
    if (GetConsoleMode((HANDLE)(uintptr_t)hOut_, &outMode))
        SetConsoleMode((HANDLE)(uintptr_t)hOut_,
                       outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                                | ENABLE_PROCESSED_OUTPUT);
    raw_ = true;
    return true;
#elif defined(XL_HAVE_TERMIOS)
    if (!isatty(STDIN_FILENO)) return false;
    if (getenv("TERM") && std::string(getenv("TERM")) == "dumb") return false;
    auto* t = new termios();
    if (tcgetattr(STDIN_FILENO, t) != 0) { delete t; return false; }
    orig_ = t;
    termios raw = *t;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) { delete t; orig_ = nullptr; return false; }
    raw_ = true;
    return true;
#else
    return false;
#endif
}

void Terminal::leaveRaw() {
#if defined(XL_HAVE_WINCON)
    // 必须恢复控制台模式，否则退出后终端还是原始模式，
    // 用户的 shell 会"看起来坏了"（不回显、不换行处理）
    if (raw_) {
        if (hIn_) SetConsoleMode((HANDLE)(uintptr_t)hIn_, (DWORD)origMode_);
        raw_ = false;
    }
    hIn_ = hOut_ = 0;
    origMode_ = 0;
#elif defined(XL_HAVE_TERMIOS)
    if (raw_ && orig_) { tcsetattr(STDIN_FILENO, TCSANOW, (termios*)orig_); raw_ = false; }
    delete (termios*)orig_; orig_ = nullptr;
#endif
}

bool Terminal::size(int& w, int& h) {
#if defined(XL_HAVE_WINCON)
    unsigned long ho = hOut_ ? hOut_ : (unsigned long)(uintptr_t)GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO ci{};
    // 用窗口矩形而不是缓冲区大小：缓冲区可能比可见窗口高很多
    if (GetConsoleScreenBufferInfo((HANDLE)(uintptr_t)ho, &ci)) {
        int cw = ci.srWindow.Right - ci.srWindow.Left + 1;
        int chh = ci.srWindow.Bottom - ci.srWindow.Top + 1;
        if (cw > 0 && chh > 0) { w = cw; h = chh; return true; }
    }
#elif defined(XL_HAVE_TERMIOS)
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        w = ws.ws_col; h = ws.ws_row; return true;
    }
#endif
    const char* c = getenv("COLUMNS"); const char* r = getenv("LINES");
    if (c && r) { w = atoi(c); h = atoi(r); return w > 0 && h > 0; }
    return false;
}

std::string Terminal::readSome() {
#if defined(XL_HAVE_WINCON)
    if (!hIn_) return std::string();
    DWORD avail = 0;
    // 先看有没有待读事件：没有就直接返回，不阻塞。
    // 这里不能用 ReadConsoleInput 空等 —— 主循环还要定时刷新界面
    if (!GetNumberOfConsoleInputEvents((HANDLE)(uintptr_t)hIn_, &avail) || avail == 0)
        return std::string();

    std::string out;
    DWORD nRead = 0;
    // 逐个取事件。一次可能攒了多个（粘贴、连击），全取回来交给 parseKeys
    std::vector<INPUT_RECORD> recs(avail > 128 ? 128 : avail);
    if (!PeekConsoleInputA((HANDLE)(uintptr_t)hIn_, recs.data(), (DWORD)recs.size(), &nRead))
        return std::string();
    // 只取走键事件；鼠标/尺寸事件留在队列里会让后续 Peek 永远卡住同一批，
    // 所以用 ReadConsoleInput 把它们消费掉
    for (DWORD i = 0; i < nRead; i++) {
        INPUT_RECORD ir{};
        DWORD got = 0;
        if (!ReadConsoleInputA((HANDLE)(uintptr_t)hIn_, &ir, 1, &got) || got == 0) break;
        if (ir.EventType != KEY_EVENT) continue;        // 只处理键事件
        const KEY_EVENT_RECORD& k = ir.Event.KeyEvent;
        if (!k.bKeyDown) continue;                 // 只处理按下，忽略抬起
        char c = (char)(unsigned char)k.uChar.AsciiChar;
        if (c == 0) continue;                      // 功能键扩展位（已由 VT input 转成 ANSI）
        out.push_back(c);
        // 多字节 UTF-8 的后续字节：AsciiChar 只能放一个字节，
        // 中文在 VT input 下会拆成多次事件，逐个追加即可拼回完整序列
    }
    return out;
#elif defined(XL_HAVE_TERMIOS)
    char buf[256];
    fd_set rd; FD_ZERO(&rd); FD_SET(STDIN_FILENO, &rd);
    timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 200000;
    if (select(STDIN_FILENO + 1, &rd, nullptr, nullptr, &tv) > 0) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) return std::string(buf, (size_t)n);
    }
#endif
    return std::string();
}

void Terminal::write(const std::string& s) { std::cout << s; std::cout.flush(); }
void Terminal::clearScreen()   { write("\x1b[2J\x1b[H"); }
void Terminal::hideCursor()    { write("\x1b[?25l"); }
void Terminal::showCursor()    { write("\x1b[?25h"); }

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------
int runTui(Workbook& wb, const std::string& path, bool isInteractive) {
    GridUI ui;
    ui.attach(&wb, path);

    if (!isInteractive) {
        // 无终端：渲染一帧纯文本，便于查看与测试
        int w = 100, h = 24;
        ui.resize(w, h);
        Screen s(w, h);
        ui.render(s);
        std::cout << s.toPlainText() << "\n";
        return 0;
    }

    Terminal term;
    if (!term.enterRaw()) {
        int w = 100, h = 24;
        term.size(w, h);
        ui.resize(w, h);
        Screen s(w, h);
        ui.render(s);
        std::cout << s.toPlainText() << "\n";
        std::cout << "\n（当前不是交互终端，已输出静态快照）\n";
        return 0;
    }
    term.hideCursor();
    int w = 80, h = 24;
    term.size(w, h);
    ui.resize(w, h);

    bool running = true;
    int lastW = w, lastH = h;
    while (running) {
        int cw, ch;
        if (term.size(cw, ch) && (cw != lastW || ch != lastH)) { ui.resize(cw, ch); lastW = cw; lastH = ch; }
        Screen s(lastW, lastH);
        ui.render(s);
        term.write("\x1b[H" + s.toAnsi());
        std::string in = term.readSome();
        if (in.empty()) continue;
        for (auto& ev : parseKeys(in)) {
            if (!ui.handleKey(ev)) { running = false; break; }
        }
        if (ui.quitRequested()) running = false;
    }
    term.showCursor();
    term.leaveRaw();
    std::cout << "\n";
    return 0;
}

} // namespace xl
