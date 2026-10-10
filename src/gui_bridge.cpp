// ---------------------------------------------------------------------------
// 图形界面用的 C 桥接层
// ---------------------------------------------------------------------------
// 把 C++ 引擎暴露成一组 extern "C" 函数，供 Python（ctypes）调用。
//
// 为什么是 C 而不是直接做 Python 扩展：
//   - ctypes 不需要编译绑定代码，不引入 pybind11 依赖
//   - 同一份 .so 也能被别的语言/界面复用
//
// 约定：
//   - col / row 一律从 0 开始（与引擎内部一致）
//   - 返回 char* 的函数用 malloc 分配，调用方必须用 xl_str_free 释放
//   - 返回 int 的函数：0 表示成功，非 0 表示失败（错误文本用 xl_last_error 取）
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "sheet.hpp"
#include "xlsx.hpp"
#include "chart.hpp"
#include "cf.hpp"
#include "dv.hpp"
#include "note.hpp"
#include "sheetpdf.hpp"
#include "style.hpp"
#include "value.hpp"
#include "functions.hpp"
#include "refshift.hpp"

namespace {

std::string g_lastError;

char* dupStr(const std::string& s) {
    char* p = (char*)std::malloc(s.size() + 1);
    if (!p) return nullptr;
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

// 坐标合法性检查。UI 里滚动位置可能算出越界值，静默夹取会指向错误的格子，
// 所以这里明确失败并给出原因。
bool checkCell(xl::Workbook* wb, int sheet, int col, int row) {
    if (!wb) { g_lastError = "工作簿为空"; return false; }
    if (sheet < 0 || sheet >= (int)wb->sheetCount()) { g_lastError = "表索引越界"; return false; }
    xl::Sheet& sh = wb->sheet((size_t)sheet);
    if (col < 0 || col > sh.maxCol || row < 0 || row > sh.maxRow) {
        g_lastError = "单元格坐标越界";
        return false;
    }
    return true;
}

xl::Sheet* sheetAt(xl::Workbook* wb, int sheet) {
    if (!wb || sheet < 0 || sheet >= (int)wb->sheetCount()) return nullptr;
    return &wb->sheet((size_t)sheet);
}

}  // namespace

extern "C" {

// ---- 工作簿 ----
void* xl_wb_new(void) { return new xl::Workbook(); }

void xl_wb_free(void* wb) { delete (xl::Workbook*)wb; }

int xl_wb_save(void* wb, const char* path) {
    if (!wb) { g_lastError = "工作簿为空"; return 1; }
    std::string err;
    if (!((xl::Workbook*)wb)->save(path ? path : "", err)) {
        g_lastError = err.empty() ? "保存失败" : err;
        return 1;
    }
    //
    // 成功时必须清空错误。
    // 不清的话上一次失败调用的错误会一直留着，调用方"保存完顺手看一眼
    // last_error"就会读到一个和自己无关的旧错误 —— 表现为"明明存盘成功了
    // 却报了个错"。测试里就是这么误判的。
    //
    g_lastError.clear();
    return 0;
}

int xl_wb_load(void* wb, const char* path) {
    if (!wb) { g_lastError = "工作簿为空"; return 1; }
    std::string err;
    if (!((xl::Workbook*)wb)->load(path ? path : "", err)) {
        g_lastError = err.empty() ? "打开失败" : err;
        return 1;
    }
    return 0;
}

int xl_wb_sheet_count(void* wb) {
    return wb ? (int)((xl::Workbook*)wb)->sheetCount() : 0;
}

char* xl_wb_sheet_name(void* wb, int i) {
    if (!wb || i < 0 || i >= (int)((xl::Workbook*)wb)->sheetCount()) return dupStr("");
    return dupStr(((xl::Workbook*)wb)->sheet((size_t)i).name());
}

int xl_wb_add_sheet(void* wb, const char* name) {
    if (!wb) { g_lastError = "工作簿为空"; return -1; }
    ((xl::Workbook*)wb)->addSheet(name ? name : "");
    return (int)((xl::Workbook*)wb)->sheetCount() - 1;
}

const char* xl_last_error(void) { return g_lastError.c_str(); }

void xl_str_free(char* p) { if (p) std::free(p); }

// ---- 单元格写入 ----
int xl_set_num(void* wb, int sheet, int col, int row, double v) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    sheetAt((xl::Workbook*)wb, sheet)->setValue(col, row, xl::Value::num(v));
    return 0;
}

int xl_set_str(void* wb, int sheet, int col, int row, const char* s) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    sheetAt((xl::Workbook*)wb, sheet)->setValue(col, row, xl::Value::str(s ? s : ""));
    return 0;
}

