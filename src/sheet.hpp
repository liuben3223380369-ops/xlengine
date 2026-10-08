#pragma once
// ---------------------------------------------------------------------------
// 稀疏单元格存储 + 依赖驱动的惰性重算。
// 采用 pull-based 递归求值：求值时按需递归到前驱，天然处理依赖顺序，
// 并用访问栈检测循环引用。
// ---------------------------------------------------------------------------
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ast.hpp"
#include "parser.hpp"
#include "depgraph.hpp"
#include "precedent.hpp"
#include "style.hpp"

namespace xl {

class Workbook;

// 跨表引用转发。定义在 xlsx.cpp，这样 sheet.cpp 不必知道 Workbook 的完整布局。
// 单表使用时 owner_ 为空，跨表引用直接返回 #REF!。
Value lookupCrossSheet(Workbook* wb, const std::string& sheetName, int col, int row);
bool  lookupCrossSheetByAddr(Workbook* wb, const std::string& sheetName,
                             const std::string& addr, Value& out);

std::string colToName(int col);          // 0 -> "A"
std::string addrToStr(int col, int row); // (0,0) -> "A1"

class Sheet : public EvalCtx {
public:
    // 单元格记录。
    //
    // 这里刻意**不**放数字格式码与视觉样式：那些是稀疏的（绝大多数格子没有），
    // 内联进来会让每个 CellRec 白背 184 字节（CellStyle 152 + string 32）。
    // 实测 10 万格工作簿每格约 375 字节，其中近一半来自这两个很少用到的字段。
    // 它们改存在 Sheet 的两个稀疏 map 里，只有真正设过的格子才占空间。
    struct CellRec {
        std::string formula;             // 原始公式文本（不含 =），空表示常量
        NodePtr ast;
        Value value;                     // 缓存值
        bool hasFormula = false;
        bool evaluated = false;
    };

    int maxRow = 1048575;                // Excel 上限 1,048,576
    int maxCol = 16383;                  // Excel 上限 16,384 (XFD)

    // EvalCtx
    Value cell(const std::string& sheet, int col, int row) override;
    std::string sheetName() const override { return name_; }
    void currentCell(int& col, int& row) const override { col = curCol_; row = curRow_; }
    bool cellByAddr(const std::string& sheet, const std::string& addr, Value& out) override;
    // LET / LAMBDA 作用域
    bool lookupName(const std::string& name, Value& out) const override;
    void bindName(const std::string& name, const Value& v) override;
    void unbindNames(size_t count) override;
    Value applyLambda(const LambdaDef& fn, const std::vector<Value>& args) override;

    void setName(const std::string& n) { name_ = n; }
    const std::string& name() const { return name_; }
    void setOwner(Workbook* w) { owner_ = w; }
    // 定义名称求值入口（EvalCtx 覆写）。转发给工作簿，由它查表并展开。
    bool definedName(const std::string& name, Value& out) override;
    // 当前求值位置（定义名称展开时需要，供 ROW()/OFFSET 之类使用）
    int curCol() const { return curCol_; }
    int curRow() const { return curRow_; }
    Workbook* owner() const { return owner_; }

    // 设置一个常量（数字 / 文本 / 布尔）
    void setValue(int col, int row, const Value& v);

    // 彻底移除单元格（而不是留下一个空值记录）。
    // 区别很重要：留下记录的话 hasCell() 恒为真，
    // 导出时会出现一堆空行空列，撤销也要多存一份无用状态。
    void eraseCell(int col, int row);

    // 单元格记录数 / 格式码数 / 样式数（诊断与测试用）
    size_t numFmtCount() const { return numFmts_.size(); }
    size_t styleCount() const { return styles_.size(); }

    // 数字格式码
    void setNumFmt(int col, int row, const std::string& code);
    std::string numFmtAt(int col, int row) const;

    // 视觉样式
    void setStyle(int col, int row, const CellStyle& st);
    CellStyle styleAt(int col, int row) const;

    // 设置公式；返回解析错误信息（空表示成功）
    std::string setFormula(int col, int row, const std::string& text);

    bool hasCell(int col, int row) const { return cells_.count({col, row}) > 0; }
    // EvalCtx 扩展：公式文本与地址（FORMULATEXT / ISFORMULA / CELL 用）
    bool cellFormula(int col, int row, std::string& out) const override;
    bool cellAddr(int col, int row, std::string& out) const override;
    bool usedRange(const std::string& sheet, int& c0, int& r0, int& c1, int& r1) const override;
    // 引用语义（FORMULATEXT / ISFORMULA / CELL）
    void pushArgRefs(const std::vector<std::pair<int,int>>& refs) override;
    void popArgRefs() override;
    bool argRefAt(size_t idx, int& col, int& row) const override;
    CellRec* find(int col, int row);
    const CellRec* find(int col, int row) const;

