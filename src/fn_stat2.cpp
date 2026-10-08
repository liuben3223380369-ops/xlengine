#include "functions.hpp"
#include "date.hpp"
#include "dist.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>

namespace xl {

static bool n1(const Value& v, double& out) {
    Value e; if (!toNumber(v, out, e)) return false; return true;
}
static bool i1(const Value& v, int& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    double d; Value e; if (!toNumber(v, d, e)) { err = e; return false; }
    out = (int)std::floor(d); return true;
}
static void col2(const std::vector<Value>& a, std::vector<double>& out, Value& err) {
    std::vector<Value> v;
    if (!flattenArgs(a, v, true, err)) return;
    for (auto& x : v) out.push_back(x.n);
}
// 保留非数值（文本/空）的摊平，用于 COUNTIF 类
static void flatKeep(const Value& v, std::vector<Value>& out, Value& err) {
    if (v.isError()) { err = v; return; }
    if (v.isArray()) { if (v.arr) for (auto& r : *v.arr) for (auto& x : r) {
        if (x.isError()) { err = x; return; } out.push_back(x); } }
    else out.push_back(v);
}

// ---------------------------------------------------------------------------
// 最小二乘：解 (X'X) b = X'y，带部分选主元的高斯消元
// ---------------------------------------------------------------------------
static bool solveLinear(const std::vector<std::vector<double>>& A,
                        const std::vector<double>& b,
                        std::vector<double>& x) {
    size_t n = b.size();
    if (A.size() != n) return false;
    std::vector<std::vector<double>> M(n, std::vector<double>(n + 1));
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) M[i][j] = A[i][j];
        M[i][n] = b[i];
    }
    for (size_t c = 0; c < n; c++) {
        size_t piv = c;
        for (size_t r = c + 1; r < n; r++)
            if (std::fabs(M[r][c]) > std::fabs(M[piv][c])) piv = r;
        if (std::fabs(M[piv][c]) < 1e-14) return false;      // 奇异（共线）
        if (piv != c) std::swap(M[piv], M[c]);
        for (size_t r = 0; r < n; r++) {
            if (r == c) continue;
            double f = M[r][c] / M[c][c];
            for (size_t k = c; k <= n; k++) M[r][k] -= f * M[c][k];
        }
    }
    x.assign(n, 0);
    for (size_t i = 0; i < n; i++) x[i] = M[i][n] / M[i][i];
    return true;
}