int xl_set_bool(void* wb, int sheet, int col, int row, int b) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    sheetAt((xl::Workbook*)wb, sheet)->setValue(col, row, xl::Value::boolean(b != 0));
    return 0;
}

int xl_set_formula(void* wb, int sheet, int col, int row, const char* f) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    std::string err = sheetAt((xl::Workbook*)wb, sheet)->setFormula(col, row, f ? f : "");
    if (!err.empty()) { g_lastError = err; return 1; }
    return 0;
}

int xl_erase(void* wb, int sheet, int col, int row) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    sheetAt((xl::Workbook*)wb, sheet)->eraseCell(col, row);
    return 0;
}

// ---- 重算 ----
void xl_recalc(void* wb) {
    if (!wb) return;
    ((xl::Workbook*)wb)->recalcDirty();
}

// ---- 读取 ----
char* xl_display(void* wb, int sheet, int col, int row) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return dupStr("");
    return dupStr(sheetAt((xl::Workbook*)wb, sheet)->display(col, row));
}

char* xl_formula(void* wb, int sheet, int col, int row) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return dupStr("");
    std::string f;
    if (!sheetAt((xl::Workbook*)wb, sheet)->cellFormula(col, row, f)) return dupStr("");
    return dupStr(f);
}

// 0=空 1=数字 2=文本 3=布尔 4=错误 5=其它
int xl_value_type(void* wb, int sheet, int col, int row) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 0;
    xl::Value v = sheetAt((xl::Workbook*)wb, sheet)->valueAt(col, row);
    switch (v.t) {
        case xl::Value::T::Empty: return 0;
        case xl::Value::T::Num:   return 1;
        case xl::Value::T::Str:   return 2;
        case xl::Value::T::Bool:  return 3;
        case xl::Value::T::Error: return 4;
        default: return 5;
    }
}

double xl_value_num(void* wb, int sheet, int col, int row) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 0.0;
    return sheetAt((xl::Workbook*)wb, sheet)->valueAt(col, row).n;
}

int xl_has_cell(void* wb, int sheet, int col, int row) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 0;
    return sheetAt((xl::Workbook*)wb, sheet)->hasCell(col, row) ? 1 : 0;
}

// 已用区域（UI 用来决定画多少行多少列）。返回 0 成功，写入 4 个整数。
int xl_used_range(void* wb, int sheet, int* c0, int* r0, int* c1, int* r1) {
    xl::Sheet* sh = sheetAt((xl::Workbook*)wb, sheet);
    if (!sh) { g_lastError = "表索引越界"; return 1; }
    int a = 0, b = 0, c = 0, d = 0;
    sh->usedRange(sh->name(), a, b, c, d);
    if (c0) *c0 = a;
    if (r0) *r0 = b;
    if (c1) *c1 = c;
    if (r1) *r1 = d;
    return 0;
}

// ---- 数字格式 ----
// 读回数字格式码 —— 撤销格式操作要能还原，所以必须可读
char* xl_get_numfmt(void* wb, int sheet, int col, int row) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) return dupStr("");
    return dupStr(w->sheet((size_t)sheet).numFmtAt(col, row));
}

int xl_set_numfmt(void* wb, int sheet, int col, int row, const char* code) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    sheetAt((xl::Workbook*)wb, sheet)->setNumFmt(col, row, code ? code : "");
    return 0;
}

// ---- 样式 ----
// 单格
int xl_style_font(void* wb, int sheet, int col, int row,
                  int bold, int italic, int underline, int size, const char* color) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    xl::Sheet* sh = sheetAt((xl::Workbook*)wb, sheet);
    xl::CellStyle st = sh->styleAt(col, row);
    st.bold = bold != 0;
    st.italic = italic != 0;
    st.underline = underline != 0;
    st.fontSize = size;
    st.fontColor = color ? color : "";
    sh->setStyle(col, row, st);
    return 0;
}

int xl_style_fill(void* wb, int sheet, int col, int row, const char* color) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    xl::Sheet* sh = sheetAt((xl::Workbook*)wb, sheet);
    xl::CellStyle st = sh->styleAt(col, row);
    st.fillColor = color ? color : "";
    sh->setStyle(col, row, st);
    return 0;
}

int xl_style_align(void* wb, int sheet, int col, int row, int halign, int valign) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    xl::Sheet* sh = sheetAt((xl::Workbook*)wb, sheet);
    xl::CellStyle st = sh->styleAt(col, row);
    st.hAlign = (xl::HAlign)halign;
    st.vAlign = (xl::VAlign)valign;
    sh->setStyle(col, row, st);
    return 0;
}

