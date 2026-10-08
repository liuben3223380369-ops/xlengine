#pragma once
// ---------------------------------------------------------------------------
// 值系统：Excel 语义的核心。
// 自研表格最容易做错、也最决定"像不像 Excel"的地方，不是函数数量，
// 而是这里的类型转换与错误传播规则。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace xl {

enum class Err { Null, Div0, Value, Ref, Name, Num, NA, Spill, Calc };

inline const char* errText(Err e) {
    switch (e) {
        case Err::Null:  return "#NULL!";
        case Err::Div0:  return "#DIV/0!";
        case Err::Value: return "#VALUE!";
        case Err::Ref:   return "#REF!";
        case Err::Name:  return "#NAME?";
        case Err::Num:   return "#NUM!";
        case Err::NA:    return "#N/A";
        case Err::Spill: return "#SPILL!";
        case Err::Calc:  return "#CALC!";
    }
    return "#UNKNOWN!";
}

struct Value;
using Array2D = std::vector<std::vector<Value>>;

// 惰性区域引用。
//
// 求值一个 A1:A100 这样的区域，原本要 materialize 成 Array2D：
// 一次 make_shared + 100 次 vector 增长 + 100 次 Value 拷贝。
// 实测 SUM(A1:A100) 单条 7.53us，而同样只碰几个格子的 A1*2 只要 0.15us ——
// 差额几乎全是这次展开。
//
// 改成只记坐标，由真正要遍历它的函数按需取格子。这样：
//   - 聚合类函数（SUM/COUNT/AVERAGE...）一次分配都不做
//   - 其它函数走 materializeIfRange()，行为与以前完全一致
struct RangeRefData {
    std::string sheet;
    int c0 = 0, r0 = 0, c1 = 0, r1 = 0;
};

// LAMBDA 定义：参数名 + 函数体。求值时把实参绑定到参数名再算 body。
struct LambdaDef;
using LambdaPtr = std::shared_ptr<LambdaDef>;

struct Value {
    enum class T { Empty, Num, Str, Bool, Error, Array, Lambda, RangeRef } t = T::Empty;
    double n = 0.0;
    std::string s;
    bool b = false;
    Err e = Err::Null;
    std::shared_ptr<Array2D> arr;
    LambdaPtr lambda;
    std::shared_ptr<RangeRefData> rng;

    static Value empty() { return Value{}; }
    static Value num(double v) { Value x; x.t = T::Num; x.n = v; return x; }
    static Value str(std::string v) { Value x; x.t = T::Str; x.s = std::move(v); return x; }
    static Value boolean(bool v) { Value x; x.t = T::Bool; x.b = v; return x; }
    static Value error(Err v) { Value x; x.t = T::Error; x.e = v; return x; }
    static Value array(std::shared_ptr<Array2D> v) { Value x; x.t = T::Array; x.arr = std::move(v); return x; }
    static Value lambdaV(LambdaPtr v) { Value x; x.t = T::Lambda; x.lambda = std::move(v); return x; }
    static Value rangeRef(std::string sheet, int c0, int r0, int c1, int r1) {
        Value x; x.t = T::RangeRef;
        x.rng = std::make_shared<RangeRefData>();
        x.rng->sheet = std::move(sheet);
        x.rng->c0 = c0; x.rng->r0 = r0; x.rng->c1 = c1; x.rng->r1 = r1;
        return x;
    }
    bool isLambda() const { return t == T::Lambda; }
    bool isRangeRef() const { return t == T::RangeRef; }

    bool isEmpty() const { return t == T::Empty; }
    bool isError() const { return t == T::Error; }
    bool isNum()   const { return t == T::Num; }
    bool isStr()   const { return t == T::Str; }
    bool isBool()  const { return t == T::Bool; }
    bool isArray() const { return t == T::Array; }
    // 惰性区域对上层应表现得像数组：凡是"这是个区域"的判断都要认它，
    // 否则 495 个函数里会有大量地方把区域当标量处理，静默算错。
    bool isArrayLike() const { return t == T::Array || t == T::RangeRef; }
};

// Excel 数字显示规则：15 位有效数字，整数不显示小数点
inline std::string numToText(double d) {
    if (std::isnan(d)) return "#NUM!";
    if (std::isinf(d)) return "#NUM!";
    if (d == 0.0) return "0";
    double r = std::floor(d + 0.5);
    if (r != 0 && std::fabs(d - r) < 1e-9 * std::max(1.0, std::fabs(d))) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(0) << r;
        return os.str();
    }
    std::ostringstream os;
    os << std::setprecision(15) << d;
    std::string s = os.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

// 数字解析：Excel 会接受文本型数字参与算术
inline bool textToNumber(const std::string& raw, double& out) {
    std::string s;
    for (char c : raw) if (!std::isspace((unsigned char)c)) s.push_back(c);
    if (s.empty()) return false;
    // 支持尾随百分号： "50%" -> 0.5
    bool pct = false;
    if (s.back() == '%') { pct = true; s.pop_back(); }
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size()) return false;
        out = pct ? v / 100.0 : v;
        return true;
    } catch (...) { return false; }
}

