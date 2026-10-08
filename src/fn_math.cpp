#include "functions.hpp"
#include "date.hpp"
#include <sstream>
#include <iomanip>
#include <random>

namespace xl {

static bool num1(const Value& v, double& out) {
    Value e;
    if (!toNumber(v, out, e)) return false;
    return true;
}
static Value nArg(const Value& v) {
    double d; Value e;
    if (!toNumber(v, d, e)) return e;
    return Value::num(d);
}
static bool int1(const Value& v, int& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    double d; Value e;
    if (!toNumber(v, d, e)) { err = e; return false; }
    out = (int)std::floor(d);
    return true;
}

static void collect(const std::vector<Value>& a, std::vector<double>& out, Value& err) {
    std::vector<Value> v;
    if (!flattenArgs(a, v, true, err)) return;
    for (auto& x : v) out.push_back(x.n);
}

// ===========================================================================
// 数学与三角
// ===========================================================================
struct MathRegistrar {
    MathRegistrar() {
        // --- 三角 ---
        auto trig = [](double (*f)(double)) -> FnImpl {
            return [f](const std::vector<Value>& a, EvalCtx&) -> Value {
                Value x = nArg(a[0]); if (x.isError()) return x;
                return Value::num(f(x.n));
            };
        };
        registerFunction("SIN", 1, 1, trig(std::sin));
        registerFunction("COS", 1, 1, trig(std::cos));
        registerFunction("TAN", 1, 1, trig(std::tan));
        registerFunction("SINH", 1, 1, trig(std::sinh));
        registerFunction("COSH", 1, 1, trig(std::cosh));
        registerFunction("TANH", 1, 1, trig(std::tanh));
        registerFunction("ASIN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            if (x.n < -1 || x.n > 1) return Value::error(Err::Num);
            return Value::num(std::asin(x.n));
        });
        registerFunction("ACOS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            if (x.n < -1 || x.n > 1) return Value::error(Err::Num);
            return Value::num(std::acos(x.n));
        });
        registerFunction("ATAN", 1, 1, trig(std::atan));
        registerFunction("ASINH", 1, 1, trig(std::asinh));
        registerFunction("ACOSH", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            if (x.n < 1) return Value::error(Err::Num);
            return Value::num(std::acosh(x.n));
        });
        registerFunction("ATANH", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            if (x.n <= -1 || x.n >= 1) return Value::error(Err::Num);
            return Value::num(std::atanh(x.n));
        });
        registerFunction("ATAN2", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            Value y = nArg(a[1]); if (y.isError()) return y;
            // Excel 的 ATAN2(x_num, y_num) 参数顺序与 C 相反
            if (x.n == 0 && y.n == 0) return Value::error(Err::Div0);
            return Value::num(std::atan2(y.n, x.n));
        });
        registerFunction("DEGREES", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            return Value::num(x.n * 180.0 / 3.14159265358979323846);
        });
        registerFunction("RADIANS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            return Value::num(x.n * 3.14159265358979323846 / 180.0);
        });

        // --- 取整 ---
        registerFunction("TRUNC", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            int d = 0;
            if (a.size() >= 2) { Value e1; if (!int1(a[1], d, e1)) return e1; }
            double f = std::pow(10.0, d);
            double y = x.n * f;
            return Value::num(std::trunc(y) / f);
        });
        registerFunction("CEILING", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            Value s = nArg(a[1]); if (s.isError()) return s;
            if (s.n == 0) return Value::num(0);
            if ((x.n > 0 && s.n < 0) || (x.n < 0 && s.n > 0)) return Value::error(Err::Num);
            return Value::num(std::ceil(x.n / s.n) * s.n);
        });
        registerFunction("FLOOR", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            Value s = nArg(a[1]); if (s.isError()) return s;
            if (s.n == 0) return Value::error(Err::Div0);
            if ((x.n > 0 && s.n < 0) || (x.n < 0 && s.n > 0)) return Value::error(Err::Num);
            return Value::num(std::floor(x.n / s.n) * s.n);
        });
        registerFunction("CEILING.MATH", 1, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            double step = 1; int mode = 0;
            if (a.size() >= 2) { Value s = nArg(a[1]); if (s.isError()) return s; step = s.n == 0 ? 1 : std::fabs(s.n); }
            if (a.size() >= 3) { Value e2; if (!int1(a[2], mode, e2)) return e2; }
            double v = std::ceil(x.n / step) * step;
            if (x.n < 0 && mode == 0) v = std::ceil(x.n / step) * step;   // 向零取整
            return Value::num(v);
        });
        registerFunction("FLOOR.MATH", 1, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            double step = 1; int mode = 0;
            if (a.size() >= 2) { Value s = nArg(a[1]); if (s.isError()) return s; step = s.n == 0 ? 1 : std::fabs(s.n); }
            if (a.size() >= 3) { Value e2; if (!int1(a[2], mode, e2)) return e2; }
            if (mode == 0) return Value::num(std::floor(x.n / step) * step);
            return Value::num(std::floor(x.n / (-step)) * (-step));       // 远离零
        });
        registerFunction("MROUND", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            Value m = nArg(a[1]); if (m.isError()) return m;
            if (m.n == 0) return Value::num(0);
            if ((x.n > 0 && m.n < 0) || (x.n < 0 && m.n > 0)) return Value::error(Err::Num);
            return Value::num(std::round(x.n / m.n) * m.n);
        });
        registerFunction("EVEN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            double v = std::ceil(std::fabs(x.n) / 2.0) * 2.0;
            return Value::num(x.n < 0 ? -v : v);
        });
        registerFunction("ODD", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            double v = (std::ceil((std::fabs(x.n) - 1) / 2.0) * 2.0) + 1.0;
            return Value::num(x.n < 0 ? -v : v);
        });

        // --- 组合与数论 ---
        registerFunction("FACT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            if (x.n < 0 || std::floor(x.n) != x.n) return Value::error(Err::Num);
            double r = 1; for (int i = 2; i <= (int)x.n; i++) r *= i;
            return Value::num(r);
        });
        registerFunction("FACTDOUBLE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            if (x.n < -1 || std::floor(x.n) != x.n) return Value::error(Err::Num);
            double r = 1; for (int i = (int)x.n; i > 0; i -= 2) r *= i;
            return Value::num(r);
        });
        registerFunction("GCD", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> v; Value e;
            collect(a, v, e); if (e.isError()) return e;
            if (v.empty()) return Value::num(0);
            long long g = (long long)std::fabs(v[0]);
            for (size_t i = 1; i < v.size(); i++) {
                long long x = (long long)std::fabs(v[i]);
                while (x) { long long t = g % x; g = x; x = t; }
            }
            return Value::num((double)g);
        });
        registerFunction("LCM", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> v; Value e;
            collect(a, v, e); if (e.isError()) return e;
            if (v.empty()) return Value::num(0);
            long long l = (long long)std::fabs(v[0]);
            for (size_t i = 1; i < v.size(); i++) {
                long long x = (long long)std::fabs(v[i]);
                if (l == 0 || x == 0) { l = 0; break; }
                long long g = l, xx = x;
                while (xx) { long long t = g % xx; g = xx; xx = t; }
                l = l / g * x;
            }
            return Value::num((double)l);
        });
        registerFunction("COMBIN", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n, k; Value e;
            if (!int1(a[0], n, e)) return e;
            if (!int1(a[1], k, e)) return e;
            if (n < 0 || k < 0 || k > n) return Value::error(Err::Num);
            double r = 1;
            k = std::min(k, n - k);
            for (int i = 1; i <= k; i++) r = r * (n - k + i) / i;
            return Value::num(std::floor(r + 0.5));
        });
        registerFunction("COMBINA", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n, k; Value e;
            if (!int1(a[0], n, e)) return e;
            if (!int1(a[1], k, e)) return e;
            if (n < 1 || k < 0) return Value::error(Err::Num);
            return Value::num(COMBIN_impl(n + k - 1, k));
        });
        registerFunction("PERMUT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n, k; Value e;
            if (!int1(a[0], n, e)) return e;
            if (!int1(a[1], k, e)) return e;
            if (n < 0 || k < 0 || k > n) return Value::error(Err::Num);
            double r = 1; for (int i = 0; i < k; i++) r *= (n - i);
            return Value::num(r);
        });
        registerFunction("PERMUTATIONA", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double n, k; Value e;
            if (!num1(a[0], n)) return Value::error(Err::Value);
            if (!num1(a[1], k)) return Value::error(Err::Value);
            if (n < 0 || k < 0) return Value::error(Err::Num);
            return Value::num(std::pow(n, k));
        });
        registerFunction("MULTINOMIAL", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> v; Value e;
            collect(a, v, e); if (e.isError()) return e;
            // (Σx)! / (x1!·x2!·...) —— 先乘总阶乘，再逐项除，避免溢出
            double sum = 0; for (auto x : v) { if (x < 0) return Value::error(Err::Num); sum += x; }
            double r = 1;
            for (int i = 2; i <= (int)sum; i++) r *= i;
            for (auto x : v) { for (int i = 2; i <= (int)x; i++) r /= i; }
            return Value::num(r);
        });
        registerFunction("QUOTIENT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            Value y = nArg(a[1]); if (y.isError()) return y;
            if (y.n == 0) return Value::error(Err::Div0);
            return Value::num(std::trunc(x.n / y.n));
        });

        // --- 求和/级数 ---
        registerFunction("SUMPRODUCT", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<double>> cols;
            size_t n = 0;
            for (auto& arg : a) {
                std::vector<Value> raw; Value e;
                if (!flattenArgs({arg}, raw, true, e)) return e;
                std::vector<double> v; for (auto& x : raw) v.push_back(x.n);
                if (n == 0) n = v.size();
                else if (v.size() != n) return Value::error(Err::Value);
                cols.push_back(v);
            }
            double s = 0;
            for (size_t i = 0; i < n; i++) {
                double p = 1; for (auto& c : cols) p *= c[i];
                s += p;
            }
            return Value::num(s);
        });
        registerFunction("SUMXMY2", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> x, y; Value e;
            collect({a[0]}, x, e); if (e.isError()) return e;
            collect({a[1]}, y, e); if (e.isError()) return e;
            if (x.size() != y.size()) return Value::error(Err::NA);
            double s = 0; for (size_t i = 0; i < x.size(); i++) s += (x[i]-y[i])*(x[i]-y[i]);
            return Value::num(s);
        });
        registerFunction("SUMX2MY2", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> x, y; Value e;
            collect({a[0]}, x, e); if (e.isError()) return e;
            collect({a[1]}, y, e); if (e.isError()) return e;
            if (x.size() != y.size()) return Value::error(Err::NA);
            double s = 0; for (size_t i = 0; i < x.size(); i++) s += x[i]*x[i] - y[i]*y[i];
            return Value::num(s);
        });
        registerFunction("SUMX2PY2", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> x, y; Value e;
            collect({a[0]}, x, e); if (e.isError()) return e;
            collect({a[1]}, y, e); if (e.isError()) return e;
            if (x.size() != y.size()) return Value::error(Err::NA);
            double s = 0; for (size_t i = 0; i < x.size(); i++) s += x[i]*x[i] + y[i]*y[i];
            return Value::num(s);
        });
        registerFunction("SERIESSUM", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, n, m; Value e;
            if (!num1(a[0], x) || !num1(a[1], n) || !num1(a[2], m)) return Value::error(Err::Value);
            std::vector<double> coef;
            collect({a[3]}, coef, e); if (e.isError()) return e;
            double s = 0;
            for (size_t i = 0; i < coef.size(); i++) s += coef[i] * std::pow(x, n + i * m);
            return Value::num(s);
        });
        registerFunction("SQRTPI", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            Value x = nArg(a[0]); if (x.isError()) return x;
            if (x.n < 0) return Value::error(Err::Num);
            return Value::num(std::sqrt(x.n * 3.14159265358979323846));
        });
        registerFunction("LOG", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = nArg(a[0]); if (x.isError()) return x;
            if (x.n <= 0) return Value::error(Err::Num);
            if (a.size() == 1) return Value::num(std::log10(x.n));
            Value b = nArg(a[1]); if (b.isError()) return b;
            if (b.n <= 0 || b.n == 1) return Value::error(Err::Num);
            return Value::num(std::log(x.n) / std::log(b.n));
        });

        // --- 随机 ---
        registerFunction("RANDBETWEEN", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int lo, hi; Value e;
            if (!int1(a[0], lo, e)) return e;
            if (!int1(a[1], hi, e)) return e;
            if (lo > hi) { int t = lo; lo = hi; hi = t; }
            static std::mt19937 gen(987654321);
            std::uniform_int_distribution<int> d(lo, hi);
            return Value::num((double)d(gen));
        }, true);

        // --- 进制 ---
        registerFunction("DECIMAL", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string t; Value e;
            if (!toText(a[0], t, e)) return e;
            int radix; Value e3; if (!int1(a[1], radix, e3)) return e3;
            if (radix < 2 || radix > 36) return Value::error(Err::Num);
            unsigned long long v = 0;
            for (char c : t) {
                int d;
                if (std::isdigit((unsigned char)c)) d = c - '0';
                else if (std::isalpha((unsigned char)c)) d = std::toupper((unsigned char)c) - 'A' + 10;
                else return Value::error(Err::Num);
                if (d >= radix) return Value::error(Err::Num);
                v = v * radix + d;
            }
            return Value::num((double)v);
        });
        registerFunction("BASE", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n, radix, len = 0; Value e;
            if (!int1(a[0], n, e)) return e;
            if (!int1(a[1], radix, e)) return e;
            Value e4; if (a.size() >= 3 && !int1(a[2], len, e4)) return e4;
            if (n < 0 || radix < 2 || radix > 36 || len < 0) return Value::error(Err::Num);
            static const char* dg = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            std::string r;
            do { r.insert(r.begin(), dg[n % radix]); n /= radix; } while (n);
            if ((int)r.size() < len) r.insert(0, len - r.size(), '0');
            return Value::str(r);
        });
        registerFunction("ROMAN", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n; Value e;
            if (!int1(a[0], n, e)) return e;
            if (n < 0 || n > 3999) return Value::error(Err::Value);
            static const int val[]  = {1000,900,500,400,100,90,50,40,10,9,5,4,1};
            static const char* sy[] = {"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"};
            std::string out;
            for (int i = 0; i < 13; i++) while (n >= val[i]) { out += sy[i]; n -= val[i]; }
            return Value::str(out);
        });
        registerFunction("ARABIC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string t; Value e;
            if (!toText(a[0], t, e)) return e;
            std::transform(t.begin(), t.end(), t.begin(), ::toupper);
            static const char* sy[] = {"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"};
            static const int  val[] = {1000,900,500,400,100,90,50,40,10,9,5,4,1};
            int out = 0, pos = 0;
            for (int i = 0; i < 13; i++) {
                while (t.compare(pos, std::string(sy[i]).size(), sy[i]) == 0) {
                    out += val[i]; pos += (int)std::string(sy[i]).size();
                }
            }
            if (pos != (int)t.size()) return Value::error(Err::Value);
            return Value::num((double)out);
        });
    }

    static double COMBIN_impl(int n, int k) {
        if (n < 0 || k < 0 || k > n) return 0;
        double r = 1;
        k = std::min(k, n - k);
        for (int i = 1; i <= k; i++) r = r * (n - k + i) / i;
        return std::floor(r + 0.5);
    }
};
static MathRegistrar g_mathReg;

} // namespace xl