int xl_style_border(void* wb, int sheet, int col, int row, int style, const char* color) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    xl::Sheet* sh = sheetAt((xl::Workbook*)wb, sheet);
    xl::CellStyle st = sh->styleAt(col, row);
    st.border = (xl::BorderStyle)style;
    st.borderColor = color ? color : "";
    sh->setStyle(col, row, st);
    return 0;
}

// 区域样式：逐格复制同一份设置。
// 注意必须逐格 setStyle —— 引擎的样式是按格存的稀疏 map，没有区域概念。
int xl_style_range(void* wb, int sheet, int c0, int r0, int c1, int r1,
                   int bold, int italic, int size, const char* fontColor,
                   const char* fillColor, int halign, int valign,
                   int border, const char* borderColor) {
    xl::Sheet* sh = sheetAt((xl::Workbook*)wb, sheet);
    if (!sh) { g_lastError = "表索引越界"; return 1; }
    if (c0 > c1) { int t = c0; c0 = c1; c1 = t; }
    if (r0 > r1) { int t = r0; r0 = r1; r1 = t; }
    // 大区域要截断：10 万格 × 样式对象会把界面卡死
    if ((long long)(c1 - c0 + 1) * (r1 - r0 + 1) > 200000LL) {
        g_lastError = "区域过大（超过 20 万格）";
        return 1;
    }
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            if (c < 0 || c > sh->maxCol || r < 0 || r > sh->maxRow) continue;
            xl::CellStyle st = sh->styleAt(c, r);
            st.bold = bold != 0;
            st.italic = italic != 0;
            if (size > 0) st.fontSize = size;
            if (fontColor) st.fontColor = fontColor;
            if (fillColor) st.fillColor = fillColor;
            if (halign >= 0) st.hAlign = (xl::HAlign)halign;
            if (valign >= 0) st.vAlign = (xl::VAlign)valign;
            if (border >= 0) st.border = (xl::BorderStyle)border;
            if (borderColor) st.borderColor = borderColor;
            sh->setStyle(c, r, st);
        }
    }
    return 0;
}

// 读取样式（UI 渲染用）。返回 0 成功。
int xl_get_style(void* wb, int sheet, int col, int row,
                 int* bold, int* italic, int* size,
                 char** fontColor, char** fillColor,
                 int* halign, int* valign, int* border) {
    if (!checkCell((xl::Workbook*)wb, sheet, col, row)) return 1;
    xl::CellStyle st = sheetAt((xl::Workbook*)wb, sheet)->styleAt(col, row);
    if (bold)   *bold = st.bold ? 1 : 0;
    if (italic) *italic = st.italic ? 1 : 0;
    if (size)   *size = st.fontSize;
    if (fontColor) *fontColor = dupStr(st.fontColor);
    if (fillColor) *fillColor = dupStr(st.fillColor);
    if (halign) *halign = (int)st.hAlign;
    if (valign) *valign = (int)st.vAlign;
    if (border) *border = (int)st.border;
    return 0;
}

// ---- 列宽 / 行高 / 合并 ----
// 列宽 / 行高不在 Sheet 里，而在工作簿的 SheetLayout 上
// （OOXML 里 cols 是 sheetData 之前的独立元素，row 的 ht 是行属性）。
int xl_set_col_width(void* wb, int sheet, int col, double width) {
    // 注意别把工作簿指针也叫 w —— 参数 width 就是宽度，重名会让
    // "宽度为 0"的判断变成"指针为空"，语义完全错掉。
    xl::Workbook* book = (xl::Workbook*)wb;
    if (!book || sheet < 0 || sheet >= (int)book->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::SheetLayout& lay = book->layout(sheet);
    if (width <= 0) lay.colWidths.erase(col);
    else lay.colWidths[col] = width;
    return 0;
}

int xl_set_row_height(void* wb, int sheet, int row, double height) {
    xl::Workbook* book = (xl::Workbook*)wb;
    if (!book || sheet < 0 || sheet >= (int)book->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::SheetLayout& lay = book->layout(sheet);
    if (height <= 0) lay.rowHeights.erase(row);
    else lay.rowHeights[row] = height;
    return 0;
}

double xl_col_width(void* wb, int sheet, int col) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) return 0.0;
    const xl::SheetLayout& lay = w->layout(sheet);
    auto it = lay.colWidths.find(col);
    // 0 表示"用默认宽度"，由 UI 自己决定默认值
    return it == lay.colWidths.end() ? 0.0 : it->second;
}

double xl_row_height(void* wb, int sheet, int row) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) return 0.0;
    const xl::SheetLayout& lay = w->layout(sheet);
    auto it = lay.rowHeights.find(row);
    return it == lay.rowHeights.end() ? 0.0 : it->second;
}