// 强制转数字（算术/比较语境）。失败返回错误值。
// 规则：Empty->0, Bool->1/0, Str->尝试解析, Error->原样传播, Array->取左上角
inline bool toNumber(const Value& v, double& out, Value& err) {
    switch (v.t) {
        case Value::T::Empty: out = 0.0; return true;
        case Value::T::Num:   out = v.n; return true;
        case Value::T::Bool:  out = v.b ? 1.0 : 0.0; return true;
        case Value::T::Str:
            if (textToNumber(v.s, out)) return true;
            err = Value::error(Err::Value); return false;
        case Value::T::Error: err = v; return false;
        case Value::T::Array:
            if (!v.arr || v.arr->empty() || (*v.arr)[0].empty()) { out = 0.0; return true; }
            return toNumber((*v.arr)[0][0], out, err);
        case Value::T::Lambda: err = Value::error(Err::Value); return false;
        // 惰性区域走到这里说明上游没展开它 —— 这是 plumping 错误，
        // 报 #VALUE! 让它可见，而不是静默当成 0
        case Value::T::RangeRef: err = Value::error(Err::Value); return false;
    }
    err = Value::error(Err::Value);
    return false;
}

// 强制转文本
inline bool toText(const Value& v, std::string& out, Value& err) {
    switch (v.t) {
        case Value::T::Empty: out.clear(); return true;
        case Value::T::Num:   out = numToText(v.n); return true;
        case Value::T::Str:   out = v.s; return true;
        case Value::T::Bool:  out = v.b ? "TRUE" : "FALSE"; return true;
        case Value::T::Error: err = v; return false;
        case Value::T::Array:
            if (!v.arr || v.arr->empty() || (*v.arr)[0].empty()) { out.clear(); return true; }
            return toText((*v.arr)[0][0], out, err);
        case Value::T::Lambda: out = "#LAMBDA"; return true;
        case Value::T::RangeRef: err = Value::error(Err::Value); return false;
    }
    err = Value::error(Err::Value);
    return false;
}

// 强制转布尔
inline bool toBool(const Value& v, bool& out, Value& err) {
    switch (v.t) {
        case Value::T::Empty: out = false; return true;
        case Value::T::Num:   out = v.n != 0.0; return true;
        case Value::T::Bool:  out = v.b; return true;
        case Value::T::Str: {
            std::string u = v.s;
            std::transform(u.begin(), u.end(), u.begin(), ::toupper);
            if (u == "TRUE") { out = true; return true; }
            if (u == "FALSE") { out = false; return true; }
            double d;
            if (textToNumber(v.s, d)) { out = d != 0.0; return true; }
            err = Value::error(Err::Value); return false;
        }
        case Value::T::Error: err = v; return false;
        case Value::T::Array:
            if (!v.arr || v.arr->empty() || (*v.arr)[0].empty()) { out = false; return true; }
            return toBool((*v.arr)[0][0], out, err);
        case Value::T::Lambda: err = Value::error(Err::Value); return false;
        case Value::T::RangeRef: err = Value::error(Err::Value); return false;
    }
    err = Value::error(Err::Value);
    return false;
}

// Excel 的排序序：数字 < 文本 < 逻辑值 < 错误值
inline int typeRank(const Value& v) {
    switch (v.t) {
        case Value::T::Num:   return 0;
        case Value::T::Str:   return 1;
        case Value::T::Bool:  return 2;
        case Value::T::Error: return 3;
        case Value::T::Empty: return 0;   // 空单元格按 0 参与
        case Value::T::Array: return 0;
        case Value::T::Lambda: return 1;  // 当作文本参与排序
        case Value::T::RangeRef: return 0;
    }
    return 0;
}

// 比较：返回 -1/0/1；若出错则 ok=false
inline bool compareValues(const Value& a, const Value& b, int& cmp, Value& err) {
    if (a.isError()) { err = a; return false; }
    if (b.isError()) { err = b; return false; }
    int ra = typeRank(a), rb = typeRank(b);
    if (ra != rb) { cmp = (ra < rb) ? -1 : 1; return true; }
    if (ra == 1) {  // 双方都是文本：大小写不敏感比较
        std::string x = a.s, y = b.s;
        std::transform(x.begin(), x.end(), x.begin(), ::toupper);
        std::transform(y.begin(), y.end(), y.begin(), ::toupper);
        cmp = x.compare(y);
        if (cmp < 0) cmp = -1; else if (cmp > 0) cmp = 1;
        return true;
    }
    if (ra == 2) {  // 逻辑值：FALSE < TRUE
        bool x, y; Value e;
        if (!toBool(a, x, e)) { err = e; return false; }
        if (!toBool(b, y, e)) { err = e; return false; }
        cmp = (x == y) ? 0 : (x ? 1 : -1);
        return true;
    }
    double x, y; Value e;
    if (!toNumber(a, x, e)) { err = e; return false; }
    if (!toNumber(b, y, e)) { err = e; return false; }
    if (x < y) cmp = -1; else if (x > y) cmp = 1; else cmp = 0;
    return true;
}

inline std::string valueToText(const Value& v) {
    switch (v.t) {
        case Value::T::Empty: return "";
        case Value::T::Num:   return numToText(v.n);
        case Value::T::Str:   return v.s;
        case Value::T::Bool:  return v.b ? "TRUE" : "FALSE";
        case Value::T::Error: return errText(v.e);
        case Value::T::Array:
            if (!v.arr || v.arr->empty() || (*v.arr)[0].empty()) return "";
            return valueToText((*v.arr)[0][0]);
        case Value::T::Lambda:
            return "#LAMBDA";      // 未绑定名称的 lambda 不能直接显示为值
        // 惰性区域不该活到显示层：所有存储/运算/非白名单函数入口都已展开它。
        // 真走到这里说明某个消费点漏了展开，给个可见的标记而不是空串。
        case Value::T::RangeRef:
            return "#REF!";
    }
    return "";
}

} // namespace xl
