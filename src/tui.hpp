#pragma once
// ---------------------------------------------------------------------------
// 终端网格界面（TUI）
//
// 设计上刻意把两件事分开：
//   1. 渲染 —— 写进一个内存里的 Screen 缓冲，不碰真实终端。
//   2. I/O  —— 只有真正运行时才开 termios 原始模式、才发 ANSI 序列。
// 分开之后界面逻辑可以在无终端环境（TERM=dumb、CI）里用纯文本断言来测，
// 否则这类代码只能靠"人眼看一眼"，回归时最容易悄悄坏掉。
//
// 另一处不能省的是 CJK 宽度：中文占 2 个显示列。
// 一个"销售额"按 3 个字符截断会吃掉后面一列的内容，整个网格就错位了。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <cstdint>
#include "sheet.hpp"
#include "xlsx.hpp"

namespace xl {

// 命令参数分词（引号内不切分）
std::vector<std::string> tokenizeSpec(const std::string& spec);


// ---------------------------------------------------------------------------
// UTF-8 与显示宽度
// ---------------------------------------------------------------------------
std::vector<uint32_t> decodeUtf8(const std::string& s);
// 单个码点的显示列宽（CJK/全角 = 2）
int  codePointWidth(uint32_t cp);
// 字符串的总显示宽度
int  displayWidth(const std::string& s);
// 按显示宽度裁切，放不下时补 "…"
std::string fitToWidth(const std::string& s, int width);
// 按显示宽度右对齐
std::string padLeftTo(const std::string& s, int width);

// ---------------------------------------------------------------------------
// 屏幕缓冲
// ---------------------------------------------------------------------------
enum class TuiColor { Default = 0, Black, Red, Green, Yellow, Blue, Magenta, Cyan, White, Gray };

struct Attr {
    TuiColor fg = TuiColor::Default;
    TuiColor bg = TuiColor::Default;
    bool bold = false;
    bool reverse = false;
};

class Screen {
public:
    Screen(int w = 80, int h = 24);

    int width()  const { return w_; }
    int height() const { return h_; }

    void clear(Attr a = Attr{});
    void put(int x, int y, char32_t c, Attr a);
    // 覆盖式写串（会清掉该行剩余部分之前的内容）
    void putStr(int x, int y, const std::string& utf8, Attr a);
    void fillRow(int y, const std::string& utf8, Attr a);   // 整行，右侧补空格
    void fillCell(int x, int y, int width, const std::string& utf8, Attr a);

    // 屏幕坐标 -> 缓冲下标（越界时返回 false）
    bool at(int x, int y, char32_t& c, Attr& a) const;

    // 输出
    std::string toPlainText() const;    // 每行去尾部空格，用于测试与无终端环境
    std::string toAnsi() const;         // 带 SGR 序列，用于真实终端
    int ansiColor(TuiColor c, bool bg) const;

private:
    int w_, h_;
    std::vector<char32_t> ch_;
    std::vector<Attr> at_;
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
};

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------
enum class Key {
    Unknown, Left, Right, Up, Down, Home, End,
    PageUp, PageDown, Enter, Escape, Tab, Backspace, Delete,
    F2, F5, Char,
    SheetPrev, SheetNext      // Ctrl+PageUp / Ctrl+PageDown
};

struct KeyEvent {
    Key key = Key::Unknown;
    char32_t ch = 0;        // Key::Char 时有效
    bool ctrl = false;
    bool shift = false;
};

// 把一段输入字节流解析成按键序列（处理 ESC [ A 这类方向键）
std::vector<KeyEvent> parseKeys(const std::string& bytes);

// ---------------------------------------------------------------------------
// 选区
// ---------------------------------------------------------------------------
struct Range {
    int c0 = 0, r0 = 0, c1 = 0, r1 = 0;
    void normalize();
    int cols() const { return c1 - c0 + 1; }
    int rows() const { return r1 - r0 + 1; }
    int count() const { return cols() * rows(); }
    bool contains(int c, int r) const {
        return c >= c0 && c <= c1 && r >= r0 && r <= r1;
    }
    bool isSingle() const { return c0 == c1 && r0 == r1; }
    std::string addr() const;      // "A1" 或 "A1:C5"
};

// ---------------------------------------------------------------------------
// 撤销栈
//
// 存的是"改动前后的单元格快照"，不是整表快照 —— 后者在大表上每次编辑
// 都要复制一遍，而实际一次改动只涉及很少的格子。
// ---------------------------------------------------------------------------
struct CellSnap {
    int col = 0, row = 0;
    bool existed = false;
    std::string formula;
    bool hasFormula = false;
    Value value;
};

struct UndoEntry {
    std::string label;
    int sheet = 0;
    std::vector<std::pair<CellSnap, CellSnap>> diff;   // {改动前, 改动后}
    // 纯格式变更不落在 Value 里，CellSnap 存不到，单独记一份：
    // 记录区域起点、尺寸与"改动前"的格式码（按行优先顺序）
    bool fmtOnly = false;
    int fmtC0 = 0, fmtR0 = 0, fmtCols = 0, fmtRows = 0;
    std::vector<std::string> fmtBefore;
    std::string fmtAfter;
    std::vector<CellStyle> fmtBeforeStyle;   // 旧样式（逐格）
    CellStyle fmtAfterStyle;                 // 新样式
    bool hasStyle = false;                   // 本次改动是否含样式
};

// ---------------------------------------------------------------------------
// 网格界面
// ---------------------------------------------------------------------------
class GridUI {
public:
    enum class Mode { Normal, Edit, Command };