// ===========================================================================
// 统计增强：回归、假设检验、更多分布
// ===========================================================================
struct Stat2Registrar {
    Stat2Registrar() {
        // --- 多元线性回归 LINEST ---
        registerFunction("LINEST", 1, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> y; Value e;
            col2({a[0]}, y, e); if (e.isError()) return e;
            if (y.empty()) return Value::error(Err::Value);
            size_t n = y.size();
            // 自变量矩阵：known_x's 可为单列或多列（按行摊平后按列数切分）
            size_t k = 1;
            std::vector<double> xflat;
            bool constOn = true;
            if (a.size() >= 2 && a[1].isArray() && a[1].arr) {
                auto& m = *a[1].arr;
                if (!m.empty()) {
                    // Excel 约定：known_x's 每"列"是一个自变量，每"行"是一次观测。
                    // 观测数必须与 known_y's 一致；单行水平数组则视为单变量。
                    size_t xr = m.size(), xc = m[0].size();
                    if (xr != n && !(xr == 1 && xc == n)) return Value::error(Err::Value);
                    k = (xr == n) ? xc : 1;
                    for (auto& row : m)
                        for (auto& c : row) {
                            if (c.isError()) return c;
                            double d; if (!n1(c, d)) return Value::error(Err::Value);
                            xflat.push_back(d);
                        }
                }
            } else if (a.size() >= 2) {
                col2({a[1]}, xflat, e); if (e.isError()) return e;
            }
            if (a.size() >= 3) { Value c; int ci; if (!i1(a[2], ci, c)) return c; constOn = (ci != 0); }
            if (xflat.empty()) {
                // 退化为一元：x = 1..n
                for (size_t i = 0; i < n; i++) xflat.push_back((double)(i + 1));
                k = 1;
            }
            size_t rows = xflat.size() / k;
            if (rows != n) return Value::error(Err::Value);
            size_t p = k + (constOn ? 1 : 0);
            // 正规方程
            std::vector<std::vector<double>> XtX(p, std::vector<double>(p, 0.0));
            std::vector<double> Xty(p, 0.0);
            for (size_t r = 0; r < n; r++) {
                std::vector<double> row(p, 1.0);
                for (size_t j = 0; j < k; j++) row[constOn ? j + 1 : j] = xflat[r * k + j];
                if (constOn) row[0] = 1.0;
                for (size_t i = 0; i < p; i++) {
                    Xty[i] += row[i] * y[r];
                    for (size_t j = 0; j < p; j++) XtX[i][j] += row[i] * row[j];
                }
            }
            std::vector<double> beta;
            if (!solveLinear(XtX, Xty, beta)) return Value::error(Err::Num);
            // Excel 的系数顺序是**逆序**：最后一个自变量的系数排最前，
            // 第一个自变量的系数排其后，截距在最后。
            //
            // 原来写成正序（beta[1], beta[2], ..., 截距），单变量时看不出差别
            // （只有一个斜率），多变量时前后颠倒。用 SciPy 的 lstsq 对照才暴露：
            //   y = 1·x1 + 2·x2 + 3
            //   引擎输出 {1, 2, 3}    ← 正序
            //   Excel 应为 {2, 1, 3}  ← 逆序
            // LOGEST 复用本函数，一并修正。
            auto arr = std::make_shared<Array2D>();
            std::vector<Value> out;
            if (constOn) {
                for (size_t jj = p; jj-- > 1; ) out.push_back(Value::num(beta[jj]));
                out.push_back(Value::num(beta[0]));   // 截距永远在最后
            } else {
                for (size_t j = 0; j < p; j++) out.push_back(Value::num(beta[j]));
            }
            arr->push_back(out);
            return Value::array(arr);
        });

