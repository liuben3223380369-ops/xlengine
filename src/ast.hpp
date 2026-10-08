#pragma once
// ---------------------------------------------------------------------------
// AST 节点与求值上下文。
// 求值规则严格按 Excel：错误优先传播、空单元格按 0/"" 参与、
// 文本型数字自动转换、文本 > 数字 的排序序。
// ---------------------------------------------------------------------------
#include <memory>
#include <vector>
#include <string>
#include <utility>
#include <vector>
#include "value.hpp"

namespace xl {

enum class BinOp {
    Add, Sub, Mul, Div, Pow, Concat,
    Eq, Ne, Lt, Gt, Le, Ge
};

enum class UnOp { Neg, Plus, Percent };

struct EvalCtx {
    virtual ~EvalCtx() = default;
    // sheet 为空表示当前表
    virtual Value cell(const std::string& sheet, int col, int row) = 0;
    virtual std::string sheetName() const { return ""; }
    // 当前正在求值的单元格（ROW()/COLUMN()/OFFSET 需要）；(-1,-1) 表示未知
    virtual void currentCell(int& col, int& row) const { col = -1; row = -1; }
    // 按 A1 文本取单元格（INDIRECT 需要）；不支持时返回 false
    virtual bool cellByAddr(const std::string& /*sheet*/, const std::string& /*addr*/, Value& /*out*/) {
        return false;
    }
    // 取单元格的原始公式文本（FORMULATEXT / ISFORMULA / CELL 需要）。
    // 常量格返回 false；不支持该能力时也返回 false。
    virtual bool cellFormula(int /*col*/, int /*row*/, std::string& /*out*/) const { return false; }
    // 取单元格地址文本（CELL("address") 需要）
    virtual bool cellAddr(int /*col*/, int /*row*/, std::string& /*out*/) const { return false; }
    // 已用区域（整列/整行引用求值时裁剪用）。没有数据时返回 false。
    // 稀疏表不可能真的展开 1048576 行，所以必须由存储层告诉调用方边界在哪。
    virtual bool usedRange(const std::string& /*sheet*/, int& /*c0*/, int& /*r0*/,
                           int& /*c1*/, int& /*r1*/) const {
        return false;
    }

    // 引用语义：FORMULATEXT / ISFORMULA / CELL 这类"元函数"要拿到引用本身，
    // 而不是引用求值后的值。eval 在调用它们之前把各参数的地址压栈。
    virtual void pushArgRefs(const std::vector<std::pair<int,int>>& /*refs*/) {}
    virtual void popArgRefs() {}
    virtual bool argRefAt(size_t /*idx*/, int& /*col*/, int& /*row*/) const { return false; }

    // LET / LAMBDA 的名字作用域
    virtual bool lookupName(const std::string& /*name*/, Value& /*out*/) const { return false; }
    // 工作簿级"定义名称"（命名区域）。与 LET 作用域是两套东西：
    //   LET 作用域 = 公式内部的临时绑定，随求值进出栈
    //   定义名称   = 存在 workbook.xml 里的持久命名，全表可用
    // 查找顺序 LET 优先 —— 否则公式内的 LET(x,...) 会被外部持久名称遮蔽。
    virtual bool definedName(const std::string& /*name*/, Value& /*out*/) { return false; }
    virtual void bindName(const std::string& /*name*/, const Value& /*v*/) {}
    virtual void unbindNames(size_t /*count*/) {}
    // 在给定绑定下求值 lambda 函数体（由 MAP/REDUCE 等驱动）
    virtual Value applyLambda(const LambdaDef& /*fn*/, const std::vector<Value>& /*args*/) {
        return Value::error(Err::Value);
    }
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

// LAMBDA 的完整定义放在 value.hpp 之后可见处
struct LambdaDef {
    std::vector<std::string> params;
    NodePtr body;
    std::string name;                 // 递归调用时绑定的自身名字
};

enum class NodeKind {
    Num, Str, Bool, ErrLit, CellRef, Range, Union, Intersect,
    Unary, Binary, Call, ArrayLit, Name, LambdaDefNode,
    Missing          // 空参数占位：`SORT(A1:A3,,-1)`、`VLOOKUP(x,A:B,2,)`
};

struct Node {
    NodeKind kind = NodeKind::Num;

    double num = 0.0;
    std::string text;                 // 函数名 / 字符串 / 表名
    bool bval = false;
    Err err = Err::Null;

    int col = 0, row = 0;
    bool colAbs = false, rowAbs = false;
    std::string sheet;
    // 整列（B:B / A:A）与整行（1:1）引用。
    // 语义上是 1048576 行 × 1 列，但稀疏存储下不可能真去展开 ——
    // 求值时裁剪到工作表的已用区域。见 eval.cpp 的说明。
    bool wholeCol = false, wholeRow = false;

    BinOp op = BinOp::Add;
    UnOp uop = UnOp::Neg;

    NodePtr a, b;
    NodePtr body;                     // LambdaDefNode 的函数体
    std::vector<std::string> paramNames;
    std::vector<NodePtr> kids;        // 参数 / 数组元素

    // 带递归深度守卫的求值入口，见 eval.cpp 的 kMaxEvalDepth。
    Value eval(EvalCtx& ctx) const;
};

NodePtr makeNum(double v);
NodePtr makeStr(const std::string& v);
NodePtr makeBool(bool v);
NodePtr makeErr(Err v);
NodePtr makeRef(const std::string& sheet, int col, int row, bool ca, bool ra);
NodePtr makeRange(NodePtr a, NodePtr b);
NodePtr makeUnion(NodePtr a, NodePtr b);
NodePtr makeIntersect(NodePtr a, NodePtr b);
NodePtr makeUnary(UnOp op, NodePtr a);
NodePtr makeBinary(BinOp op, NodePtr a, NodePtr b);
NodePtr makeCall(const std::string& name, std::vector<NodePtr> args);
NodePtr makeArray(std::vector<NodePtr> rows);
NodePtr makeName(const std::string& name);

// 把惰性区域引用展开成 Array2D（存储层用，见 eval.cpp）
Value materializeRangeValue(const Value& v, EvalCtx& ctx);
NodePtr makeLambdaNode(std::vector<std::string> params, NodePtr body);

// ctx 用于展开惰性区域引用；不需要时传 nullptr。
// 展开直接做在函数里，不再另包一层 —— 求值递归深度等于 AST 深度，
// 每层多一次函数调用就会让"1+1+...（万项）"从能算变成段错误。
Value evalBinary(BinOp op, const Value& x, const Value& y, EvalCtx* ctx = nullptr);
Value evalUnary(UnOp op, const Value& x, EvalCtx* ctx = nullptr);

} // namespace xl