// ---- 冻结窗格 / 自动筛选 ----
int xl_set_freeze(void* wb, int sheet, int cols, int rows) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::SheetLayout& lay = w->layout(sheet);
    lay.freeze.enabled = (cols > 0 || rows > 0);
    lay.freeze.frozenCols = cols;
    lay.freeze.frozenRows = rows;
    return 0;
}

// 读取冻结设置（UI 要画冻结线，所以得能读回来）
int xl_get_freeze(void* wb, int sheet, int* cols, int* rows) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    const xl::SheetLayout& lay = w->layout(sheet);
    if (cols) *cols = lay.freeze.enabled ? lay.freeze.frozenCols : 0;
    if (rows) *rows = lay.freeze.enabled ? lay.freeze.frozenRows : 0;
    return 0;
}

int xl_set_autofilter(void* wb, int sheet, int c0, int r0, int c1, int r1) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::SheetLayout& lay = w->layout(sheet);
    lay.filter.enabled = true;
    lay.filter.c0 = c0; lay.filter.r0 = r0;
    lay.filter.c1 = c1; lay.filter.r1 = r1;
    return 0;
}

int xl_merge(void* wb, int sheet, int c0, int r0, int c1, int r1) {
    if (!wb) { g_lastError = "工作簿为空"; return 1; }
    ((xl::Workbook*)wb)->addMerge(sheet, c0, r0, c1, r1);
    return 0;
}

// 合并信息：该格被吞掉返回 1；是合并区左上角返回 2 并写出跨度；否则 0
int xl_merge_info(void* wb, int sheet, int col, int row, int* spanC, int* spanR) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) return 0;
    if (w->isMergedAway(sheet, col, row)) return 1;
    int ac = 0, ar = 0;
    if (w->mergeAnchorOf(sheet, col, row, ac, ar)) {
        std::pair<int, int> sp = w->mergeSpan(sheet, col, row);
        if (spanC) *spanC = sp.first;
        if (spanR) *spanR = sp.second;
        return 2;
    }
    return 0;
}

// ---- 图表 ----
// 从单元格区域取数据建图。hasHeader=1 时首行/首列当系列名。
// 返回图表索引，失败返回 -1。
int xl_add_chart(void* wb, int sheet, int type,
                 int c0, int r0, int c1, int r1,
                 const char* title, int hasHeader, int catFromFirstCol) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return -1; }
    xl::Sheet& sh = w->sheet((size_t)sheet);

    xl::Chart ch;
    ch.type = (xl::ChartType)type;
    ch.title = title ? title : "";
    ch.anchor.fromCol = c1 + 2;      // 放到数据区右侧，避免盖住数据
    ch.anchor.fromRow = r0;
    ch.anchor.toCol = ch.anchor.fromCol + 8;
    ch.anchor.toRow = r0 + 16;

    // 记下数据源：有它才能事后改区域
    ch.source.valid = true;
    ch.source.sheetName = sh.name();
    ch.source.c0 = c0; ch.source.r0 = r0;
    ch.source.c1 = c1; ch.source.r1 = r1;
    ch.source.hasHeader = hasHeader != 0;
    ch.source.catFromFirstCol = catFromFirstCol != 0;

    std::string e;
    if (!xl::rebuildChartFromSource(sh, ch, e)) { g_lastError = e; return -1; }
    return w->addChart((size_t)sheet, ch, ch.title);
}

// 改已有图表的数据区域。
//
// 关键：不能只改区域字段就完事 —— 系列是值快照，
// 必须按新区域重新生成，否则图表显示的还是旧数据。
// 锚点不动（用户可能手动挪过位置）。
int xl_set_chart_range(void* wb, int sheet, int i,
                       int c0, int r0, int c1, int r1,
                       int hasHeader, int catFromFirstCol) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::Chart* c = w->chart(sheet, (size_t)i);
    if (!c) { g_lastError = "图表索引越界"; return 1; }

    xl::Chart saved = *c;             // 失败时回滚，别把图表弄成半成品
    c->source.valid = true;
    c->source.sheetName = w->sheet((size_t)sheet).name();
    c->source.c0 = c0; c->source.r0 = r0;
    c->source.c1 = c1; c->source.r1 = r1;
    c->source.hasHeader = hasHeader != 0;
    c->source.catFromFirstCol = catFromFirstCol != 0;

    std::string e;
    if (!xl::rebuildChartFromSource(w->sheet((size_t)sheet), *c, e)) {
        *c = saved;
        g_lastError = e;
        return 1;
    }
    return 0;
}