        // --- 指数回归 LOGEST：对 ln(y) 做线性拟合，系数取 exp ---
        registerFunction("LOGEST", 1, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> y; Value e;
            col2({a[0]}, y, e); if (e.isError()) return e;
            for (double& v : y) { if (v <= 0) return Value::error(Err::Num); v = std::log(v); }
            std::vector<Value> args = a;
            auto ly = std::make_shared<Array2D>();
            ly->push_back({});
            for (double v : y) ly->at(0).push_back(Value::num(v));
            args[0] = Value::array(ly);
            Value r = findFunction("LINEST")->impl(args, *(EvalCtx*)nullptr);
            if (r.isError() || !r.arr) return r;
            for (auto& row : *r.arr)
                for (auto& c : row) if (c.isNum()) c = Value::num(std::exp(c.n));
            return r;
        });

        // --- TREND：按线性拟合给出预测 ---
        registerFunction("TREND", 1, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> base = {a[0]};
            if (a.size() >= 2) base.push_back(a[1]);
            Value fit = findFunction("LINEST")->impl(base, *(EvalCtx*)nullptr);
            if (fit.isError() || !fit.arr || fit.arr->empty()) return fit;
            auto& row0 = (*fit.arr)[0];
            double b = row0.back().n;                        // 截距
            std::vector<double> slopes;
            for (size_t i = 0; i + 1 < row0.size(); i++) slopes.push_back(row0[i].n);
            // 预测点：new_x's，缺省沿用 known_x's
            std::vector<double> xs; Value e;
            // new_x's 只认数组 → 传标量时整段跳过，退回用 known_x's，
            // 于是 TREND({2,4,6},{1,2,3},4) 返回 {2,4,6} 而不是 8。
            // Excel 里标量是合法的（等价于一个元素的数组）。
            if (a.size() >= 3) {
                if (a[2].isArray()) col2({a[2]}, xs, e);
                else { double d; if (!n1(a[2], d)) return Value::error(Err::Value); xs.push_back(d); }
            }
            else if (a.size() >= 2 && a[1].isArray()) col2({a[1]}, xs, e);
            if (e.isError()) return e;
            if (xs.empty()) { xs.clear(); for (size_t i = 0; i < slopes.size() + 1; i++) xs.push_back(1); }
            size_t k = slopes.size();
            size_t rows = k ? xs.size() / k : 0;
            if (rows == 0) { rows = xs.size(); k = 1; }
            auto arr = std::make_shared<Array2D>();
            std::vector<Value> out;
            for (size_t r = 0; r < rows; r++) {
                double v = b;
                for (size_t j = 0; j < k; j++) v += slopes[j] * xs[r * k + j];
                out.push_back(Value::num(v));
            }
            arr->push_back(out);
            return Value::array(arr);
        });

        // --- GROWTH：指数预测 ---
        registerFunction("GROWTH", 1, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> base = {a[0]};
            if (a.size() >= 2) base.push_back(a[1]);
            Value fit = findFunction("LOGEST")->impl(base, *(EvalCtx*)nullptr);
            if (fit.isError() || !fit.arr || fit.arr->empty()) return fit;
            auto& row0 = (*fit.arr)[0];
            std::vector<double> xs; Value e;
            // 同 TREND：标量 new_x's 曾经被忽略
            if (a.size() >= 3) {
                if (a[2].isArray()) col2({a[2]}, xs, e);
                else { double d; if (!n1(a[2], d)) return Value::error(Err::Value); xs.push_back(d); }
            }
            else if (a.size() >= 2 && a[1].isArray()) col2({a[1]}, xs, e);
            if (e.isError()) return e;
            size_t k = row0.size() - 1;
            if (k < 1) {
                // 一元：x = 1..n
                k = 1; xs.clear();
                std::vector<double> ys; col2({a[0]}, ys, e);
                for (size_t i = 0; i < ys.size(); i++) xs.push_back((double)(i + 1));
            }
            size_t rows = xs.size() / k;
            double b = row0.back().n;
            auto arr = std::make_shared<Array2D>();
            std::vector<Value> out;
            for (size_t r = 0; r < rows; r++) {
                double lnv = std::log(b);
                for (size_t j = 0; j < k; j++) lnv += std::log(row0[j].n) * xs[r * k + j];
                out.push_back(Value::num(std::exp(lnv)));
            }
            arr->push_back(out);
            return Value::array(arr);
        });

        // --- 假设检验 ---
        registerFunction("T.TEST", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> x, y; Value e;
            col2({a[0]}, x, e); if (e.isError()) return e;
            col2({a[1]}, y, e); if (e.isError()) return e;
            int tails, type;
            if (!i1(a[2], tails, e)) return e;
            if (!i1(a[3], type, e)) return e;
            if (x.size() < 2 || y.size() < 2) return Value::error(Err::NA);
            if (tails != 1 && tails != 2) return Value::error(Err::Num);
            double t = 0, df = 0;
            if (type == 1) {                                  // 配对
                if (x.size() != y.size()) return Value::error(Err::NA);
                size_t n = x.size();
                std::vector<double> d(n);
                for (size_t i = 0; i < n; i++) d[i] = x[i] - y[i];
                double m = 0; for (double v : d) m += v; m /= n;
                double s2 = 0; for (double v : d) s2 += (v - m) * (v - m);
                double sd = std::sqrt(s2 / (n - 1));
                if (sd == 0) return Value::error(Err::Div0);
                t = m / (sd / std::sqrt((double)n));
                df = n - 1;
            } else if (type == 2 || type == 3) {               // 等方差 / 异方差
                size_t n1 = x.size(), n2 = y.size();
                double m1 = 0, m2 = 0;
                for (double v : x) m1 += v;
                for (double v : y) m2 += v;
                m1 /= n1; m2 /= n2;
                double s1 = 0, s2 = 0;
                for (double v : x) s1 += (v - m1) * (v - m1);
                for (double v : y) s2 += (v - m2) * (v - m2);
                s1 /= (n1 - 1); s2 /= (n2 - 1);
                if (type == 2) {
                    double sp = ((n1 - 1) * s1 + (n2 - 1) * s2) / (n1 + n2 - 2);
                    if (sp <= 0) return Value::error(Err::Div0);
                    t = (m1 - m2) / std::sqrt(sp * (1.0 / n1 + 1.0 / n2));
                    df = n1 + n2 - 2;
                } else {
                    double se = std::sqrt(s1 / n1 + s2 / n2);
                    if (se == 0) return Value::error(Err::Div0);
                    t = (m1 - m2) / se;
                    double num = std::pow(s1 / n1 + s2 / n2, 2);
                    double den = std::pow(s1 / n1, 2) / (n1 - 1) + std::pow(s2 / n2, 2) / (n2 - 1);
                    df = den == 0 ? 1 : num / den;
                }
            } else return Value::error(Err::Num);
            double p = tails == 1 ? (1.0 - dist::tCdf(std::fabs(t), df))
                                  : 2.0 * (1.0 - dist::tCdf(std::fabs(t), df));
            return Value::num(std::max(0.0, std::min(1.0, p)));
        });
        registerFunction("TTEST", 4, 4, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("T.TEST")->impl(a, c); });

        registerFunction("F.TEST", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> x, y; Value e;
            col2({a[0]}, x, e); if (e.isError()) return e;
            col2({a[1]}, y, e); if (e.isError()) return e;
            if (x.size() < 2 || y.size() < 2) return Value::error(Err::NA);
            auto var = [](const std::vector<double>& v) {
                double m = 0; for (double d : v) m += d; m /= v.size();
                double s = 0; for (double d : v) s += (d - m) * (d - m);
                return s / (v.size() - 1);
            };
            double v1 = var(x), v2 = var(y);
            if (v1 <= 0 || v2 <= 0) return Value::error(Err::Div0);
            double d1 = x.size() - 1, d2 = y.size() - 1;
            double F = v1 / v2;
            // 双尾：取两侧较小者 ×2
            double rt = dist::fCdfR(F, d1, d2);
            double p = 2.0 * std::min(rt, 1.0 - rt);
            return Value::num(std::max(0.0, std::min(1.0, p)));
        });
        registerFunction("FTEST", 2, 2, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("F.TEST")->impl(a, c); });

        registerFunction("CHISQ.TEST", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> obs, exp; Value e;
            if (a[0].isArray()) obs = *a[0].arr; else return Value::error(Err::Value);
            if (a[1].isArray()) exp = *a[1].arr; else return Value::error(Err::Value);
            if (obs.size() != exp.size()) return Value::error(Err::NA);
            double chi = 0; int df = 0;
            for (size_t r = 0; r < obs.size(); r++) {
                if (obs[r].size() != exp[r].size()) return Value::error(Err::NA);
                for (size_t c = 0; c < obs[r].size(); c++) {
                    double o, x;
                    if (!n1(obs[r][c], o) || !n1(exp[r][c], x)) return Value::error(Err::Value);
                    if (x <= 0) return Value::error(Err::Num);
                    chi += (o - x) * (o - x) / x;
                }
                df += (int)obs[r].size() - 1;
            }
            if (df < 1) df = 1;
            return Value::num(std::max(0.0, std::min(1.0, dist::chiCdfR(chi, df))));
        });
        registerFunction("CHITEST", 2, 2, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("CHISQ.TEST")->impl(a, c); });

        registerFunction("FISHER", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            if (x <= -1 || x >= 1) return Value::error(Err::Num);
            return Value::num(0.5 * std::log((1.0 + x) / (1.0 - x)));
        });
        registerFunction("FISHERINV", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double y; if (!n1(a[0], y)) return Value::error(Err::Value);
            return Value::num(std::tanh(y));
        });

        // --- 更多分布 ---
        registerFunction("GAMMA.DIST", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, alpha, beta; int cum;
            if (!n1(a[0], x) || !n1(a[1], alpha) || !n1(a[2], beta)) return Value::error(Err::Value);
            Value e; if (!i1(a[3], cum, e)) return e;
            if (x < 0 || alpha <= 0 || beta <= 0) return Value::error(Err::Num);
            if (x == 0) return Value::num(cum ? 0.0 : 0.0);
            double z = x / beta;
            if (cum) return Value::num(dist::gammp(alpha, z));
            return Value::num(std::exp((alpha - 1) * std::log(z) - z - std::lgamma(alpha)) / beta);
        });
        registerFunction("GAMMADIST", 4, 4, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("GAMMA.DIST")->impl(a, c); });
        registerFunction("GAMMA.INV", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double p, alpha, beta;
            if (!n1(a[0], p) || !n1(a[1], alpha) || !n1(a[2], beta)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1 || alpha <= 0 || beta <= 0) return Value::error(Err::Num);
            double lo = 1e-9, hi = 1e6;
            for (int i = 0; i < 200; i++) {
                double mid = (lo + hi) / 2;
                if (dist::gammp(alpha, mid / beta) < p) lo = mid; else hi = mid;
            }
            return Value::num((lo + hi) / 2);
        });
        registerFunction("GAMMALN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            if (x <= 0) return Value::error(Err::Num);
            return Value::num(std::lgamma(x));
        });
        registerFunction("GAMMA", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            if (x <= 0 && std::floor(x) == x) return Value::error(Err::Num);
            return Value::num(std::tgamma(x));
        });
        registerFunction("BETA.DIST", 4, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, al, be; int cum;
            if (!n1(a[0], x) || !n1(a[1], al) || !n1(a[2], be)) return Value::error(Err::Value);
            Value e; if (!i1(a[3], cum, e)) return e;
            if (al <= 0 || be <= 0 || x < 0) return Value::error(Err::Num);
            if (cum && x > 1) return Value::num(1.0);
            if (!cum && x > 1) return Value::num(0.0);
            if (cum) return Value::num(dist::betai(al, be, x));
            return Value::num(std::exp((al - 1) * std::log(x) + (be - 1) * std::log(1.0 - x)
                                       + std::lgamma(al + be) - std::lgamma(al) - std::lgamma(be)));
        });
        // BETADIST(x, alpha, beta, [A], [B]) 是旧式签名，**没有** cumulative 参数，
        // 恒表示累积分布。直接把 3 个参数转发给 BETA.DIST 会让 beta 被当成
        // cumulative —— 实测 BETADIST(0.5,1,1) 因此返回 #VALUE!（而且即使
        // 参数够了语义也是错的）。这里补一个 TRUE 到正确位置。
        registerFunction("BETADIST", 3, 5, [](const std::vector<Value>& a, EvalCtx& c) -> Value {
            std::vector<Value> b = {a[0], a[1], a[2], Value::boolean(true)};
            for (size_t i = 3; i < a.size(); i++) b.push_back(a[i]);   // 可选的 A、B
            return findFunction("BETA.DIST")->impl(b, c);
        });
        registerFunction("BETA.INV", 3, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double p, al, be;
            if (!n1(a[0], p) || !n1(a[1], al) || !n1(a[2], be)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1 || al <= 0 || be <= 0) return Value::error(Err::Num);
            double lo = 0, hi = 1;
            for (int i = 0; i < 200; i++) {
                double mid = (lo + hi) / 2;
                if (dist::betai(al, be, mid) < p) lo = mid; else hi = mid;
            }
            return Value::num((lo + hi) / 2);
        });
        registerFunction("LOGNORM.DIST", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, m, sd; int cum;
            if (!n1(a[0], x) || !n1(a[1], m) || !n1(a[2], sd)) return Value::error(Err::Value);
            Value e; if (!i1(a[3], cum, e)) return e;
            if (x <= 0 || sd <= 0) return Value::error(Err::Num);
            double z = (std::log(x) - m) / sd;
            if (cum) return Value::num(dist::normCdf(z));
            return Value::num(std::exp(-0.5 * z * z) / (x * sd * std::sqrt(2 * 3.14159265358979323846)));
        });
        registerFunction("LOGNORMDIST", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> args = {a[0], a[1], a[2], Value::boolean(true)};
            return findFunction("LOGNORM.DIST")->impl(args, *(EvalCtx*)nullptr);
        });
        registerFunction("LOGNORM.INV", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double p, m, sd;
            if (!n1(a[0], p) || !n1(a[1], m) || !n1(a[2], sd)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1 || sd <= 0) return Value::error(Err::Num);
            return Value::num(std::exp(m + sd * dist::normInv(p)));
        });
        registerFunction("WEIBULL.DIST", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, al, be; int cum;
            if (!n1(a[0], x) || !n1(a[1], al) || !n1(a[2], be)) return Value::error(Err::Value);
            Value e; if (!i1(a[3], cum, e)) return e;
            if (x < 0 || al <= 0 || be <= 0) return Value::error(Err::Num);
            if (x == 0) return Value::num(cum ? 0.0 : 0.0);
            double z = std::pow(x / be, al);
            if (cum) return Value::num(1.0 - std::exp(-z));
            return Value::num(al / be * std::pow(x / be, al - 1) * std::exp(-z));
        });
        registerFunction("HYPGEOM.DIST", 5, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int k, n, M, N; Value e;
            if (!i1(a[0], k, e)) return e;
            if (!i1(a[1], n, e)) return e;
            if (!i1(a[2], M, e)) return e;
            if (!i1(a[3], N, e)) return e;
            int cum; if (!i1(a[4], cum, e)) return e;
            if (k < 0 || k > n || M > N || n > N) return Value::error(Err::Num);
            auto C = [](int a2, int b2) -> double {
                if (b2 < 0 || b2 > a2) return 0.0;
                double r = 1; b2 = std::min(b2, a2 - b2);
                for (int i = 1; i <= b2; i++) r = r * (a2 - b2 + i) / i;
                return r;
            };
            if (cum) {
                double s = 0;
                for (int i = 0; i <= k; i++) s += C(M, i) * C(N - M, n - i);
                return Value::num(s / C(N, n));
            }
            return Value::num(C(M, k) * C(N - M, n - k) / C(N, n));
        });
        registerFunction("NEGBINOM.DIST", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int f, s; double p; int cum; Value e;
            if (!i1(a[0], f, e)) return e;
            if (!i1(a[1], s, e)) return e;
            if (!n1(a[2], p)) return Value::error(Err::Value);
            if (!i1(a[3], cum, e)) return e;
            if (f < 0 || s < 1 || p <= 0 || p > 1) return Value::error(Err::Num);
            auto C = [](int a2, int b2) -> double {
                if (b2 < 0 || b2 > a2) return 0.0;
                double r = 1; b2 = std::min(b2, a2 - b2);
                for (int i = 1; i <= b2; i++) r = r * (a2 - b2 + i) / i;
                return r;
            };
            // Excel: 失败次数 f 出现在成功 s 次之前
            if (cum) {
                double acc = 0;
                for (int i = 0; i <= f; i++)
                    acc += C(i + s - 1, i) * std::pow(p, s) * std::pow(1 - p, i);
                return Value::num(acc);
            }
            return Value::num(C(f + s - 1, f) * std::pow(p, s) * std::pow(1 - p, f));
        });

        // --- 条件极值（Excel 2016+）---
        auto critIdx = [](const Value& range, const std::string& crit,
                          std::vector<Value>& cells, std::vector<size_t>& idx, Value& err) -> bool {
            flatKeep(range, cells, err);
            if (err.isError()) return false;
            for (size_t i = 0; i < cells.size(); i++) {
                if (cells[i].isError()) { err = cells[i]; return false; }
                bool m = matchCrit(cells[i], crit, err);
                if (err.isError()) return false;
                if (m) idx.push_back(i);
            }
            return true;
        };
        registerFunction("MAXIFS", 3, 255, [critIdx](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (a.size() < 3 || (a.size() - 1) % 2 != 0) return Value::error(Err::Value);
            std::vector<Value> src; Value e;
            flatKeep(a[0], src, e); if (e.isError()) return e;
            size_t nCrit = (a.size() - 1) / 2;
            std::vector<std::vector<Value>> cells(nCrit);
            std::vector<std::vector<size_t>> idx(nCrit);
            for (size_t k = 0; k < nCrit; k++) {
                // MAXIFS(max_range, crit_range1, crit1, ...)
                std::string crit;
                if (!toText(a[2 + 2 * k], crit, e)) return e;
                if (!critIdx(a[1 + 2 * k], crit, cells[k], idx[k], e)) return e;
            }
            bool found = false; double best = 0;
            for (size_t i = 0; i < src.size(); i++) {
                bool all = true;
                for (size_t k = 0; k < nCrit; k++)
                    if (std::find(idx[k].begin(), idx[k].end(), i) == idx[k].end()) { all = false; break; }
                if (!all) continue;
                if (i >= src.size()) continue;
                const Value& v = src[i];
                if (v.isError()) return v;
                if (!v.isNum()) continue;
                if (!found || v.n > best) { best = v.n; found = true; }
            }
            return Value::num(found ? best : 0.0);
        });
        registerFunction("MINIFS", 3, 255, [critIdx](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (a.size() < 3 || (a.size() - 1) % 2 != 0) return Value::error(Err::Value);
            std::vector<Value> src; Value e;
            flatKeep(a[0], src, e); if (e.isError()) return e;
            size_t nCrit = (a.size() - 1) / 2;
            std::vector<std::vector<Value>> cells(nCrit);
            std::vector<std::vector<size_t>> idx(nCrit);
            for (size_t k = 0; k < nCrit; k++) {
                // MAXIFS(max_range, crit_range1, crit1, ...)
                std::string crit;
                if (!toText(a[2 + 2 * k], crit, e)) return e;
                if (!critIdx(a[1 + 2 * k], crit, cells[k], idx[k], e)) return e;
            }
            bool found = false; double best = 0;
            for (size_t i = 0; i < src.size(); i++) {
                bool all = true;
                for (size_t k = 0; k < nCrit; k++)
                    if (std::find(idx[k].begin(), idx[k].end(), i) == idx[k].end()) { all = false; break; }
                if (!all) continue;
                const Value& v = src[i];
                if (v.isError()) return v;
                if (!v.isNum()) continue;
                if (!found || v.n < best) { best = v.n; found = true; }
            }
            return Value::num(found ? best : 0.0);
        });

        // --- 秩和 / 描述 ---
        registerFunction("AVERAGEA", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> v;
            for (auto& x : a) {
                if (x.isArray()) { if (x.arr) for (auto& r : *x.arr) for (auto& c : r) {
                    if (c.isError()) return c;
                    if (c.isEmpty() || c.isStr()) { v.push_back(0); continue; }
                    double d; if (!n1(c, d)) { v.push_back(0); continue; }
                    v.push_back(d);
                } } else { double d; if (!n1(x, d)) { v.push_back(0); continue; } v.push_back(d); }
            }
            if (v.empty()) return Value::error(Err::Div0);
            double s = 0; for (double d : v) s += d;
            return Value::num(s / v.size());
        });
        registerFunction("MAXA", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> v;
            for (auto& x : a) {
                if (x.isArray()) { if (x.arr) for (auto& r : *x.arr) for (auto& c : r) {
                    if (c.isError()) return c;
                    double d; if (n1(c, d)) v.push_back(d); else v.push_back(0);
                } } else { double d; if (n1(x, d)) v.push_back(d); else v.push_back(0); }
            }
            if (v.empty()) return Value::num(0);
            return Value::num(*std::max_element(v.begin(), v.end()));
        });
        registerFunction("MINA", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> v;
            for (auto& x : a) {
                if (x.isArray()) { if (x.arr) for (auto& r : *x.arr) for (auto& c : r) {
                    if (c.isError()) return c;
                    double d; if (n1(c, d)) v.push_back(d); else v.push_back(0);
                } } else { double d; if (n1(x, d)) v.push_back(d); else v.push_back(0); }
            }
            if (v.empty()) return Value::num(0);
            return Value::num(*std::min_element(v.begin(), v.end()));
        });
        registerFunction("SUBTOTAL", 2, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int fn; Value e;
            if (!i1(a[0], fn, e)) return e;
            if (fn < 1 || fn > 11 || (fn > 111 && fn < 1)) {
                if (!(fn >= 1 && fn <= 11) && !(fn >= 101 && fn <= 111)) return Value::error(Err::Value);
            }
            bool ignoreHidden = (fn >= 101);
            int base = (fn >= 101) ? fn - 100 : fn;
            std::vector<double> v;
            col2(std::vector<Value>(a.begin() + 1, a.end()), v, e);
            if (e.isError()) return e;
            (void)ignoreHidden;
            switch (base) {
                case 1: { double s = 0; for (double d : v) s += d; return Value::num(s); }
                case 2: return Value::num((double)v.size());
                case 3: { size_t c = 0; for (double d : v) (void)d, c++; return Value::num((double)c); }
                case 4: return v.empty() ? Value::num(0) : Value::num(*std::max_element(v.begin(), v.end()));
                case 5: return v.empty() ? Value::num(0) : Value::num(*std::min_element(v.begin(), v.end()));
                case 6: { double p = 1; for (double d : v) p *= d; return Value::num(p); }
                case 7: {
                    if (v.size() < 2) return Value::error(Err::Div0);
                    double m = 0; for (double d : v) m += d; m /= v.size();
                    double s = 0; for (double d : v) s += (d - m) * (d - m);
                    return Value::num(std::sqrt(s / (v.size() - 1)));
                }
                case 8: {
                    if (v.size() < 2) return Value::error(Err::Div0);
                    double m = 0; for (double d : v) m += d; m /= v.size();
                    double s = 0; for (double d : v) s += (d - m) * (d - m);
                    return Value::num(s / (v.size() - 1));
                }
                case 9: { double s = 0; for (double d : v) s += d; return Value::num(v.empty() ? 0 : s); }
                case 10: {
                    if (v.size() < 2) return Value::error(Err::Div0);
                    double m = 0; for (double d : v) m += d; m /= v.size();
                    double s = 0; for (double d : v) s += (d - m) * (d - m);
                    return Value::num(std::sqrt(s / v.size()));
                }
                case 11: {
                    if (v.size() < 2) return Value::error(Err::Div0);
                    double m = 0; for (double d : v) m += d; m /= v.size();
                    double s = 0; for (double d : v) s += (d - m) * (d - m);
                    return Value::num(s / v.size());
                }
            }
            return Value::error(Err::Value);
        });
    }
};


static Stat2Registrar g_stat2Reg;

} // namespace xl
