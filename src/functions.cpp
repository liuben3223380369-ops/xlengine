#include "functions.hpp"
#include "utf8.hpp"
#include "ast.hpp"
#include <cmath>
#include <algorithm>
#include <random>
#include <map>
#include <sstream>
#include <iomanip>

namespace xl {

namespace {


// ---------------------------------------------------------------------------
// 惰性遍历
// ---------------------------------------------------------------------------
// SUM(A1:A100) 的开销原本有两处：
//   1) 区域 materialize 成 Array2D（make_shared + 每行 vector + 逐元素拷贝）
//   2) flattenArgs 再把结果摊进一个 vector<Value>
// 这两处都是为了"先攒起来再算"，而聚合函数根本不需要攒 —— 边遍历边累加即可。
//
// 语义与 flattenArgs / flattenAll 完全一致（区域内文本/逻辑/空忽略，
// 顶层真文本报 #VALUE!，错误值一律传播），只是不构造中间容器。
//
// 只有 eval.cpp 里 supportsLazyRange() 白名单中的函数拿得到未展开的
// RangeRef；其余函数在进函数前就已被展开，所以这两个辅助看到的 RangeRef
// 一定是"明确支持惰性"的那些。
template <class F>
bool forEachNum(const std::vector<Value>& args, EvalCtx& ctx, F&& cb, Value& err) {
    for (const Value& a : args) {
        if (a.isError()) { err = a; return false; }
        if (a.isRangeRef()) {
            const RangeRefData& r = *a.rng;
            long long rows = (long long)r.r1 - r.r0 + 1;
            long long cols = (long long)r.c1 - r.c0 + 1;
            if (rows <= 0 || cols <= 0) continue;
            if (rows * cols > 1048576LL) { err = Value::error(Err::Num); return false; }
            for (int y = r.r0; y <= r.r1; y++)
                for (int x = r.c0; x <= r.c1; x++) {
                    Value v = ctx.cell(r.sheet, x, y);
                    if (v.isError()) { err = v; return false; }
                    // 区域内的文本 / 逻辑值 / 空：忽略（Excel 行为）
                    if (v.isNum()) cb(v.n);
                }
            continue;
        }
        if (a.isArray()) {
            if (!a.arr) continue;
            for (const auto& row : *a.arr)
                for (const auto& v : row) {
                    if (v.isError()) { err = v; return false; }
                    if (v.isNum()) cb(v.n);
                }
            continue;
        }
        if (a.isNum()) { cb(a.n); continue; }
        if (a.isBool()) { cb(a.b ? 1.0 : 0.0); continue; }
        if (a.isEmpty()) continue;
        if (a.isStr()) {
            double d;
            if (textToNumber(a.s, d)) { cb(d); continue; }
            err = Value::error(Err::Value); return false;   // 顶层真文本
        }
    }
    return true;
}

// 与 flattenAll 对应：保留文本与空值（COUNTA / COUNTBLANK 需要区分它们）
template <class F>
bool forEachAny(const std::vector<Value>& args, EvalCtx& ctx, F&& cb, Value& err) {
    for (const Value& a : args) {
        if (a.isError() && !(a.isRangeRef())) { err = a; return false; }
        if (a.isRangeRef()) {
            const RangeRefData& r = *a.rng;
            long long rows = (long long)r.r1 - r.r0 + 1;
            long long cols = (long long)r.c1 - r.c0 + 1;
            if (rows <= 0 || cols <= 0) continue;
            if (rows * cols > 1048576LL) { err = Value::error(Err::Num); return false; }
            for (int y = r.r0; y <= r.r1; y++)
                for (int x = r.c0; x <= r.c1; x++) {
                    Value v = ctx.cell(r.sheet, x, y);
                    if (v.isError()) { err = v; return false; }
                    cb(v);
                }
            continue;
        }
        if (a.isArray()) {
            if (!a.arr) continue;
            for (const auto& row : *a.arr)
                for (const auto& v : row) {
                    if (v.isError()) { err = v; return false; }
                    cb(v);
                }
            continue;
        }
        cb(a);
    }
    return true;
}

} // namespace


static std::map<std::string, FnInfo>& registry() {
    static std::map<std::string, FnInfo> r;
    return r;
}
static std::vector<std::string>& nameList() {
    static std::vector<std::string> n;
    return n;
}

// 重复注册记录。
//
// 曾经的坑：同一个函数名在两个 .cpp 里各注册一次，registerFunction 用的是
// 覆盖语义（registry()[u] = ...），于是**谁生效取决于跨编译单元的静态初始化
// 顺序，而 C++ 并不保证这个顺序**。审计时发现两起真实事故：
//   - PERMUTATIONA：fn_stat2 里一个返回硬编码 0 的残桩覆盖了 fn_math 的真实实现
//   - F.DIST：fn_stat 的实现在 cumulative=FALSE 时返回 0（应是概率密度），
//             覆盖了 fn_fill3 的正确实现
// 两者都能通过"全函数冒烟测试"——因为返回 0 不崩溃、也不报未实现。
// 所以这里把重复登记下来，由测试断言必须为 0 条。
static std::vector<std::string> g_duplicates;
const std::vector<std::string>& duplicateRegistrations() { return g_duplicates; }

bool registerFunction(const std::string& name, int minArgs, int maxArgs, FnImpl impl, bool vol) {
    std::string u = name;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    if (registry().count(u)) {
        // 不在这里报错：静态初始化期间抛异常会直接终止进程，
        // 而且调用方也无从处理。登记下来交给测试去断言。
        g_duplicates.push_back(u);
    }
    registry()[u] = FnInfo{name, minArgs, maxArgs, vol, std::move(impl)};
    if (std::find(nameList().begin(), nameList().end(), u) == nameList().end())
        nameList().push_back(u);
    return true;
}
const FnInfo* findFunction(const std::string& upperName) {
    auto it = registry().find(upperName);
    return it == registry().end() ? nullptr : &it->second;
}
const std::vector<std::string>& functionNames() { return nameList(); }

// ---------------------------------------------------------------------------
// 参数展开
// ---------------------------------------------------------------------------
bool flattenArgs(const std::vector<Value>& args, std::vector<Value>& out,
                 bool ignoreNonNumbers, Value& err) {
    // 区域内的文本/逻辑值/空一律忽略（Excel 行为）；ignoreNonNumbers 保留给
    // 需要区分"容忍非数字"语义的调用方（如 MATCH 的查找区域）
    (void)ignoreNonNumbers;
    for (const auto& a : args) {
        if (a.isError()) { err = a; return false; }
        if (a.isArray()) {
            if (!a.arr) continue;
            for (const auto& row : *a.arr) {
                for (const auto& v : row) {
                    if (v.isError()) { err = v; return false; }
                    if (v.isNum()) out.push_back(v);
                    // 区域内的文本 / 逻辑值 / 空：聚合时忽略（Excel 行为）
                }
            }
        } else {
            // 顶层标量参数：文本型数字可转换，真文本报错
            if (a.isNum()) { out.push_back(a); continue; }
            if (a.isBool()) { out.push_back(Value::num(a.b ? 1.0 : 0.0)); continue; }
            if (a.isEmpty()) continue;
            if (a.isStr()) {
                double d;
                if (textToNumber(a.s, d)) { out.push_back(Value::num(d)); continue; }
                // 顶层直接给的真文本：Excel 报 #VALUE!（只有"区域内的文本"才被忽略）
                err = Value::error(Err::Value);
                return false;
            }
        }
    }
    return true;
}

// 收集"所有非错误值"用于 COUNTA / COUNTBLANK 等
void flattenAll(const std::vector<Value>& args, std::vector<Value>& out, Value& err) {
    for (const auto& a : args) {
        if (a.isError()) { err = a; return; }
        if (a.isArray()) {
            if (!a.arr) continue;
            for (const auto& row : *a.arr)
                for (const auto& v : row) {
                    if (v.isError()) { err = v; return; }
                    out.push_back(v);
                }
        } else out.push_back(a);
    }
}

static Value needNum(const Value& v) {
    double d; Value e;
    if (!toNumber(v, d, e)) return e;
    return Value::num(d);
}
static Value needText(const Value& v) {
    std::string s; Value e;
    if (!toText(v, s, e)) return e;
    return Value::str(s);
}

static double excelRound(double x, int digits) {
    double f = std::pow(10.0, digits);
    double y = x * f;
    if (y >= 0) return std::floor(y + 0.5) / f;
    return std::ceil(y - 0.5) / f;
}

// ---------------------------------------------------------------------------
// 注册全部函数
// ---------------------------------------------------------------------------
struct Registrar {
    Registrar() {
        // ---- 数学与聚合 ----
        registerFunction("SUM", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            double s = 0; Value e;
            if (!forEachNum(a, ctx, [&](double x){ s += x; }, e)) return e;
            return Value::num(s);
        });
        registerFunction("PRODUCT", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            double p = 1; Value e;
            if (!forEachNum(a, ctx, [&](double x){ p *= x; }, e)) return e;
            return Value::num(p);
        });
        registerFunction("SUMSQ", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            double s = 0; Value e;
            if (!forEachNum(a, ctx, [&](double x){ s += x * x; }, e)) return e;
            return Value::num(s);
        });
        registerFunction("AVERAGE", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            double s = 0, c = 0; Value e;
            if (!forEachNum(a, ctx, [&](double x){ s += x; c += 1; }, e)) return e;
            if (c == 0) return Value::error(Err::Div0);
            return Value::num(s / c);
        });
        registerFunction("COUNT", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            double c = 0; Value e;
            if (!forEachNum(a, ctx, [&](double){ c += 1; }, e)) return e;
            return Value::num(c);
        });
        registerFunction("COUNTA", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            size_t c = 0; Value e;
            if (!forEachAny(a, ctx, [&](const Value& x){ if (!x.isEmpty() && !(x.isStr() && x.s.empty())) c++; }, e)) return e;
            return Value::num((double)c);
        });
        registerFunction("COUNTBLANK", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            size_t c = 0; Value e;
            if (!forEachAny(a, ctx, [&](const Value& x){ if (x.isEmpty() || (x.isStr() && x.s.empty())) c++; }, e)) return e;
            return Value::num((double)c);
        });
        registerFunction("MIN", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            bool first = true; double m = 0; Value e;
            if (!forEachNum(a, ctx, [&](double x){ if (first) { m = x; first = false; } else m = std::min(m, x); }, e)) return e;
            return Value::num(first ? 0.0 : m);
        });
        registerFunction("MAX", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            bool first = true; double m = 0; Value e;
            if (!forEachNum(a, ctx, [&](double x){ if (first) { m = x; first = false; } else m = std::max(m, x); }, e)) return e;
            return Value::num(first ? 0.0 : m);
        });
        registerFunction("MEDIAN", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> d; Value e;
            if (!forEachNum(a, ctx, [&](double x){ d.push_back(x); }, e)) return e;
            if (d.empty()) return Value::error(Err::Num);
            std::sort(d.begin(), d.end());
            size_t n = d.size();
            return Value::num(n % 2 ? d[n/2] : (d[n/2 - 1] + d[n/2]) / 2.0);
        });
        registerFunction("STDEV", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            if (!flattenArgs(a, v, true, e)) return e;
            if (v.size() < 2) return Value::error(Err::Div0);
            double s = 0; for (auto& x : v) s += x.n;
            double m = s / v.size(), ss = 0;
            for (auto& x : v) ss += (x.n - m) * (x.n - m);
            return Value::num(std::sqrt(ss / (v.size() - 1)));
        });
        registerFunction("VAR", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            if (!flattenArgs(a, v, true, e)) return e;
            if (v.size() < 2) return Value::error(Err::Div0);
            double s = 0; for (auto& x : v) s += x.n;
            double m = s / v.size(), ss = 0;
            for (auto& x : v) ss += (x.n - m) * (x.n - m);
            return Value::num(ss / (v.size() - 1));
        });
        registerFunction("LARGE", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            if (!flattenArgs({a[0]}, v, true, e)) return e;
            Value k = needNum(a[1]); if (k.isError()) return k;
            int kk = (int)std::floor(k.n);
            if (kk < 1 || (size_t)kk > v.size()) return Value::error(Err::Num);
            std::vector<double> d; for (auto& x : v) d.push_back(x.n);
            std::sort(d.rbegin(), d.rend());
            return Value::num(d[kk - 1]);
        });
        registerFunction("SMALL", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            if (!flattenArgs({a[0]}, v, true, e)) return e;
            Value k = needNum(a[1]); if (k.isError()) return k;
            int kk = (int)std::floor(k.n);
            if (kk < 1 || (size_t)kk > v.size()) return Value::error(Err::Num);
            std::vector<double> d; for (auto& x : v) d.push_back(x.n);
            std::sort(d.begin(), d.end());
            return Value::num(d[kk - 1]);
        });

        registerFunction("ABS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return needNum(a[0]).isError() ? needNum(a[0]) : Value::num(std::fabs(needNum(a[0]).n)); });
        registerFunction("SQRT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            if (x.n < 0) return Value::error(Err::Num);
            return Value::num(std::sqrt(x.n));
        });
        registerFunction("POWER", 2, 2, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            Value y = needNum(a[1]); if (y.isError()) return y;
            errno = 0; double r = std::pow(x.n, y.n);
            if (std::isnan(r)) return Value::error(Err::Num);
            return Value::num(r);
        });
        registerFunction("EXP", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            return Value::num(std::exp(x.n));
        });
        registerFunction("LN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            if (x.n <= 0) return Value::error(Err::Num);
            return Value::num(std::log(x.n));
        });
        registerFunction("LOG10", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            if (x.n <= 0) return Value::error(Err::Num);
            return Value::num(std::log10(x.n));
        });
        registerFunction("INT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            return Value::num(std::floor(x.n));
        });
        registerFunction("SIGN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            return Value::num(x.n > 0 ? 1 : (x.n < 0 ? -1 : 0));
        });
        // Excel 的 MOD 与除数同号：MOD(-3,2)=1
        registerFunction("MOD", 2, 2, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            Value y = needNum(a[1]); if (y.isError()) return y;
            if (y.n == 0) return Value::error(Err::Div0);
            return Value::num(x.n - y.n * std::floor(x.n / y.n));
        });
        registerFunction("ROUND", 2, 2, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            Value d = needNum(a[1]); if (d.isError()) return d;
            return Value::num(excelRound(x.n, (int)std::floor(d.n)));
        });
        registerFunction("ROUNDUP", 2, 2, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            Value d = needNum(a[1]); if (d.isError()) return d;
            double f = std::pow(10.0, (int)std::floor(d.n));
            double y = x.n * f;
            return Value::num((y >= 0 ? std::ceil(y) : std::floor(y)) / f);
        });
        registerFunction("ROUNDDOWN", 2, 2, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = needNum(a[0]); if (x.isError()) return x;
            Value d = needNum(a[1]); if (d.isError()) return d;
            double f = std::pow(10.0, (int)std::floor(d.n));
            double y = x.n * f;
            return Value::num((y >= 0 ? std::floor(y) : std::ceil(y)) / f);
        });
        registerFunction("PI", 0, 0, [](const std::vector<Value>&, EvalCtx&) {
            return Value::num(3.14159265358979323846);
        });
        registerFunction("RAND", 0, 0, [](const std::vector<Value>&, EvalCtx&) -> Value {
            static std::mt19937 gen(12345);
            static std::uniform_real_distribution<double> dist(0.0, 1.0);
            return Value::num(dist(gen));
        }, true);

        // ---- 逻辑 ----
        registerFunction("IF", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (a[0].isError()) return a[0];
            bool c; Value e;
            if (!toBool(a[0], c, e)) return e;
            if (c) return a[1];
            return a.size() >= 3 ? a[2] : Value::boolean(false);
        });
        registerFunction("IFERROR", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            return a[0].isError() ? a[1] : a[0];
        });
        registerFunction("IFNA", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            return (a[0].isError() && a[0].e == Err::NA) ? a[1] : a[0];
        });
        registerFunction("AND", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flattenAll(a, v, e);
            if (e.isError()) return e;
            bool all = true;
            for (auto& x : v) {
                if (x.isEmpty()) continue;
                if (!x.isNum() && !x.isBool() && !x.isStr()) continue;
                bool b; Value ee;
                if (!toBool(x, b, ee)) return ee;
                if (!b) { all = false; break; }
            }
            return Value::boolean(all);
        });
        registerFunction("OR", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flattenAll(a, v, e);
            if (e.isError()) return e;
            bool any = false;
            for (auto& x : v) {
                if (x.isEmpty()) continue;
                if (!x.isNum() && !x.isBool() && !x.isStr()) continue;
                bool b; Value ee;
                if (!toBool(x, b, ee)) return ee;
                if (b) { any = true; break; }
            }
            return Value::boolean(any);
        });
        registerFunction("NOT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            bool b; Value e;
            if (!toBool(a[0], b, e)) return e;
            return Value::boolean(!b);
        });
        registerFunction("XOR", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flattenAll(a, v, e);
            if (e.isError()) return e;
            int cnt = 0;
            for (auto& x : v) {
                if (x.isEmpty()) continue;
                if (!x.isNum() && !x.isBool() && !x.isStr()) continue;
                bool b; Value ee;
                if (!toBool(x, b, ee)) return ee;
                if (b) cnt++;
            }
            return Value::boolean(cnt % 2 == 1);
        });
        registerFunction("TRUE", 0, 0, [](const std::vector<Value>&, EvalCtx&) { return Value::boolean(true); });
        registerFunction("FALSE", 0, 0, [](const std::vector<Value>&, EvalCtx&) { return Value::boolean(false); });

        // ---- 文本 ----
        registerFunction("LEN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = needText(a[0]); if (s.isError()) return s;
            // 按**字符**计数：LEN("中文abc") = 5，不是 9（UTF-8 字节数）
            return Value::num((double)utf::len(s.s));
        });
        registerFunction("UPPER", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = needText(a[0]); if (s.isError()) return s;
            std::string x = s.s; std::transform(x.begin(), x.end(), x.begin(), ::toupper);
            return Value::str(x);
        });
        registerFunction("LOWER", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = needText(a[0]); if (s.isError()) return s;
            std::string x = s.s; std::transform(x.begin(), x.end(), x.begin(), ::tolower);
            return Value::str(x);
        });
        registerFunction("TRIM", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = needText(a[0]); if (s.isError()) return s;
            std::string out; bool sp = false;
            for (char c : s.s) {
                if (std::isspace((unsigned char)c)) { sp = true; continue; }
                if (sp && !out.empty()) out.push_back(' ');
                sp = false; out.push_back(c);
            }
            return Value::str(out);
        });
        registerFunction("LEFT", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = needText(a[0]); if (s.isError()) return s;
            size_t n = 1;
            if (a.size() >= 2) {
                Value k = needNum(a[1]); if (k.isError()) return k;
                if (k.n < 0) return Value::error(Err::Value);
                n = (size_t)std::floor(k.n);
            }
            return Value::str(utf::slice(s.s, 0, n));
        });
        registerFunction("RIGHT", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = needText(a[0]); if (s.isError()) return s;
            size_t n = 1;
            if (a.size() >= 2) {
                Value k = needNum(a[1]); if (k.isError()) return k;
                if (k.n < 0) return Value::error(Err::Value);
                n = (size_t)std::floor(k.n);
            }
            size_t L = utf::len(s.s);
            if (n >= L) return Value::str(s.s);
            return Value::str(utf::slice(s.s, L - n, n));
        });
        registerFunction("MID", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = needText(a[0]); if (s.isError()) return s;
            Value st = needNum(a[1]); if (st.isError()) return st;
            Value ln = needNum(a[2]); if (ln.isError()) return ln;
            if (st.n < 1 || ln.n < 0) return Value::error(Err::Value);
            size_t p = (size_t)std::floor(st.n) - 1;
            if (p >= utf::len(s.s)) return Value::str("");
            return Value::str(utf::slice(s.s, p, (size_t)std::floor(ln.n)));
        });
        registerFunction("CONCAT", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string out;
            for (auto& x : a) {
                if (x.isError()) return x;
                std::string s; Value e;
                if (!toText(x, s, e)) return e;
                out += s;
            }
            return Value::str(out);
        });
        registerFunction("CONCATENATE", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string out;
            for (auto& x : a) {
                if (x.isError()) return x;
                std::string s; Value e;
                if (!toText(x, s, e)) return e;
                out += s;
            }
            return Value::str(out);
        });
        registerFunction("REPT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = needText(a[0]); if (s.isError()) return s;
            Value k = needNum(a[1]); if (k.isError()) return k;
            int n = (int)std::floor(k.n);
            if (n < 0) return Value::error(Err::Value);
            std::string out; for (int i = 0; i < n; i++) out += s.s;
            return Value::str(out);
        });
        registerFunction("VALUE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string s; Value e;
            if (!toText(a[0], s, e)) return e;
            double d;
            if (!textToNumber(s, d)) return Value::error(Err::Value);
            return Value::num(d);
        });
        registerFunction("FIND", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value n = needText(a[0]); if (n.isError()) return n;
            Value h = needText(a[1]); if (h.isError()) return h;
            size_t from = 0;
            if (a.size() >= 3) {
                Value f = needNum(a[2]); if (f.isError()) return f;
                if (f.n < 1) return Value::error(Err::Value);
                from = (size_t)std::floor(f.n) - 1;
            }
            size_t p = h.s.find(n.s, from);
            if (p == std::string::npos) return Value::error(Err::Value);
            return Value::num((double)(p + 1));
        });
        registerFunction("SUBSTITUTE", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value t = needText(a[0]); if (t.isError()) return t;
            Value o = needText(a[1]); if (o.isError()) return o;
            Value n = needText(a[2]); if (n.isError()) return n;
            std::string out = t.s;
            if (o.s.empty()) return Value::str(out);
            size_t pos = 0; int count = 0;
            while ((pos = out.find(o.s, pos)) != std::string::npos) {
                out.replace(pos, o.s.size(), n.s);
                pos += n.s.size();
                if (++count > 100000) break;
            }
            return Value::str(out);
        });

        // ---- 查找引用 ----
        registerFunction("INDEX", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (!a[0].isArray() || !a[0].arr) {
                if (a[0].isError()) return a[0];
                return Value::error(Err::Value);
            }
            auto& arr = *a[0].arr;
            Value r = needNum(a[1]); if (r.isError()) return r;
            int ri = (int)std::floor(r.n) - 1;
            int ci = 0;
            if (a.size() >= 3) {
                Value c = needNum(a[2]); if (c.isError()) return c;
                ci = (int)std::floor(c.n) - 1;
            }
            if (ri < 0 || ci < 0) return Value::error(Err::Value);
            // 只给一个序号时，对单行/单列数组按线性索引（Excel 行为）
            if (a.size() < 3) {
                size_t rows = arr.size();
                size_t cols = rows ? arr[0].size() : 0;
                if (rows == 1) {
                    if ((size_t)ri >= cols) return Value::error(Err::Ref);
                    return arr[0][ri];
                }
                if (cols == 1) {
                    if ((size_t)ri >= rows) return Value::error(Err::Ref);
                    return arr[ri][0];
                }
            }
            if ((size_t)ri >= arr.size()) return Value::error(Err::Ref);
            if ((size_t)ci >= arr[ri].size()) return Value::error(Err::Ref);
            return arr[ri][ci];
        });
        registerFunction("MATCH", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            if (!flattenArgs({a[1]}, v, false, e)) return e;
            for (size_t i = 0; i < v.size(); i++) {
                int cmp; Value ee;
                if (!compareValues(a[0], v[i], cmp, ee)) continue;
                if (cmp == 0) return Value::num((double)(i + 1));
            }
            return Value::error(Err::NA);
        });
        registerFunction("VLOOKUP", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (!a[1].isArray() || !a[1].arr) return Value::error(Err::Value);
            auto& arr = *a[1].arr;
            Value c = needNum(a[2]); if (c.isError()) return c;
            int ci = (int)std::floor(c.n) - 1;
            if (ci < 0) return Value::error(Err::Value);
            for (auto& row : arr) {
                int cmp; Value ee;
                if (row.empty()) continue;
                if (!compareValues(a[0], row[0], cmp, ee)) continue;
                if (cmp == 0) {
                    if ((size_t)ci >= row.size()) return Value::error(Err::Ref);
                    return row[ci];
                }
            }
            return Value::error(Err::NA);
        });
        registerFunction("HLOOKUP", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (!a[1].isArray() || !a[1].arr || a[1].arr->empty()) return Value::error(Err::Value);
            auto& arr = *a[1].arr;
            Value r = needNum(a[2]); if (r.isError()) return r;
            int ri = (int)std::floor(r.n) - 1;
            if (ri < 0) return Value::error(Err::Value);
            for (size_t c = 0; c < arr[0].size(); c++) {
                int cmp; Value ee;
                if (!compareValues(a[0], arr[0][c], cmp, ee)) continue;
                if (cmp == 0) {
                    if ((size_t)ri >= arr.size()) return Value::error(Err::Ref);
                    if (c >= arr[ri].size()) return Value::error(Err::Ref);
                    return arr[ri][c];
                }
            }
            return Value::error(Err::NA);
        });
        registerFunction("CHOOSE", 2, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value i = needNum(a[0]); if (i.isError()) return i;
            int k = (int)std::floor(i.n);
            if (k < 1 || k > (int)a.size() - 1) return Value::error(Err::Value);
            return a[k];
        });

        // ---- 信息 ----
        registerFunction("ISNUMBER", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return Value::boolean(a[0].isNum()); });
        registerFunction("ISTEXT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return Value::boolean(a[0].isStr()); });
        registerFunction("ISBLANK", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return Value::boolean(a[0].isEmpty()); });
        registerFunction("ISLOGICAL", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return Value::boolean(a[0].t == Value::T::Bool); });
        registerFunction("ISERROR", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return Value::boolean(a[0].isError()); });
        registerFunction("ISNA", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return Value::boolean(a[0].isError() && a[0].e == Err::NA); });
        registerFunction("NA", 0, 0, [](const std::vector<Value>&, EvalCtx&) { return Value::error(Err::NA); });
    }
};
static Registrar g_registrar;

// 对外暴露：fn_stat.cpp 的 col2 需要同一套惰性遍历语义
bool forEachNumStat(const std::vector<Value>& args, EvalCtx& ctx,
                    const std::function<void(double)>& cb, Value& err) {
    return forEachNum(args, ctx, cb, err);
}

} // namespace xl