// 改完单元格数值后刷新所有图表（系列是快照，不会自动更新）
int xl_refresh_charts(void* wb, int sheet) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return -1; }
    xl::Sheet& sh = w->sheet((size_t)sheet);
    int n = 0;
    for (size_t i = 0; i < w->chartCount((size_t)sheet); i++) {
        xl::Chart* c = w->chart(sheet, i);
        if (!c || !c->source.valid) continue;
        std::string e;
        if (xl::rebuildChartFromSource(sh, *c, e)) n++;
    }
    return n;
}

// ---- 填充 ----
// 把源区域按相对引用规则铺到目标区域。
//
// 这里必须走引擎的 fillRange，而不是在 Python 侧"复制公式文本"：
// 复制文本的话 =A1*2 填到 B4 还是 =A1*2，引用不会跟着走，
// 填充就失去了意义（Excel 里应该是 =A4*2）。
int xl_fill(void* wb, int sheet, int srcC0, int srcR0, int srcC1, int srcR1,
            int dstC0, int dstR0, int repeatCols, int repeatRows) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return -1; }
    xl::FillRequest req;
    req.srcC0 = srcC0; req.srcR0 = srcR0; req.srcC1 = srcC1; req.srcR1 = srcR1;
    req.dstC0 = dstC0;  req.dstR0 = dstR0;
    req.repeatCols = repeatCols > 0 ? repeatCols : 1;
    req.repeatRows = repeatRows > 0 ? repeatRows : 1;
    xl::FillResult res = xl::fillRange(w->sheet((size_t)sheet), req);
    if (!res.error.empty()) { g_lastError = res.error; return -1; }
    return res.written;
}

// 全部工作表导出到一个 PDF。返回导出的表数（<0 失败）

// ---------------------------------------------------------------------------
// PDF 字体选择
//
// 关键：**中文字体常常不含 ASCII 数字**。实测 DroidSansFallbackFull.ttf
// 就没有 0-9 的字形。只嵌一个字体的话，PDF 里所有数字会变成 .notdef ——
// 不报错、不崩溃，就是数字整片消失。
//
// 所以分两次挑：先挑能显示中文的当主字体，再挑能显示数字的当备用。
// ---------------------------------------------------------------------------
static bool fontHasAll(const std::string& path, const std::string& chars) {
    xl::TtfFont f;
    std::string e;
    if (!f.load(path, e)) return false;
    //
    // 必须按**码点**遍历，不能逐字节。
    // "中" 的 UTF-8 是 E4 B8 AD 三个字节，按字节查 cmap 等于在查
    // U+00E4/U+00B8/U+00AD 这三个码点 —— 字体里当然没有，
    // 于是"这个字体不含中文"的判定恒为真，中文字体一个都选不上。
    //
    for (uint32_t cp : xl::TtfFont::decodeUtf8(chars))
        if (f.glyphFor(cp) == 0) return false;
    return true;
}

// 返回 true 时 fp = 主字体（含中文），fb = 备用字体（含数字，可为空）
static void pickPdfFonts(std::string& fp, std::string& fb) {
    static const char* cjkCand[] = {
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    static const char* latinCand[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/droid/DroidSans.ttf",
    };
    fp.clear(); fb.clear();
    for (auto c : cjkCand) {
        if (fontHasAll(c, "\xE4\xB8\xAD")) { fp = c; break; }     // 含"中"
    }
    if (fp.empty()) return;
    // 主字体自己就有数字的话不用备用
    if (fontHasAll(fp, "0123456789")) return;
    for (auto c : latinCand) {
        if (fontHasAll(c, "0123456789")) { fb = c; break; }
    }
}

int xl_export_pdf_all(void* wb, const char* path, const char* fontPath, int landscape) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w) { g_lastError = "工作簿为空"; return -1; }
    std::string fp, fb;
    if (fontPath && *fontPath) fp = fontPath;
    else pickPdfFonts(fp, fb);
    xl::SheetPdfOptions o;
    o.landscape = landscape != 0;
    o.pageNumbers = true;
    o.fontPath = fp;
    o.fallbackFontPath = fb;
    int done = 0;
    std::string err;
    if (!xl::workbookToPdf(*w, path ? path : "", o, err, &done)) {
        g_lastError = err.empty() ? "导出失败" : err;
        return -1;
    }
    return done;
}

