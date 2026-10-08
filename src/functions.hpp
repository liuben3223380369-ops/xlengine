#pragma once
// ---------------------------------------------------------------------------
// 函数库。先落一批核心函数验证架构，后续按类别批量扩展。
// 语义尽量对齐 Excel，而非"看起来能算"。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <iomanip>
#include "value.hpp"

namespace xl {

struct EvalCtx;
using FnImpl = std::function<Value(const std::vector<Value>&, EvalCtx&)>;

struct FnInfo {
    std::string name;
    int minArgs;
    int maxArgs;      // -1 表示可变
    bool isVolatile = false;
    FnImpl impl;
};

const FnInfo* findFunction(const std::string& upperName);
// 惰性遍历（供 fn_stat.cpp 等使用，语义与 eval.cpp 的 forEachNum 一致）
struct EvalCtx;
bool forEachNumStat(const std::vector<Value>& args, EvalCtx& ctx,
                    const std::function<void(double)>& cb, Value& err);
const std::vector<std::string>& functionNames();
// 审计用：重复注册的函数名列表。正常应为空，见 functions.cpp 的说明。
const std::vector<std::string>& duplicateRegistrations();
bool registerFunction(const std::string& name, int minArgs, int maxArgs, FnImpl impl, bool vol = false);

// 把区域展开成一维列表。
// - 区域内的文本 / 逻辑值 / 空单元格：按 ignoreNonNumbers 决定忽略还是报错
// - 错误值永远传播
bool flattenArgs(const std::vector<Value>& args, std::vector<Value>& out,
                 bool ignoreNonNumbers, Value& err);

// 收集所有值（含文本/逻辑/空），用于 COUNTA / 联合区域等
void flattenAll(const std::vector<Value>& args, std::vector<Value>& out, Value& err);

// ---------------------------------------------------------------------------
// 复数：Excel 用文本 "a+bi" / "a+bj" 表示。
// 放在这里是因为工程函数（fn_eng）与复数双曲（fn_fill2）都要用。
// ---------------------------------------------------------------------------
struct Cx { double re = 0, im = 0; };

inline bool parseCx(const Value& v, Cx& out) {
    std::string s; Value e;
    if (!toText(v, s, e)) return false;
    std::string t;
    for (char c : s) if (!std::isspace((unsigned char)c)) t.push_back(c);
    if (t.empty()) return false;
    char unit = 'i';
    for (char c : t) if (c == 'i' || c == 'j') { unit = c; break; }
    size_t split = std::string::npos;
    for (size_t k = 1; k < t.size(); k++)
        if ((t[k] == '+' || t[k] == '-') && t.find(unit, k) != std::string::npos) split = k;
    try {
        if (split == std::string::npos) {
            if (t.find(unit) != std::string::npos) {
                std::string iv = t.substr(0, t.find(unit));
                if (iv.empty() || iv == "+") out.im = 1;
                else if (iv == "-") out.im = -1;
                else out.im = std::stod(iv);
                out.re = 0;
            } else { out.re = std::stod(t); out.im = 0; }
            return true;
        }
        std::string rs = t.substr(0, split);
        std::string is = t.substr(split);
        out.re = rs.empty() ? 0 : std::stod(rs);
        size_t u = is.find(unit);
        std::string iv = is.substr(0, u);
        if (iv.empty() || iv == "+") out.im = 1;
        else if (iv == "-") out.im = -1;
        else out.im = std::stod(iv);
        return true;
    } catch (...) { return false; }
}

inline std::string cxStr(const Cx& c, const std::string& suffix = "i") {
    // 浮点噪声清理。复数运算（尤其 IMPOWER 走的极坐标公式）会留下 1e-16 量级的
    // 残余实部：不清理的话 IMPOWER("1+i",2) 显示成 "1.2246e-16+2i"，
    // 而 Excel 是 "2i"。
    auto clean = [](double x) { return std::fabs(x) < 1e-12 ? 0.0 : x; };
    double re = clean(c.re), im = clean(c.im);
    std::ostringstream a, b;
    a << std::setprecision(15) << re;
    double aim = std::fabs(im);
    b << std::setprecision(15) << aim;
    std::string sign = im < 0 ? "-" : "+";
    if (im == 0 && re == 0) return "0";
    if (im == 0) return a.str();
    // Excel 省略虚部系数 1：写 "2+i" 而不是 "2+1i"，写 "i" 而不是 "1i"。
    // 实部的 1 照常写出（IMCONJUGATE("1+i") = "1-i"）。
    std::string iv = (aim == 1.0) ? std::string() : b.str();
    if (re == 0) return (im < 0 ? "-" : "") + iv + suffix;
    return a.str() + sign + iv + suffix;
}

// 条件匹配（COUNTIF/SUMIF/MAXIFS 家族共用）：支持 >,>=,<,<=,<>,= 与 * ? 通配符
bool matchCrit(const Value& cellV, const std::string& crit, Value& err);

} // namespace xl
