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
#include <string>
#include <vector>

#include "sheet.hpp"
#include "xlsx.hpp"
#include "chart.hpp"
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

    std::string sn = sh.name();
    auto absRange = [&](int a, int b, int c, int d) {
        return sn + "!$" + xl::colToName(a) + "$" + std::to_string(b + 1) +
               ":$" + xl::colToName(c) + "$" + std::to_string(d + 1);
    };

    int dataR0 = r0;
    if (hasHeader) dataR0 = r0 + 1;

    // 系列来自列还是行：Excel 默认按列（首列作分类标签）
    if (catFromFirstCol) {
        for (int c = c0 + 1; c <= c1; c++) {
            xl::DataSeries s;
            std::string nm;
            if (hasHeader) nm = sh.display(c, r0);
            s.name = nm.empty() ? ("系列" + std::to_string(c - c0)) : nm;
            s.catRange = absRange(c0, dataR0, c0, r1);
            s.valRange = absRange(c, dataR0, c, r1);
            for (int r = dataR0; r <= r1; r++) {
                xl::Value v = sh.valueAt(c, r);
                s.values.push_back(v.isNum() ? v.n : 0.0);
            }
            ch.series.push_back(s);
        }
        for (int r = dataR0; r <= r1; r++) ch.categories.push_back(sh.display(c0, r));
    } else {
        for (int r = r0 + (hasHeader ? 1 : 0); r <= r1; r++) {
            xl::DataSeries s;
            std::string nm;
            if (hasHeader) nm = sh.display(c0, r);
            s.name = nm.empty() ? ("系列" + std::to_string(r - r0 + 1)) : nm;
            s.catRange = absRange(c0 + 1, dataR0, c1, dataR0);
            s.valRange = absRange(c0 + 1, r, c1, r);
            for (int c = c0 + 1; c <= c1; c++) {
                xl::Value v = sh.valueAt(c, r);
                s.values.push_back(v.isNum() ? v.n : 0.0);
            }
            ch.series.push_back(s);
        }
        for (int c = c0 + 1; c <= c1; c++) ch.categories.push_back(sh.display(c, hasHeader ? r0 : r0));
    }

    if (ch.series.empty()) { g_lastError = "选中区域没有可用数据"; return -1; }
    return w->addChart((size_t)sheet, ch, ch.title);
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