// ---- 条件格式 ----
// type: 0=CellIs 1=Expression 2=Top10 3=AboveAverage 4=Duplicate 5=Unique
// op:   0=None 1=Between 2=NotBetween 3=Equal 4=NotEqual
//       5=GreaterThan 6=LessThan 7=GtOrEq 8=LtOrEq
int xl_add_cf(void* wb, int sheet, int c0, int r0, int c1, int r1,
              int type, int op, const char* f1, const char* f2,
              const char* fill, const char* fontColor, int bold, int italic,
              int bottom, int percent, int rank, int above) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }

    xl::ConditionalFormat cf;
    cf.rects.push_back({{c0, r0, c1, r1}});

    xl::CfRule r;
    r.type = (xl::CfType)type;
    r.op = (xl::CfOperator)op;
    if (f1 && *f1) r.formulas.push_back(f1);
    if (f2 && *f2) r.formulas.push_back(f2);
    r.style.fillColor = fill ? fill : "";
    r.style.fontColor = fontColor ? fontColor : "";
    r.style.bold = bold != 0;
    r.style.italic = italic != 0;
    r.bottom = bottom != 0;
    r.percent = percent != 0;
    r.rank = rank > 0 ? rank : 10;
    r.above = above != 0;
    cf.rules.push_back(r);
    w->addCf(sheet, cf);
    return 0;
}

int xl_cf_count(void* wb, int sheet) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->allCfs().size()) return 0;
    return (int)w->allCfs()[(size_t)sheet].size();
}

// 返回第 i 条规则的描述，JSON 太重，这里用制表符分隔的字段串
char* xl_cf_info(void* wb, int sheet, int i) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->allCfs().size()) return dupStr("");
    const auto& v = w->allCfs()[(size_t)sheet];
    if (i < 0 || i >= (int)v.size()) return dupStr("");
    const xl::ConditionalFormat& cf = v[(size_t)i];
    std::ostringstream o;
    // 区域（取第一块）
    if (!cf.rects.empty()) {
        o << cf.rects[0][0] << ',' << cf.rects[0][1] << ',' << cf.rects[0][2] << ',' << cf.rects[0][3];
    } else o << ",,,,";
    o << '\t' << (int)(cf.rules.empty() ? xl::CfType::CellIs : cf.rules[0].type);
    o << '\t' << (int)(cf.rules.empty() ? xl::CfOperator::None : cf.rules[0].op);
    if (!cf.rules.empty()) {
        const auto& r = cf.rules[0];
        o << '\t' << (r.formulas.size() > 0 ? r.formulas[0] : "");
        o << '\t' << (r.formulas.size() > 1 ? r.formulas[1] : "");
        o << '\t' << r.style.fillColor << '\t' << r.style.fontColor;
        o << '\t' << (r.style.bold ? 1 : 0) << (r.style.italic ? 1 : 0);
        o << '\t' << (int)cf.rules.size();
    }
    return dupStr(o.str());
}

int xl_clear_cf(void* wb, int sheet) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    w->clearCfs(sheet);
    return 0;
}

// ---- 区域复制（拖拽移动选区要用）----
// 把源区域的内容复制到目标左上角。走的必须是引擎的引用平移，
// 自己拼公式文本会让 =A1+1 移到别处还指向 A1。
int xl_copy_range(void* wb, int sheet, int sc0, int sr0, int sc1, int sr1,
                  int dc0, int dr0) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::FillRequest req;
    req.srcC0 = sc0; req.srcR0 = sr0; req.srcC1 = sc1; req.srcR1 = sr1;
    req.dstC0 = dc0;  req.dstR0 = dr0;
    req.repeatCols = sc1 - sc0 + 1;
    req.repeatRows = sr1 - sr0 + 1;
    xl::FillResult res = xl::fillRange(w->sheet((size_t)sheet), req);
    if (!res.error.empty()) { g_lastError = res.error; return -1; }
    return res.written;
}

// 清空区域（拖拽移动后要把原位置擦掉）
int xl_erase_range(void* wb, int sheet, int c0, int r0, int c1, int r1) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::Sheet& sh = w->sheet((size_t)sheet);
    // 先快照再删：边遍历边 erase 会让迭代器失效
    std::vector<std::pair<int, int>> targets;
    for (auto& kv : sh.allCells())
        if (kv.first.first >= c0 && kv.first.first <= c1 &&
            kv.first.second >= r0 && kv.first.second <= r1)
            targets.push_back(kv.first);
    for (auto& t : targets) sh.eraseCell(t.first, t.second);
    return (int)targets.size();
}

// ---------------------------------------------------------------------------
// 数据验证
// ---------------------------------------------------------------------------
// type: 1=整数 2=小数 3=序列 4=日期 5=时间 6=文本长度 7=自定义公式
// op:   0=无 1=介于 2=不介于 3=等于 4=不等于 5=大于 6=小于 7=≥ 8=≤
// errStyle: 0=拒绝 1=警告 2=仅提示
int xl_add_dv(void* wb, int sheet, int c0, int r0, int c1, int r1,
              int type, int op, const char* f1, const char* f2,
              int errStyle, const char* errTitle, const char* errMsg,
              int allowBlank, int showErr) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }

    xl::DataValidation dv;
    dv.type = (xl::DvType)type;
    dv.op = (xl::DvOperator)op;
    dv.errorStyle = (xl::DvErrorStyle)errStyle;
    dv.rects.push_back({{c0, r0, c1, r1}});
    dv.formula1 = f1 ? f1 : "";
    dv.formula2 = f2 ? f2 : "";
    dv.errorTitle = errTitle ? errTitle : "";
    dv.error = errMsg ? errMsg : "";
    dv.allowBlank = allowBlank != 0;
    dv.showErrorMessage = showErr != 0;
    w->addDv(sheet, dv);
    return 0;
}