    void attach(Workbook* wb, const std::string& path = std::string());
    void resize(int w, int h);

    void render(Screen& s) const;
    // 返回 false 表示请求退出
    bool handleKey(const KeyEvent& ev);
    // 非交互驱动：直接执行一条 ":" 命令，供脚本与测试使用
    bool runCommand(const std::string& cmd, std::string& msg);

    Mode mode() const { return mode_; }
    int  cursorCol() const { return curCol_; }
    int  cursorRow() const { return curRow_; }
    int  topRow() const { return topRow_; }
    int  leftCol() const { return leftCol_; }
    const std::string& status() const { return status_; }
    bool dirty() const { return dirty_; }
    // 测试驱动用：直接把光标放到指定格
    void setCursor(int c, int r) { curCol_ = c; curRow_ = r; }
    const std::string& editBuffer() const { return editBuf_; }
    // 当前单元格的原始内容（公式则带前导 =）
    std::string currentContent() const;

    void setMessage(const std::string& m) { status_ = m; }
    bool quitRequested() const { return quit_; }

    // ---- 多表 ----
    int  sheetIndex() const { return sheetIdx_; }
    int  sheetCount() const { return wb_ ? (int)wb_->sheetCount() : 0; }
    bool switchSheet(int idx, std::string& msg);
    bool newSheet(const std::string& name, std::string& msg);
    std::string sheetName() const;
    Sheet* currentSheet();
    const std::vector<std::string>& tabNames() const { return tabNames_; }

    // ---- 选区 ----
    const Range& selection() const { return sel_; }
    void setSelection(int c0, int r0, int c1, int r1);
    void collapseSelection();

    // ---- 撤销 ----
    bool undo(std::string& msg);
    bool redo(std::string& msg);
    size_t undoDepth() const { return undoStack_.size(); }
    size_t redoDepth() const { return redoStack_.size(); }

    // ---- 剪贴板 ----
    void copySelection();
    bool paste(std::string& msg);
    bool clipboardEmpty() const { return clip_.empty(); }

    // 供测试直接调用的一次性编辑（等价于进入编辑态输入后回车）
    bool editCell(int col, int row, const std::string& text, std::string& msg);

private:
    Workbook* wb_ = nullptr;
    int sheetIdx_ = 0;
    std::string path_;
    std::vector<std::string> tabNames_;

    int curCol_ = 0, curRow_ = 0;
    int anchorCol_ = 0, anchorRow_ = 0;
    Range sel_;
    int topRow_ = 0, leftCol_ = 0;
    int w_ = 80, h_ = 24;
    Mode mode_ = Mode::Normal;
    std::string editBuf_;
    std::string cmdBuf_;
    std::string status_ = "就绪";
    bool dirty_ = false;
    std::string noteAuthor_;              // 批注署名（默认"我"）
    bool quit_ = false;

    // 剪贴板：相对原点的单元格内容
    struct ClipCell { int dc = 0, dr = 0; std::string formula; bool hasFormula = false; Value value; };
    std::vector<ClipCell> clip_;
    int clipW_ = 0, clipH_ = 0;

