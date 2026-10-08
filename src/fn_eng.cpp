#include "functions.hpp"
#include "date.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <map>
#include <functional>

namespace xl {

static bool n1(const Value& v, double& out) {
    Value e; if (!toNumber(v, out, e)) return false; return true;
}
static bool i1(const Value& v, int& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    double d; Value e; if (!toNumber(v, d, e)) { err = e; return false; }
    out = (int)std::floor(d); return true;
}



// ---------------------------------------------------------------------------
// 进制转换
// ---------------------------------------------------------------------------
// pad 的语义：
//   pad > 0  —— 补足到 pad 位（Excel 的 places 参数）
//   pad == 0 —— 未给 places：正数用**最少位数**，负数用 10 位补码
//
// 曾经统一按 10 位补零，于是 DEC2OCT(100) 得到 "0000000144"，
// 而 Excel 是 "144"。Excel 只在给了 places 时才补零；
// 但负数不给 places 时仍按 10 位补码显示（DEC2OCT(-1) = "7777777777"）。
// places 夹到 [1,10]；0 表示"未指定"，原样放行（见 toBaseStr 的说明）。
static int effPad(int p) { return p <= 0 ? 0 : std::min(std::max(p, 1), 10); }
static std::string toBaseStr(long long v, int radix, int pad) {
    static const char* d = "0123456789ABCDEF";
    bool neg = v < 0;
    if (pad == 0) pad = neg ? 10 : 0;
    unsigned long long u;
    if (neg) {
        // Excel 用补码表示负数
        unsigned long long mod = 1ULL << (pad * 3 > 40 ? 40 : pad * 3);
        u = mod - (unsigned long long)(-(v + 1)) - 1;
        if (radix == 2) u = (1ULL << pad) - (unsigned long long)(-v);
        else if (radix == 8) u = (1ULL << (pad * 3)) - (unsigned long long)(-v);
        else u = (1ULL << (pad * 4)) - (unsigned long long)(-v);
    } else u = (unsigned long long)v;
    std::string r;
    do { r.insert(r.begin(), d[u % radix]); u /= radix; } while (u);
    if (pad > 0) {
        if ((int)r.size() < pad) r.insert(0, pad - r.size(), neg ? (radix == 16 ? 'F' : (radix == 8 ? '7' : '1')) : '0');
        else if ((int)r.size() > pad) r = r.substr(r.size() - pad);
    }
    return r;
}
static long long fromBaseStr(const std::string& s, int radix) {
    // Excel 规则：只有达到 10 位宽度且首位是"高位数字"时才按补码解释为负数。
    // 否则按无符号正数处理 —— 所以 BIN2DEC("1010")=10 而 BIN2DEC("1111111111")=-1。
    bool negative = false;
    if (s.size() == 10) {
        char c0 = s[0];
        int d0;
        if (std::isdigit((unsigned char)c0)) d0 = c0 - '0';
        else d0 = std::toupper((unsigned char)c0) - 'A' + 10;
        if (d0 >= radix / 2) negative = true;
    }
    size_t bits = s.size() * (radix == 2 ? 1 : (radix == 8 ? 3 : 4));
    unsigned long long u = 0;
    for (char c : s) {
        int d;
        if (std::isdigit((unsigned char)c)) d = c - '0';
        else if (std::isalpha((unsigned char)c)) d = std::toupper((unsigned char)c) - 'A' + 10;
        else return 0;
        if (d >= radix) return 0;
        u = u * radix + d;
    }
    if (!negative) return (long long)u;
    if (bits >= 64) return (long long)u;
    unsigned long long sign = 1ULL << (bits - 1);
    if (u & sign) return (long long)(u) - (long long)(sign << 1);
    return (long long)u;
}

// ===========================================================================
// 工程函数
// ===========================================================================
struct EngRegistrar {
    EngRegistrar() {
        // --- 贝塞尔 ---
        registerFunction("BESSELI", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, n; if (!n1(a[0], x) || !n1(a[1], n)) return Value::error(Err::Value);
            if (n < 0 || std::floor(n) != n) return Value::error(Err::Num);
            return Value::num(std::cyl_bessel_i(n, x));
        });
        registerFunction("BESSELJ", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, n; if (!n1(a[0], x) || !n1(a[1], n)) return Value::error(Err::Value);
            if (n < 0 || std::floor(n) != n) return Value::error(Err::Num);
            return Value::num(std::cyl_bessel_j(n, x));
        });
        registerFunction("BESSELK", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, n; if (!n1(a[0], x) || !n1(a[1], n)) return Value::error(Err::Value);
            if (n < 0 || std::floor(n) != n || x <= 0) return Value::error(Err::Num);
            return Value::num(std::cyl_bessel_k(n, x));
        });
        registerFunction("BESSELY", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, n; if (!n1(a[0], x) || !n1(a[1], n)) return Value::error(Err::Value);
            if (n < 0 || std::floor(n) != n || x <= 0) return Value::error(Err::Num);
            return Value::num(std::cyl_neumann(n, x));
        });

        // --- 误差函数 ---
        registerFunction("ERF", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (a.size() == 1) {
                double x; if (!n1(a[0], x)) return Value::error(Err::Value);
                return Value::num(std::erf(x));
            }
            double lo, hi;
            if (!n1(a[0], lo) || !n1(a[1], hi)) return Value::error(Err::Value);
            return Value::num(std::erf(hi) - std::erf(lo));
        });
        registerFunction("ERF.PRECISE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            return Value::num(std::erf(x));
        });
        registerFunction("ERFC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            return Value::num(std::erfc(x));
        });
        registerFunction("ERFC.PRECISE", 1, 1, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("ERFC")->impl(a, c); });
        registerFunction("DELTA", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            double y = 0;
            if (a.size() >= 2 && !n1(a[1], y)) return Value::error(Err::Value);
            return Value::num(std::fabs(x - y) < 1e-12 ? 1.0 : 0.0);
        });
        registerFunction("GESTEP", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            double y = 0;
            if (a.size() >= 2 && !n1(a[1], y)) return Value::error(Err::Value);
            return Value::num(x >= y ? 1.0 : 0.0);
        });

        // --- 复数 ---
        registerFunction("COMPLEX", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double re, im;
            if (!n1(a[0], re) || !n1(a[1], im)) return Value::error(Err::Value);
            std::string suf = "i";
            if (a.size() >= 3) {
                Value s; std::string t; Value e;
                if (!toText(a[2], t, e)) return e;
                if (t == "i" || t == "j") suf = t; else return Value::error(Err::Value);
            }
            return Value::str(cxStr(Cx{re, im}, suf));
        });
        registerFunction("IMREAL", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            return Value::num(c.re);
        });
        registerFunction("IMAGINARY", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            return Value::num(c.im);
        });
        registerFunction("IMABS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            return Value::num(std::sqrt(c.re*c.re + c.im*c.im));
        });
        registerFunction("IMARGUMENT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            if (c.re == 0 && c.im == 0) return Value::error(Err::Div0);
            return Value::num(std::atan2(c.im, c.re));
        });
        registerFunction("IMCONJUGATE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            return Value::str(cxStr(Cx{c.re, -c.im}));
        });
        registerFunction("IMSUM", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx acc;
            for (auto& x : a) { Cx c; if (!parseCx(x, c)) return Value::error(Err::Num); acc.re += c.re; acc.im += c.im; }
            return Value::str(cxStr(acc));
        });
        registerFunction("IMSUB", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx x, y;
            if (!parseCx(a[0], x) || !parseCx(a[1], y)) return Value::error(Err::Num);
            return Value::str(cxStr(Cx{x.re - y.re, x.im - y.im}));
        });
        registerFunction("IMPRODUCT", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx acc{1, 0};
            for (auto& v : a) {
                Cx c; if (!parseCx(v, c)) return Value::error(Err::Num);
                double r = acc.re * c.re - acc.im * c.im;
                double i2 = acc.re * c.im + acc.im * c.re;
                acc.re = r; acc.im = i2;
            }
            return Value::str(cxStr(acc));
        });
        registerFunction("IMDIV", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx x, y;
            if (!parseCx(a[0], x) || !parseCx(a[1], y)) return Value::error(Err::Num);
            double den = y.re*y.re + y.im*y.im;
            if (den == 0) return Value::error(Err::Num);
            return Value::str(cxStr(Cx{(x.re*y.re + x.im*y.im)/den, (x.im*y.re - x.re*y.im)/den}));
        });
        registerFunction("IMPOWER", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            double n; if (!n1(a[1], n)) return Value::error(Err::Value);
            double r = std::sqrt(c.re*c.re + c.im*c.im);
            double th = std::atan2(c.im, c.re);
            double rn = std::pow(r, n);
            return Value::str(cxStr(Cx{rn * std::cos(n*th), rn * std::sin(n*th)}));
        });
        registerFunction("IMSQRT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            double r = std::sqrt(c.re*c.re + c.im*c.im);
            double re = std::sqrt((r + c.re) / 2.0);
            double im = std::sqrt((r - c.re) / 2.0) * (c.im < 0 ? -1 : 1);
            return Value::str(cxStr(Cx{re, im}));
        });
        registerFunction("IMEXP", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            double e = std::exp(c.re);
            return Value::str(cxStr(Cx{e * std::cos(c.im), e * std::sin(c.im)}));
        });
        registerFunction("IMLN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            if (c.re == 0 && c.im == 0) return Value::error(Err::Num);
            double r = std::sqrt(c.re*c.re + c.im*c.im);
            return Value::str(cxStr(Cx{std::log(r), std::atan2(c.im, c.re)}));
        });
        registerFunction("IMLOG10", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            if (c.re == 0 && c.im == 0) return Value::error(Err::Num);
            double r = std::sqrt(c.re*c.re + c.im*c.im);
            double th = std::atan2(c.im, c.re);
            return Value::str(cxStr(Cx{std::log10(r), th / std::log(10.0)}));
        });
        registerFunction("IMLOG2", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            if (c.re == 0 && c.im == 0) return Value::error(Err::Num);
            double r = std::sqrt(c.re*c.re + c.im*c.im);
            double th = std::atan2(c.im, c.re);
            return Value::str(cxStr(Cx{std::log2(r), th / std::log(2.0)}));
        });
        registerFunction("IMSIN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            return Value::str(cxStr(Cx{std::sin(c.re)*std::cosh(c.im), std::cos(c.re)*std::sinh(c.im)}));
        });
        registerFunction("IMCOS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            return Value::str(cxStr(Cx{std::cos(c.re)*std::cosh(c.im), -std::sin(c.re)*std::sinh(c.im)}));
        });

        // --- 进制 ---
        registerFunction("DEC2BIN", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n, p = 0; Value e;
            if (!i1(a[0], n, e)) return e;
            if (a.size() >= 2 && !i1(a[1], p, e)) return e;
            if (n < -512 || n > 511) return Value::error(Err::Num);
            return Value::str(toBaseStr(n, 2, effPad(p)));
        });
        registerFunction("DEC2OCT", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n, p = 0; Value e;
            if (!i1(a[0], n, e)) return e;
            if (a.size() >= 2 && !i1(a[1], p, e)) return e;
            if (n < -536870912 || n > 536870911) return Value::error(Err::Num);
            return Value::str(toBaseStr(n, 8, effPad(p)));
        });
        registerFunction("DEC2HEX", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            long long n; double d;
            if (!n1(a[0], d)) return Value::error(Err::Value);
            n = (long long)std::floor(d);
            int p = 0; Value e;      // 0 = 未给 places，见 toBaseStr 的说明
            if (a.size() >= 2 && !i1(a[1], p, e)) return e;
            if (n < -549755813888LL || n > 549755813887LL) return Value::error(Err::Num);
            return Value::str(toBaseStr(n, 16, effPad(p)));
        });
        registerFunction("BIN2DEC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string s; Value e;
            if (!toText(a[0], s, e)) return e;
            if (s.empty() || s.size() > 10) return Value::error(Err::Num);
            for (char c : s) if (c != '0' && c != '1') return Value::error(Err::Num);
            return Value::num((double)fromBaseStr(s, 2));
        });
        registerFunction("OCT2DEC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string s; Value e;
            if (!toText(a[0], s, e)) return e;
            if (s.empty() || s.size() > 10) return Value::error(Err::Num);
            for (char c : s) if (c < '0' || c > '7') return Value::error(Err::Num);
            return Value::num((double)fromBaseStr(s, 8));
        });
        registerFunction("HEX2DEC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string s; Value e;
            if (!toText(a[0], s, e)) return e;
            if (s.empty() || s.size() > 10) return Value::error(Err::Num);
            return Value::num((double)fromBaseStr(s, 16));
        });
        registerFunction("BIN2HEX", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value d = findFunction("BIN2DEC")->impl({a[0]}, *(EvalCtx*)nullptr);
            if (d.isError()) return d;
            std::vector<Value> args = {d};
            if (a.size() >= 2) args.push_back(a[1]);
            return findFunction("DEC2HEX")->impl(args, *(EvalCtx*)nullptr);
        });
        registerFunction("BIN2OCT", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value d = findFunction("BIN2DEC")->impl({a[0]}, *(EvalCtx*)nullptr);
            if (d.isError()) return d;
            std::vector<Value> args = {d};
            if (a.size() >= 2) args.push_back(a[1]);
            return findFunction("DEC2OCT")->impl(args, *(EvalCtx*)nullptr);
        });
        registerFunction("OCT2BIN", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value d = findFunction("OCT2DEC")->impl({a[0]}, *(EvalCtx*)nullptr);
            if (d.isError()) return d;
            std::vector<Value> args = {d};
            if (a.size() >= 2) args.push_back(a[1]);
            return findFunction("DEC2BIN")->impl(args, *(EvalCtx*)nullptr);
        });
        registerFunction("OCT2HEX", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value d = findFunction("OCT2DEC")->impl({a[0]}, *(EvalCtx*)nullptr);
            if (d.isError()) return d;
            std::vector<Value> args = {d};
            if (a.size() >= 2) args.push_back(a[1]);
            return findFunction("DEC2HEX")->impl(args, *(EvalCtx*)nullptr);
        });
        registerFunction("HEX2BIN", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value d = findFunction("HEX2DEC")->impl({a[0]}, *(EvalCtx*)nullptr);
            if (d.isError()) return d;
            std::vector<Value> args = {d};
            if (a.size() >= 2) args.push_back(a[1]);
            return findFunction("DEC2BIN")->impl(args, *(EvalCtx*)nullptr);
        });
        registerFunction("HEX2OCT", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value d = findFunction("HEX2DEC")->impl({a[0]}, *(EvalCtx*)nullptr);
            if (d.isError()) return d;
            std::vector<Value> args = {d};
            if (a.size() >= 2) args.push_back(a[1]);
            return findFunction("DEC2OCT")->impl(args, *(EvalCtx*)nullptr);
        });

        // --- 位运算 ---
        auto bitop = [](std::function<long long(long long,long long)> f) -> FnImpl {
            return [f](const std::vector<Value>& a, EvalCtx&) -> Value {
                double x, y;
                if (!n1(a[0], x) || !n1(a[1], y)) return Value::error(Err::Value);
                if (x < 0 || y < 0 || x >= 281474976710656.0 || y >= 281474976710656.0)
                    return Value::error(Err::Num);
                if (std::floor(x) != x || std::floor(y) != y) return Value::error(Err::Num);
                return Value::num((double)f((long long)x, (long long)y));
            };
        };
        registerFunction("BITAND", 2, 2, bitop([](long long a, long long b) { return a & b; }));
        registerFunction("BITOR", 2, 2, bitop([](long long a, long long b) { return a | b; }));
        registerFunction("BITXOR", 2, 2, bitop([](long long a, long long b) { return a ^ b; }));
        registerFunction("BITLSHIFT", 2, 2, bitop([](long long a, long long b) {
            if (b < 0 || b > 53) return 0LL;
            return a << b;
        }));
        registerFunction("BITRSHIFT", 2, 2, bitop([](long long a, long long b) {
            if (b < 0 || b > 53) return 0LL;
            return a >> b;
        }));

        // --- 单位换算 ---
        registerFunction("CONVERT", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            std::string f, t; Value e;
            if (!toText(a[1], f, e)) return e;
            if (!toText(a[2], t, e)) return e;
            static const std::map<std::string, std::pair<std::string, double>> tbl = {
                // 长度（基准：米）
                {"m",{"L",1.0}}, {"km",{"L",1000.0}}, {"cm",{"L",0.01}}, {"mm",{"L",0.001}},
                {"mi",{"L",1609.344}}, {"yd",{"L",0.9144}}, {"ft",{"L",0.3048}}, {"in",{"L",0.0254}},
                {"nmi",{"L",1852.0}},
                // 重量（基准：千克）
                {"kg",{"M",1.0}}, {"g",{"M",0.001}}, {"mg",{"M",1e-6}}, {"t",{"M",1000.0}},
                {"lbm",{"M",0.45359237}}, {"ozm",{"M",0.028349523125}},
                // 时间（基准：秒）
                {"sec",{"T",1.0}}, {"min",{"T",60.0}}, {"hr",{"T",3600.0}}, {"day",{"T",86400.0}},
                {"yr",{"T",31557600.0}},
                // 体积（基准：立方米）
                {"m3",{"V",1.0}}, {"l",{"V",0.001}}, {"ml",{"V",1e-6}}, {"gal",{"V",0.003785411784}},
                {"qt",{"V",0.000946352946}},
                // 面积（基准：平方米）
                {"m2",{"A",1.0}}, {"ft2",{"A",0.09290304}}, {"in2",{"A",0.00064516}},
                // 温度（特殊处理）
                {"C",{"K",0.0}}, {"F",{"K",0.0}}, {"K",{"K",0.0}},
                // 力（基准：牛顿）
                {"N",{"F",1.0}}, {"dyn",{"F",1e-5}}, {"lbf",{"F",4.4482216152605}},
                // 能量（基准：焦耳）
                {"J",{"E",1.0}}, {"kJ",{"E",1000.0}}, {"cal",{"E",4.184}}, {"kcal",{"E",4184.0}},
                {"Wh",{"E",3600.0}}, {"kWh",{"E",3600000.0}}, {"eV",{"E",1.602176634e-19}},
                // 功率（基准：瓦）
                {"W",{"P",1.0}}, {"kW",{"P",1000.0}}, {"HP",{"P",745.6998715822702}},
                // 压力（基准：帕）
                {"Pa",{"Pr",1.0}}, {"kPa",{"Pr",1000.0}}, {"atm",{"Pr",101325.0}},
                {"mmHg",{"Pr",133.322387415}},
                // 速度（基准：m/s）
                {"m/s",{"S",1.0}}, {"km/h",{"S",1.0/3.6}}, {"mph",{"S",0.44704}}, {"kn",{"S",1852.0/3600.0}},
                // 信息（基准：bit）
                {"bit",{"I",1.0}}, {"byte",{"I",8.0}},
            };
            auto it1 = tbl.find(f), it2 = tbl.find(t);
            if (it1 == tbl.end() || it2 == tbl.end()) return Value::error(Err::NA);
            if (it1->second.first != it2->second.first) return Value::error(Err::NA);
            if (it1->second.first == "K") {
                double k;
                if (f == "C") k = x + 273.15;
                else if (f == "F") k = (x + 459.67) * 5.0/9.0;
                else k = x;
                if (t == "C") return Value::num(k - 273.15);
                if (t == "F") return Value::num(k * 9.0/5.0 - 459.67);
                return Value::num(k);
            }
            return Value::num(x * it1->second.second / it2->second.second);
        });
    }
};
static EngRegistrar g_engReg;

} // namespace xl