int xl_dv_count(void* wb, int sheet) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->allDvs().size()) return 0;
    return (int)w->allDvs()[(size_t)sheet].size();
}

// 字段用 \t 分隔：rect \t type \t op \t f1 \t f2 \t errStyle \t errTitle \t err
char* xl_dv_info(void* wb, int sheet, int i) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->allDvs().size()) return dupStr("");
    const auto& v = w->allDvs()[(size_t)sheet];
    if (i < 0 || i >= (int)v.size()) return dupStr("");
    const xl::DataValidation& d = v[(size_t)i];
    std::ostringstream o;
    if (!d.rects.empty())
        o << d.rects[0][0] << ',' << d.rects[0][1] << ',' << d.rects[0][2] << ',' << d.rects[0][3];
    else o << ",,,";
    o << '\t' << (int)d.type << '\t' << (int)d.op;
    o << '\t' << d.formula1 << '\t' << d.formula2;
    o << '\t' << (int)d.errorStyle << '\t' << d.errorTitle << '\t' << d.error;
    return dupStr(o.str());
}

int xl_clear_dv(void* wb, int sheet) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    w->clearDvs(sheet);
    return 0;
}

// ---------------------------------------------------------------------------
// 批注
// ---------------------------------------------------------------------------
int xl_add_note(void* wb, int sheet, int col, int row,
                const char* author, const char* text) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::CellNote n;
    n.col = col; n.row = row;
    n.author = author ? author : "";
    n.text = text ? text : "";
    w->addNote(sheet, n);
    return 0;
}

int xl_note_count(void* wb, int sheet) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->allNotes().size()) return 0;
    return (int)w->allNotes()[(size_t)sheet].size();
}

// col \t row \t author \t text
char* xl_note_info(void* wb, int sheet, int i) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->allNotes().size()) return dupStr("");
    const auto& v = w->allNotes()[(size_t)sheet];
    if (i < 0 || i >= (int)v.size()) return dupStr("");
    const xl::CellNote& n = v[(size_t)i];
    std::ostringstream o;
    o << n.col << '\t' << n.row << '\t' << n.author << '\t' << n.text;
    return dupStr(o.str());
}

// 删单条批注。引擎只提供整体清除，这里直接操作内部的 vector。
int xl_remove_note(void* wb, int sheet, int col, int row) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->allNotes().size()) { g_lastError = "表索引越界"; return 1; }
    return w->removeNote(sheet, col, row) ? 0 : 1;
}

int xl_clear_notes(void* wb, int sheet) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    w->clearNotes(sheet);
    return 0;
}

// ---------------------------------------------------------------------------
// 图表编辑
// ---------------------------------------------------------------------------
int xl_chart_count(void* wb, int sheet) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) return 0;
    return (int)w->chartCount((size_t)sheet);
}

// type \t title \t anchor(c0,r0,c1,r1) \t 系列数 \t 点数
char* xl_chart_info(void* wb, int sheet, int i) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) return dupStr("");
    xl::Chart* c = w->chart(sheet, (size_t)i);
    if (!c) return dupStr("");
    std::ostringstream o;
    o << (int)c->type << '\t' << c->title;
    o << '\t' << c->anchor.fromCol << ',' << c->anchor.fromRow << ','
      << c->anchor.toCol << ',' << c->anchor.toRow;
    o << '\t' << c->series.size() << '\t' << c->pointCount();
    // 源区域（改数据区域时要用它预填表单）
    o << '\t' << (c->source.valid ? 1 : 0)
      << '\t' << c->source.c0 << ',' << c->source.r0 << ','
      << c->source.c1 << ',' << c->source.r1
      << '\t' << (c->source.hasHeader ? 1 : 0)
      << '\t' << (c->source.catFromFirstCol ? 1 : 0);
    // 系列名
    for (auto& s : c->series) o << '\t' << s.name;
    return dupStr(o.str());
}

int xl_set_chart_type(void* wb, int sheet, int i, int type) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::Chart* c = w->chart(sheet, (size_t)i);
    if (!c) { g_lastError = "图表索引越界"; return 1; }
    c->type = (xl::ChartType)type;
    return 0;
}