    std::vector<UndoEntry> undoStack_;
    std::vector<UndoEntry> redoStack_;
    UndoEntry pending_;                              // 正在记录的一次操作

    static const int kRowHeadW = 6;     // 行号列宽
    static const int kColW     = 11;    // 每个单元格的显示宽度

    int visibleCols() const;
    int visibleRows() const;
    void clampCursor();
    void scrollIntoView();
    void commitEdit();
    void beginEdit(bool replace);
    void moveCursor(int dc, int dr, bool extend);

    // 撤销相关
    CellSnap snapCell(Sheet* sh, int col, int row) const;
    void applySnap(Sheet* sh, const CellSnap& s) const;
    void beginUndo(const std::string& label);
    void recordCell(Sheet* sh, int col, int row);   // 记下"改动前"
    void endUndo();                                  // 补上"改动后"并压栈
    void discardUndo();

    bool cmdSave(const std::string& arg, std::string& msg);
    bool cmdLoad(const std::string& arg, std::string& msg);
    bool cmdPdf(const std::string& arg, std::string& msg);
    bool cmdChart(std::string& msg);
    // 按方向填充：down 用选区首行作源，right 用首列作源
    bool fillSelection(bool byColumn, std::string& msg);
    // 给选区设置数字格式码；空串表示清除（回到 General）
    bool applyNumFmt(const std::string& code, std::string& msg);
    // 给选区叠加样式属性（可同时写多个，空格分隔）
    bool applyStyle(const std::string& spec, std::string& msg);
    // ---- 定义名称 ----
    // :name 销售额          把当前选区定义成名字
    // :name 销售额 A1:B5    显式给区域
    // :name                 列出全部定义名称
    // :name del 销售额      删除
    bool applyNameCmd(const std::string& body, std::string& msg);

    // ---- 嵌入图片 ----
    // :img 把当前表上的图表光栅化成 PNG 后嵌入（锚点用当前选区）
    bool embedChartImage(int chartIndex, std::string& msg);
    bool listImages(std::string& msg) const;

    // ---- 视图属性 ----
    bool applyViewCmd(const std::string& body, std::string& msg);

    // ---- 批注 ----
    void setNoteAuthor(const std::string& a) { noteAuthor_ = a; }
    const std::string& noteAuthor() const { return noteAuthor_; }
    bool applyNote(const std::string& text, std::string& msg);
    bool clearNote(std::string& msg);
    bool showNote(std::string& msg) const;

    // 覆盖当前格的数据验证规则（无则 nullptr）
    const DataValidation* currentDv(int col, int row) const;

    // 数据验证：给选区加规则 / 清空 / 列出
    bool applyDv(const std::string& spec, std::string& msg);
    bool clearDv(std::string& msg);
    bool listDv(std::string& msg) const;

    // 条件格式：给选区加规则 / 清空 / 列出
    bool applyCf(const std::string& spec, std::string& msg);
    bool clearCf(std::string& msg);
    bool listCf(std::string& msg) const;

    // 合并 / 取消合并当前选区
    bool mergeSelection(std::string& msg);
    bool unmergeSelection(std::string& msg);
    void refreshTabs();
};

// ---------------------------------------------------------------------------
// 真实终端：原始模式 + 尺寸探测。无终端（管道 / TERM=dumb）时优雅降级。
// ---------------------------------------------------------------------------
class Terminal {
public:
    bool enterRaw();       // 失败表示不是交互终端
    void leaveRaw();
    bool size(int& w, int& h);
    std::string readSome();
    void write(const std::string& s);
    void clearScreen();
    void hideCursor();
    void showCursor();
private:
    bool raw_ = false;
    // 平台相关状态，存成 void*/整数以避免头文件污染：
    //   POSIX: termios*      Windows: 原控制台模式 DWORD
    void* orig_ = nullptr;
    long long origMode_ = 0;
    unsigned long hIn_ = 0, hOut_ = 0;   // Windows: HANDLE 位模式存储
};

// 用当前工作簿跑一次交互界面。isInteractive=false 时只渲染一帧并打印纯文本。
int runTui(Workbook& wb, const std::string& path, bool isInteractive);

} // namespace xl