    // 全量重算（清空缓存）。语义简单、绝对正确，但改动一格也要重算整表。
    // 交互场景请用 recalcDirty()。
    void recalc();

    // 增量重算：只算脏格子。
    // 依赖图在 setFormula 时建好，setValue 时把脏标记传播给后继。
    // 改一格的成本从 O(全部公式) 降到 O(受影响的公式)。
    void recalcDirty();

    // 依赖图状态（供测试与诊断）
    size_t depEdgeCount() const { return deps_.edgeCount(); }
    size_t depCellCount() const { return deps_.cellCount(); }
    size_t dirtyCount() const { return deps_.dirty().size(); }
    void markAllDirty();
    // 把当前脏集合里所有公式格的缓存作废。
    //
    // 这一步不能省 —— 依赖图只是"谁该重算"的索引，真正决定是否重算的是
    // CellRec::evaluated。只标脏不清缓存的话，recalcDirty 会看到
    // evaluated=true 直接跳过，于是改了源数据结果却不变。
    // 这个 bug 的表现是"看起来跑得很快，但数是旧的"，比慢更危险。
    void invalidateDirty();
    // 定义名称变更时调用：所有引用该名称的公式都要重算
    void invalidateName(const std::string& name) { deps_.markUsingName(name); }
    // 别的表改动时调用（由工作簿转发）
    void invalidateSheetRef(const std::string& sheet) { deps_.markUsingSheet(sheet); }

    // 取显示文本（用于 UI / 测试）。会套用单元格的数字格式码。
    std::string display(int col, int row);
    // 不套格式的原始文本（值与公式的通用显示）
    std::string displayRaw(int col, int row);
    Value valueAt(int col, int row);

    // 在指定单元格的上下文里求值一条公式文本：不写入、不缓存、不参与依赖图。
    //
    // 条件格式、数据验证这类"按格判定"的功能需要它 —— 它们要按每个格子
    // 算一次判定表达式，但这些表达式并不是单元格内容，不该进存储、
    // 也不该被 recalc 扫到。
    //
    // 仍然会设置 ROW()/COLUMN()/OFFSET 所需的当前格上下文，
    // 所以判定公式里写 =ROW() 是有意义的。
    Value evalExprAt(const std::string& formula, int col, int row);

    const std::set<std::pair<int,int>>& circularCells() const { return circular_; }
    size_t cellCount() const { return cells_.size(); }
    void clear();

    // 迭代所有单元格（导出 / UI 用）
    const std::map<std::pair<int,int>, CellRec>& allCells() const { return cells_; }

private:
    std::string name_;
    std::map<std::pair<int,int>, CellRec> cells_;
    // 稀疏的格式码与样式。绝大多数格子没有，单独存省掉每格 184 字节。
    // 二者合起来决定 OOXML 的 xf 索引 —— 不能各自编号，见 xlsx.cpp。
    std::map<std::pair<int,int>, std::string> numFmts_;
    std::map<std::pair<int,int>, CellStyle> styles_;
    DepGraph deps_;                      // 依赖图（增量重算用）

    // 已用区域缓存。
    // 原实现是每次调用遍历全部格子 —— O(n)。它被两处高频调用：
    //   setValue（每次编辑）和整列引用的求值裁剪。
    // 于是"改一格"变成 O(全部格子)，这直接抵消了增量重算的收益。
    // 写入时增量扩张；只有删除格子时才失效（缩小必须重算）。
    mutable int uC0_ = 0, uR0_ = 0, uC1_ = -1, uR1_ = -1;
    mutable bool uValid_ = false;
    void touchUsed(int col, int row) const {
        if (uValid_) {
            if (col < uC0_) uC0_ = col;
            if (col > uC1_) uC1_ = col;
            if (row < uR0_) uR0_ = row;
            if (row > uR1_) uR1_ = row;
        } else {
            uC0_ = uC1_ = col;
            uR0_ = uR1_ = row;
            uValid_ = true;
        }
    }
    void invalidateUsed() const { uValid_ = false; }
    bool computeUsed(int& c0, int& r0, int& c1, int& r1) const;
    std::set<std::pair<int,int>> visiting_;
    std::set<std::pair<int,int>> circular_;
    mutable int curCol_ = -1, curRow_ = -1;
    // 引用语义栈：每组是一次函数调用的参数地址列表
    std::vector<std::vector<std::pair<int,int>>> argRefStack_;
    Workbook* owner_ = nullptr;          // 归属工作簿，用于跨表引用
    // 后进先出的名字绑定栈，支持 LET 嵌套与 LAMBDA 递归
    std::vector<std::pair<std::string, Value>> scope_;

    Value evaluate(int col, int row);

public:
    // 供外部（脚本层）定义命名 lambda，如 名称管理器
    void defineName(const std::string& name, const Value& v);
};

} // namespace xl