int xl_set_chart_title(void* wb, int sheet, int i, const char* title) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::Chart* c = w->chart(sheet, (size_t)i);
    if (!c) { g_lastError = "图表索引越界"; return 1; }
    c->title = title ? title : "";
    // chartTitles_ 是存盘时实际用的那份，只改 Chart::title 会不同步
    w->syncChartTitle((size_t)sheet, (size_t)i, c->title);
    return 0;
}

int xl_set_chart_anchor(void* wb, int sheet, int i, int c0, int r0, int c1, int r1) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::Chart* c = w->chart(sheet, (size_t)i);
    if (!c) { g_lastError = "图表索引越界"; return 1; }
    c->anchor.fromCol = c0; c->anchor.fromRow = r0;
    c->anchor.toCol = c1;   c->anchor.toRow = r1;
    return 0;
}

int xl_remove_chart(void* wb, int sheet, int i) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    if (i < 0 || i >= (int)w->chartCount((size_t)sheet)) { g_lastError = "图表索引越界"; return 1; }
    auto& v = w->chartsOf((size_t)sheet);
    v.erase(v.begin() + i);
    // 标题表要同步删，否则后面的图表会套用错位的标题
    w->eraseChartTitle((size_t)sheet, (size_t)i);
    return 0;
}

// ---- 结构性编辑 ----
int xl_insert_rows(void* wb, int sheet, int at, int count) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    if (at < 0 || count <= 0) { g_lastError = "参数无效"; return 1; }
    w->insertRows(sheet, at, count);
    return 0;
}

int xl_insert_cols(void* wb, int sheet, int at, int count) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    if (at < 0 || count <= 0) { g_lastError = "参数无效"; return 1; }
    w->insertCols(sheet, at, count);
    return 0;
}

int xl_delete_rows(void* wb, int sheet, int at, int count) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    if (at < 0 || count <= 0) { g_lastError = "参数无效"; return 1; }
    w->deleteRows(sheet, at, count);
    return 0;
}

int xl_delete_cols(void* wb, int sheet, int at, int count) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    if (at < 0 || count <= 0) { g_lastError = "参数无效"; return 1; }
    w->deleteCols(sheet, at, count);
    return 0;
}

int xl_unmerge(void* wb, int sheet, int c0, int r0, int c1, int r1) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    // 返回 0 表示确实取消了合并，1 表示该位置本来就没合并 —— 界面要区分提示
    return w->unmerge(sheet, c0, r0, c1, r1) ? 0 : 1;
}

// ---- PDF 导出 ----
// 只导出单张表。多表合并需要一个能追加页面的 PDF writer，
// 现有 sheetToPdf 每次调用都新建文件，硬凑会把前面的页覆盖掉。
int xl_export_pdf(void* wb, int sheet, const char* path,
                  const char* fontPath, int landscape) {
    xl::Workbook* w = (xl::Workbook*)wb;
    if (!w || sheet < 0 || sheet >= (int)w->sheetCount()) { g_lastError = "表索引越界"; return 1; }
    xl::Sheet& sh = w->sheet((size_t)sheet);
    // 逐格取一次值触发求值，否则公式格导出的是空
    for (auto& kv : sh.allCells()) sh.valueAt(kv.first.first, kv.first.second);

    xl::SheetPdfOptions o;
    o.landscape = landscape != 0;
    o.pageNumbers = true;          // 多页时才知道看到的是第几页，默认开
    o.title = (sheet < (int)w->sheetNames().size()) ? w->sheetNames()[(size_t)sheet]
                                                         : ("Sheet" + std::to_string(sheet + 1));
    std::string fp, fb;
    if (fontPath && *fontPath) fp = fontPath;
    else pickPdfFonts(fp, fb);
    o.fontPath = fp;
    o.fallbackFontPath = fb;
    std::string err;
    if (!xl::sheetToPdf(sh, path ? path : "", o, err)) {
        g_lastError = err.empty() ? "导出失败" : err;
        return 1;
    }
    return 0;
}

// ---- 引擎自检 ----
int xl_func_count(void) {
    return (int)xl::functionNames().size();
}

// 全部函数名，逗号分隔。UI 的"函数列表"对话框要用。
// 返回的是 malloc 出来的字符串，调用方用 xl_str_free 释放。
char* xl_func_names(void) {
    const std::vector<std::string>& ns = xl::functionNames();
    std::string joined;
    // 预留空间，避免 495 次追加反复扩容
    size_t total = 0;
    for (const auto& n : ns) total += n.size() + 1;
    joined.reserve(total);
    for (size_t i = 0; i < ns.size(); i++) {
        if (i) joined += ',';
        joined += ns[i];
    }
    return dupStr(joined);
}

}  // extern "C"
