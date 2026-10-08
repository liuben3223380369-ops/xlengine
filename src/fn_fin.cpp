#include "functions.hpp"
#include "date.hpp"
#include <algorithm>
#include <cmath>

namespace xl {


// ---------------------------------------------------------------------------
// 付息期定位：以**到期日**为锚点向前递推
// ---------------------------------------------------------------------------
// Excel 的付息日序列由到期日 + 频率决定，往前逐个周期递推得到，
// 与结算日所在年份无关。原先的写法是按结算日月份取固定的月份档
// （freq=2 → 1月/7月），于是：
//   COUPDAYS(2023-5-15, 2030-11-15, 2, 1) = 181   ← 按 1/15~7/15 算
//   正确应为 184                                    ← 按 5/15~11/15 算
// 三个函数都用同一套错误锚点，所以互相"自洽"，很难发现。
static double couponSerial(int y, int m, int day) {
    return ymdToSerial(y, m, std::min(day, daysInMonth(y, m)));
}
// 返回结算日所在的付息期两端：[prev, next]
static bool couponPeriod(double st, double mt, int freq, double& prev, double& next) {
    YMD my = serialToYMD(std::floor(mt));
    const int step = 12 / freq;
    int y = my.y, m = my.m;
    double d = couponSerial(y, m, my.d);
    int guard = 0;
    while (d > st + 1e-9 && guard++ < 100000) {
        m -= step; if (m < 1) { m += 12; y--; }
        d = couponSerial(y, m, my.d);
    }
    prev = d;
    m += step; if (m > 12) { m -= 12; y++; }
    next = couponSerial(y, m, my.d);
    return true;
}

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

// ---------------------------------------------------------------------------
// 现金流求解器
// ---------------------------------------------------------------------------
namespace fin {

// 年金现值因子
inline double pvAnnuity(double r, double n, int type) {
    if (r == 0) return -n;
    return -(1.0 - std::pow(1.0 + r, -n)) / r * (1.0 + r * type);
}
// PV(rate,nper,pmt,fv,type) 方程的残差（=0 时即解）
inline double pvEq(double r, double n, double pmt, double fv, double pv, int type) {
    if (r == 0) return pv + pmt * n + fv;
    return pv * std::pow(1.0 + r, n) + pmt * (1.0 + r * type) * (std::pow(1.0 + r, n) - 1.0) / r + fv;
}
// NPV：values[0] 对应第 1 期（Excel NPV 约定）
inline double npvAt(double r, const std::vector<double>& cf) {
    double s = 0;
    for (size_t i = 0; i < cf.size(); i++) s += cf[i] / std::pow(1.0 + r, (double)(i + 1));
    return s;
}
// IRR：values[0] 是第 0 期，不折现 —— 与 NPV 的约定不同，必须区分
inline double irrAt(double r, const std::vector<double>& cf) {
    double s = 0;
    for (size_t i = 0; i < cf.size(); i++) s += cf[i] / std::pow(1.0 + r, (double)i);
    return s;
}
// 在 [lo,hi] 上用二分 + Newton 求根
inline bool solve(std::function<double(double)> f, double& root, double lo, double hi, int maxIt = 200) {
    double flo = f(lo), fhi = f(hi);
    if (std::isnan(flo) || std::isnan(fhi)) return false;
    if (flo * fhi > 0) {
        // 扩大搜索区间
        for (int k = 0; k < 40; k++) {
            lo *= 2; hi *= 2;
            if (std::fabs(lo) > 1e12) break;
            flo = f(lo); fhi = f(hi);
            if (std::isnan(flo) || std::isnan(fhi)) continue;
            if (flo * fhi <= 0) break;
        }
        if (std::isnan(flo) || std::isnan(fhi) || flo * fhi > 0) return false;
    }
    for (int i = 0; i < maxIt; i++) {
        double mid = (lo + hi) / 2.0;
        double fm = f(mid);
        if (std::isnan(fm)) return false;
        if (std::fabs(fm) < 1e-11 || (hi - lo) < 1e-14) { root = mid; return true; }
        if (flo * fm <= 0) { hi = mid; fhi = fm; } else { lo = mid; flo = fm; }
    }
    root = (lo + hi) / 2.0;
    return true;
}

} // namespace fin

// ===========================================================================
// 财务函数
// ===========================================================================
struct FinRegistrar {
    FinRegistrar() {
        // --- 货币时间价值 ---
        registerFunction("PV", 3, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, n, pmt; Value e;
            if (!n1(a[0], r) || !n1(a[1], n) || !n1(a[2], pmt)) return Value::error(Err::Value);
            double fv = 0; int type = 0;
            if (a.size() >= 4 && !n1(a[3], fv)) return Value::error(Err::Value);
            if (a.size() >= 5 && !i1(a[4], type, e)) return e;
            if (r == 0) return Value::num(-pmt * n - fv);
            return Value::num((-pmt * (1.0 + r * type) * (1.0 - std::pow(1.0 + r, -n)) / r)
                              - fv * std::pow(1.0 + r, -n));
        });
        registerFunction("FV", 3, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, n, pmt; Value e;
            if (!n1(a[0], r) || !n1(a[1], n) || !n1(a[2], pmt)) return Value::error(Err::Value);
            double pv = 0; int type = 0;
            if (a.size() >= 4 && !n1(a[3], pv)) return Value::error(Err::Value);
            if (a.size() >= 5 && !i1(a[4], type, e)) return e;
            if (r == 0) return Value::num(-(pv + pmt * n));
            return Value::num(-(pv * std::pow(1.0 + r, n)
                                + pmt * (1.0 + r * type) * (std::pow(1.0 + r, n) - 1.0) / r));
        });
        registerFunction("PMT", 3, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, n, pv; Value e;
            if (!n1(a[0], r) || !n1(a[1], n) || !n1(a[2], pv)) return Value::error(Err::Value);
            double fv = 0; int type = 0;
            if (a.size() >= 4 && !n1(a[3], fv)) return Value::error(Err::Value);
            if (a.size() >= 5 && !i1(a[4], type, e)) return e;
            if (r == 0) return Value::num(-(pv + fv) / n);
            double powN = std::pow(1.0 + r, n);
            return Value::num(-(pv * powN + fv) * r / ((powN - 1.0) * (1.0 + r * type)));
        });
        registerFunction("RATE", 3, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double n, pmt, pv; Value e;
            if (!n1(a[0], n) || !n1(a[1], pmt) || !n1(a[2], pv)) return Value::error(Err::Value);
            double fv = 0; int type = 0; double guess = 0.1;
            if (a.size() >= 4 && !n1(a[3], fv)) return Value::error(Err::Value);
            if (a.size() >= 5 && !i1(a[4], type, e)) return e;
            if (a.size() >= 6 && !n1(a[5], guess)) return Value::error(Err::Value);
            double root;
            bool ok = fin::solve([&](double r) {
                return fin::pvEq(r, n, pmt, fv, pv, type);
            }, root, -0.999999, 10.0);
            if (!ok) return Value::error(Err::Num);
            return Value::num(root);
        });
        registerFunction("NPER", 3, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, pmt, pv; Value e;
            if (!n1(a[0], r) || !n1(a[1], pmt) || !n1(a[2], pv)) return Value::error(Err::Value);
            double fv = 0; int type = 0;
            if (a.size() >= 4 && !n1(a[3], fv)) return Value::error(Err::Value);
            if (a.size() >= 5 && !i1(a[4], type, e)) return e;
            if (r == 0) return Value::num(-(pv + fv) / pmt);
            double pmtAdj = pmt * (1.0 + r * type);
            return Value::num(std::log((pmtAdj - fv * r) / (pmtAdj + pv * r)) / std::log(1.0 + r));
        });
        registerFunction("NPV", 2, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r; if (!n1(a[0], r)) return Value::error(Err::Value);
            std::vector<double> cf; Value e;
            for (size_t k = 1; k < a.size(); k++) col2({a[k]}, cf, e);
            if (e.isError()) return e;
            if (cf.empty()) return Value::error(Err::Value);
            if (r <= -1) return Value::error(Err::Num);
            return Value::num(fin::npvAt(r, cf));
        });
        registerFunction("IRR", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> cf; Value e;
            col2({a[0]}, cf, e); if (e.isError()) return e;
            if (cf.size() < 2) return Value::error(Err::Num);
            bool pos = false, neg = false;
            for (auto x : cf) { if (x > 0) pos = true; if (x < 0) neg = true; }
            if (!pos || !neg) return Value::error(Err::Num);
            double root;
            bool ok = fin::solve([&](double r) { return fin::irrAt(r, cf); }, root, -0.9999, 10.0);
            if (!ok) return Value::error(Err::Num);
            return Value::num(root);
        });
        registerFunction("XNPV", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r; if (!n1(a[0], r)) return Value::error(Err::Value);
            std::vector<double> cf, dt; Value e;
            col2({a[1]}, cf, e); if (e.isError()) return e;
            // 日期需要保留顺序：单独遍历区域
            std::vector<Value> dvals;
            if (a[2].isArray()) { if (a[2].arr) for (auto& row : *a[2].arr) for (auto& x : row) dvals.push_back(x); }
            else dvals.push_back(a[2]);
            if (dvals.size() != cf.size()) return Value::error(Err::Num);
            for (auto& d : dvals) { double s; if (!valueToSerial(d, s)) return Value::error(Err::Value); dt.push_back(s); }
            if (r <= -1) return Value::error(Err::Num);
            double s = 0;
            for (size_t i = 0; i < cf.size(); i++)
                s += cf[i] / std::pow(1.0 + r, (dt[i] - dt[0]) / 365.0);
            return Value::num(s);
        });
        registerFunction("XIRR", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> cf, dt; Value e;
            col2({a[0]}, cf, e); if (e.isError()) return e;
            std::vector<Value> dvals;
            if (a[1].isArray()) { if (a[1].arr) for (auto& row : *a[1].arr) for (auto& x : row) dvals.push_back(x); }
            else dvals.push_back(a[1]);
            if (dvals.size() != cf.size()) return Value::error(Err::Num);
            for (auto& d : dvals) { double s; if (!valueToSerial(d, s)) return Value::error(Err::Value); dt.push_back(s); }
            bool pos = false, neg = false;
            for (auto x : cf) { if (x > 0) pos = true; if (x < 0) neg = true; }
            if (!pos || !neg) return Value::error(Err::Num);
            double root;
            bool ok = fin::solve([&](double r) {
                if (r <= -1) return 1e300;
                double s = 0;
                for (size_t i = 0; i < cf.size(); i++)
                    s += cf[i] / std::pow(1.0 + r, (dt[i] - dt[0]) / 365.0);
                return s;
            }, root, -0.9999, 10.0);
            if (!ok) return Value::error(Err::Num);
            return Value::num(root);
        });
        registerFunction("MIRR", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> cf; Value e;
            col2({a[0]}, cf, e); if (e.isError()) return e;
            double fr, rr;
            if (!n1(a[1], fr) || !n1(a[2], rr)) return Value::error(Err::Value);
            if (cf.size() < 2) return Value::error(Err::Div0);
            int n = (int)cf.size();
            double negPv = 0, posFv = 0;
            for (int i = 0; i < n; i++) {
                if (cf[i] < 0) negPv += cf[i] / std::pow(1.0 + fr, (double)i);
                else           posFv += cf[i] * std::pow(1.0 + rr, (double)(n - 1 - i));
            }
            if (negPv == 0) return Value::error(Err::Div0);
            return Value::num(std::pow(-posFv / negPv, 1.0 / (n - 1)) - 1.0);
        });
        registerFunction("IPMT", 4, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, per, n, pv; Value e;
            if (!n1(a[0], r) || !n1(a[1], per) || !n1(a[2], n) || !n1(a[3], pv)) return Value::error(Err::Value);
            double fv = 0; int type = 0;
            if (a.size() >= 5 && !n1(a[4], fv)) return Value::error(Err::Value);
            if (a.size() >= 6 && !i1(a[5], type, e)) return e;
            if (per < 1 || per > n) return Value::error(Err::Num);
            // 第 per 期期初余额 = FV(r, per-1, pmt, pv, type) 的相反数
            double pmt = (r == 0) ? (-(pv + fv) / n)
                       : (-(pv * std::pow(1.0 + r, n) + fv) * r / ((std::pow(1.0 + r, n) - 1.0) * (1.0 + r * type)));
            double bal;
            if (r == 0) bal = -(pv + pmt * (per - 1));
            else bal = -(pv * std::pow(1.0 + r, per - 1)
                         + pmt * (1.0 + r * type) * (std::pow(1.0 + r, per - 1) - 1.0) / r);
            return Value::num(bal * r);        // bal<0 表示欠款，利息为负（支出）
        });
        registerFunction("PPMT", 4, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value ip = findFunction("IPMT")->impl(a, *(EvalCtx*)nullptr);
            if (ip.isError()) return ip;
            double r, per, n, pv;
            if (!n1(a[0], r) || !n1(a[1], per) || !n1(a[2], n) || !n1(a[3], pv)) return Value::error(Err::Value);
            double fv = 0; int type = 0; Value e;
            if (a.size() >= 5 && !n1(a[4], fv)) return Value::error(Err::Value);
            if (a.size() >= 6 && !i1(a[5], type, e)) return e;
            double pmt = (r == 0) ? (-(pv + fv) / n)
                       : (-(pv * std::pow(1.0 + r, n) + fv) * r / ((std::pow(1.0 + r, n) - 1.0) * (1.0 + r * type)));
            return Value::num(pmt - ip.n);
        });
        registerFunction("ISPMT", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, per, n, pv;
            if (!n1(a[0], r) || !n1(a[1], per) || !n1(a[2], n) || !n1(a[3], pv)) return Value::error(Err::Value);
            return Value::num(pv * r * (per / n - 1.0));
        });
        registerFunction("CUMIPMT", 6, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, n, pv; int start, end, type; Value e;
            if (!n1(a[0], r) || !n1(a[1], n) || !n1(a[2], pv)) return Value::error(Err::Value);
            if (!i1(a[3], start, e)) return e;
            if (!i1(a[4], end, e)) return e;
            if (!i1(a[5], type, e)) return e;
            if (start < 1 || end < start || end > n) return Value::error(Err::Num);
            double pmt;
            if (r == 0) pmt = -pv / n;
            else pmt = -(pv * std::pow(1.0 + r, n)) * r / ((std::pow(1.0 + r, n) - 1.0) * (1.0 + r * type));
            double total = 0;
            for (int per = start; per <= end; per++) {
                double bal;
                if (r == 0) bal = -(pv + pmt * (per - 1));
                else bal = -(pv * std::pow(1.0 + r, per - 1)
                             + pmt * (1.0 + r * type) * (std::pow(1.0 + r, per - 1) - 1.0) / r);
                total += bal * r;
            }
            return Value::num(total);
        });
        registerFunction("CUMPRINC", 6, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, n, pv; int start, end, type; Value e;
            if (!n1(a[0], r) || !n1(a[1], n) || !n1(a[2], pv)) return Value::error(Err::Value);
            if (!i1(a[3], start, e)) return e;
            if (!i1(a[4], end, e)) return e;
            if (!i1(a[5], type, e)) return e;
            if (start < 1 || end < start || end > n) return Value::error(Err::Num);
            double pmt;
            if (r == 0) pmt = -pv / n;
            else pmt = -(pv * std::pow(1.0 + r, n)) * r / ((std::pow(1.0 + r, n) - 1.0) * (1.0 + r * type));
            double total = 0;
            for (int per = start; per <= end; per++) {
                double bal;
                if (r == 0) bal = -(pv + pmt * (per - 1));
                else bal = -(pv * std::pow(1.0 + r, per - 1)
                             + pmt * (1.0 + r * type) * (std::pow(1.0 + r, per - 1) - 1.0) / r);
                total += pmt - bal * r;
            }
            return Value::num(total);
        });

        // --- 折旧 ---
        registerFunction("SLN", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double cost, sal, life;
            if (!n1(a[0], cost) || !n1(a[1], sal) || !n1(a[2], life)) return Value::error(Err::Value);
            if (life == 0) return Value::error(Err::Div0);
            return Value::num((cost - sal) / life);
        });
        registerFunction("SYD", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double cost, sal, life, per;
            if (!n1(a[0], cost) || !n1(a[1], sal) || !n1(a[2], life) || !n1(a[3], per))
                return Value::error(Err::Value);
            if (life <= 0 || per < 1 || per > life) return Value::error(Err::Num);
            double n = (life * (life + 1.0)) / 2.0;
            return Value::num((cost - sal) * (life - per + 1.0) / n);
        });
        registerFunction("DB", 4, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double cost, sal, life, per; Value e;
            if (!n1(a[0], cost) || !n1(a[1], sal) || !n1(a[2], life) || !n1(a[3], per))
                return Value::error(Err::Value);
            int month = 12;
            if (a.size() >= 5 && !i1(a[4], month, e)) return e;
            if (life <= 0 || per < 1 || (per > life && !(per <= life + 1 && month < 12)))
                return Value::error(Err::Num);
            double rate = 1.0 - std::pow(sal / cost, 1.0 / life);
            rate = std::floor(rate * 1000.0 + 0.5) / 1000.0;
            double total = 0, prior = 0;
            for (int p = 1; p <= (int)per; p++) {
                double depr;
                if (p == 1) depr = cost * rate * month / 12.0;
                else if (p <= (int)life) depr = (cost - prior) * rate;
                else depr = (cost - prior) * rate * (12.0 - month) / 12.0;
                if (p == (int)per) return Value::num(depr);
                prior += depr; total += depr;
            }
            return Value::num(0);
        });
        registerFunction("DDB", 4, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double cost, sal, life, per; Value e;
            if (!n1(a[0], cost) || !n1(a[1], sal) || !n1(a[2], life) || !n1(a[3], per))
                return Value::error(Err::Value);
            double factor = 2.0;
            if (a.size() >= 5 && !n1(a[4], factor)) return Value::error(Err::Value);
            if (life <= 0 || per < 1 || per > life || factor <= 0) return Value::error(Err::Num);
            double prior = 0, depr = 0;
            for (int p = 1; p <= (int)per; p++) {
                depr = std::min((cost - prior) * (factor / life), cost - sal - prior);
                if (p < (int)per) prior += depr;
            }
            return Value::num(std::max(0.0, depr));
        });
        // VDB(cost, salvage, life, start_period, end_period, [factor], [no_switch])
        // 最少 5 参。之前写成 4，而实现里访问 a[4] 读 end_period ——
        // 只传 4 个参数时越界读，直接段错误。
        registerFunction("VDB", 5, 7, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double cost, sal, life, start, end; Value e;
            if (!n1(a[0], cost) || !n1(a[1], sal) || !n1(a[2], life) ||
                !n1(a[3], start) || !n1(a[4], end)) return Value::error(Err::Value);
            // factor 是第 6 参（下标 5），不是第 5 参 ——
            // 之前写成 a.size()>=5 时用 a[4]，等于把 end_period 当成 factor 又读了一遍
            double factor = 2.0;
            if (a.size() >= 6 && !n1(a[5], factor)) return Value::error(Err::Value);
            if (life <= 0 || start > end) return Value::error(Err::Num);
            double total = 0, prior = 0;
            for (int p = 1; p <= (int)std::ceil(end); p++) {
                double depr = std::min((cost - prior) * (factor / life), cost - sal - prior);
                double s = std::max((double)p - 1, start);
                double en = std::min((double)p, end);
                if (en > s) total += depr * (en - s);
                prior += depr;
            }
            return Value::num(total);
        });

        // --- 利率换算 ---
        registerFunction("EFFECT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double nom; int npery; Value e;
            if (!n1(a[0], nom)) return Value::error(Err::Value);
            if (!i1(a[1], npery, e)) return e;
            if (nom <= 0 || npery < 1) return Value::error(Err::Num);
            return Value::num(std::pow(1.0 + nom / npery, npery) - 1.0);
        });
        registerFunction("NOMINAL", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double eff; int npery; Value e;
            if (!n1(a[0], eff)) return Value::error(Err::Value);
            if (!i1(a[1], npery, e)) return e;
            if (eff <= 0 || npery < 1) return Value::error(Err::Num);
            return Value::num((std::pow(eff + 1.0, 1.0 / npery) - 1.0) * npery);
        });
        registerFunction("RRI", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double n, pv, fv;
            if (!n1(a[0], n) || !n1(a[1], pv) || !n1(a[2], fv)) return Value::error(Err::Value);
            if (n <= 0 || pv <= 0 || fv <= 0) return Value::error(Err::Num);
            return Value::num(std::pow(fv / pv, 1.0 / n) - 1.0);
        });
        registerFunction("PDURATION", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double r, pv, fv;
            if (!n1(a[0], r) || !n1(a[1], pv) || !n1(a[2], fv)) return Value::error(Err::Value);
            if (r <= 0 || pv <= 0 || fv <= 0) return Value::error(Err::Num);
            return Value::num(std::log(fv / pv) / std::log(1.0 + r));
        });
        registerFunction("FVSCHEDULE", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double pv; if (!n1(a[0], pv)) return Value::error(Err::Value);
            std::vector<double> rates; Value e;
            col2({a[1]}, rates, e); if (e.isError()) return e;
            double v = pv;
            for (auto r : rates) v *= (1.0 + r);
            return Value::num(v);
        });

        // --- 国库券 ---
        registerFunction("TBILLEQ", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, disc;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], disc)) return Value::error(Err::Value);
            double days = mt - st;
            if (days <= 0 || days > 365) return Value::error(Err::Num);
            if (disc <= 0) return Value::error(Err::Num);
            // 等价收益率： TBILLEQ = 365*discount / (360 - discount*DSM)
            return Value::num(365.0 * disc / (360.0 - disc * days));
        });
        registerFunction("TBILLPRICE", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, disc;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], disc)) return Value::error(Err::Value);
            double days = mt - st;
            if (days <= 0 || days > 365) return Value::error(Err::Num);
            return Value::num(100.0 * (1.0 - disc * days / 360.0));
        });
        registerFunction("TBILLYIELD", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, pr;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], pr)) return Value::error(Err::Value);
            double days = mt - st;
            if (days <= 0 || days > 365 || pr <= 0) return Value::error(Err::Num);
            return Value::num((100.0 - pr) / pr * 360.0 / days);
        });

        // --- 分数美元 ---
        registerFunction("DOLLARDE", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double fd, fr;
            if (!n1(a[0], fd) || !n1(a[1], fr)) return Value::error(Err::Value);
            int f = (int)std::floor(fr);
            if (f <= 0) return Value::error(Err::Div0);
            int whole = (int)std::floor(fd);
            double frac = fd - whole;
            double div = std::pow(10.0, std::ceil(std::log10((double)f)));
            return Value::num(whole + frac * div / f);
        });
        registerFunction("DOLLARFR", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double dd, fr;
            if (!n1(a[0], dd) || !n1(a[1], fr)) return Value::error(Err::Value);
            int f = (int)std::floor(fr);
            if (f <= 0) return Value::error(Err::Div0);
            int whole = (int)std::floor(dd);
            double frac = dd - whole;
            double div = std::pow(10.0, std::ceil(std::log10((double)f)));
            return Value::num(whole + frac * f / div);
        });

        // --- 证券（简化实现，见报告说明）---
        registerFunction("ACCRINT", 6, 8, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double issue, first, settle, rate, par; Value e;
            if (!valueToSerial(a[0], issue) && !n1(a[0], issue)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], first) && !n1(a[1], first)) return Value::error(Err::Value);
            if (!valueToSerial(a[2], settle) && !n1(a[2], settle)) return Value::error(Err::Value);
            if (!n1(a[3], rate) || !n1(a[4], par)) return Value::error(Err::Value);
            int freq; if (!i1(a[5], freq, e)) return e;
            int basis = 0;
            if (a.size() >= 7) { basis = basisOf(a, 6); if (basis < 0) return Value::error(Err::Num); }
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            if (rate <= 0 || par <= 0) return Value::error(Err::Num);
            return Value::num(par * rate * yearFrac(issue, settle, basis));
        });
        registerFunction("ACCRINTM", 4, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double issue, settle, rate, par;
            if (!valueToSerial(a[0], issue) && !n1(a[0], issue)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], settle) && !n1(a[1], settle)) return Value::error(Err::Value);
            if (!n1(a[2], rate) || !n1(a[3], par)) return Value::error(Err::Value);
            int basis = 0;
            if (a.size() >= 5) { basis = basisOf(a, 4); if (basis < 0) return Value::error(Err::Num); }
            if (rate <= 0 || par <= 0 || issue > settle) return Value::error(Err::Num);
            return Value::num(par * rate * yearFrac(issue, settle, basis));
        });
        registerFunction("DISC", 4, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, pr, red;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], pr) || !n1(a[3], red)) return Value::error(Err::Value);
            if (pr <= 0 || red <= 0 || st >= mt) return Value::error(Err::Num);
            int basis = 0;
            if (a.size() >= 5) { basis = basisOf(a, 4); if (basis < 0) return Value::error(Err::Num); }
            // 原先完全忽略第 5 个参数 basis，一律按 30/360 算：
            //   DISC(2024-1-1, 2024-7-1, 97, 100, 1) = 0.03 × 360/180 = 0.06
            //   正确（basis=1 实际天数/闰年 366）    = 0.03 × 366/182 = 0.060330
            // 判据是 DISC 与 PRICEDISC 必须互逆：把 DISC 的结果喂回 PRICEDISC
            // 应还原成原价 97，而 0.06 还原出的是 97.0164。
            double frac = yearFrac(st, mt, basis);
            if (!(frac > 0)) return Value::error(Err::Num);
            return Value::num((red - pr) / red / frac);
        });
        registerFunction("INTRATE", 4, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, inv, red;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], inv) || !n1(a[3], red)) return Value::error(Err::Value);
            int basis = 0;
            if (a.size() >= 5) { basis = basisOf(a, 4); if (basis < 0) return Value::error(Err::Num); }
            if (inv <= 0 || red <= 0 || st >= mt) return Value::error(Err::Num);
            double frac = yearFrac(st, mt, basis);
            if (frac == 0) return Value::error(Err::Div0);
            return Value::num((red - inv) / inv / frac);
        });
        registerFunction("RECEIVED", 4, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, inv, disc;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], inv) || !n1(a[3], disc)) return Value::error(Err::Value);
            int basis = 0;
            if (a.size() >= 5) { basis = basisOf(a, 4); if (basis < 0) return Value::error(Err::Num); }
            if (inv <= 0 || disc <= 0 || st >= mt) return Value::error(Err::Num);
            double frac = yearFrac(st, mt, basis);
            return Value::num(inv / (1.0 - disc * frac));
        });
        registerFunction("PRICEDISC", 4, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, disc, red;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], disc) || !n1(a[3], red)) return Value::error(Err::Value);
            int basis = 0;
            if (a.size() >= 5) { basis = basisOf(a, 4); if (basis < 0) return Value::error(Err::Num); }
            if (disc <= 0 || red <= 0 || st >= mt) return Value::error(Err::Num);
            return Value::num(red - red * disc * yearFrac(st, mt, basis));
        });
        registerFunction("PRICEMAT", 5, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, issue, rate, yld;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!valueToSerial(a[2], issue) && !n1(a[2], issue)) return Value::error(Err::Value);
            if (!n1(a[3], rate) || !n1(a[4], yld)) return Value::error(Err::Value);
            int basis = 0;
            if (a.size() >= 6) { basis = basisOf(a, 5); if (basis < 0) return Value::error(Err::Num); }
            if (rate < 0 || yld < 0 || st >= mt) return Value::error(Err::Num);
            double dim = yearFrac(st, mt, basis);
            double a2 = yearFrac(issue, st, basis);
            double b2 = yearFrac(issue, mt, basis);
            return Value::num((100.0 + rate * 100.0 * b2) / (1.0 + yld * dim) - rate * 100.0 * a2);
        });
        // 付息周期辅助
        registerFunction("COUPDAYS", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt; int freq; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!i1(a[2], freq, e)) return e;
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            int basis = (a.size() >= 4) ? basisOf(a, 3) : 0;
            if (basis < 0) return Value::error(Err::Num);
            if (st >= mt) return Value::error(Err::Num);
            // COUPDAYS 是"结算日所在付息期"的天数，**不是** 365/freq。
            // 原先完全忽略 st 和 mt，一律返回 365/freq：
            //   COUPDAYS(2024-3-1, 2025-1-1, 2, 1) = 182.5（=365/2）
            // 而同一付息期的另两个函数给出 COUPDAYBS=60、COUPDAYSNC=122，
            // 两者相加 = 182 ≠ 182.5 —— 自相矛盾，且 COUPNCD-COUPPCD
            // 也是 45474-45292 = 182，同样对不上。
            //
            // 正确做法：定位结算日所在的付息期，按 basis 的日期规则算区间天数。
            if (basis == 0 || basis == 4) {
                // 30/360：付息期长度恰为 360/freq（每月固定 30 天）
                return Value::num(360.0 / freq);
            }
            // basis 1（actual/actual）、2（actual/360）、3（actual/365）：
            // 取结算日所在付息期的两个端点，用实际天数
            double pS, nS;
            if (!couponPeriod(st, mt, freq, pS, nS)) return Value::error(Err::Num);
            if (nS > mt + 1e-9) nS = mt;
            return Value::num(std::max(1.0, nS - pS));
        });
        registerFunction("COUPNUM", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt; int freq; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!i1(a[2], freq, e)) return e;
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            if (st >= mt) return Value::error(Err::Num);
            // 按付息周期逐档推进计数，而不是用平均月长再 ceil ——
            // 后者跨闰年会多算一期：实测 COUPNUM(2024-1-1,2025-1-1,2)
            // 得到 3（366/30.4375/6 = 2.004，ceil 成 3），正确是 2。
            // 这里复用 COUPNCD 的付息日步进方式：先找到第一个晚于结算日的
            // 付息日，再一路数到到期日为止（到期日本身也算一期）。
            YMD sy = serialToYMD(std::floor(st)), my = serialToYMD(std::floor(mt));
            int step = 12 / freq;
            int m = ((sy.m - 1) / step) * step + 1 + step;
            int y = sy.y;
            if (m > 12) { m -= 12; y++; }
            double r = ymdToSerial(y, m, my.d);
            int guard = 0;
            while (r <= st && guard++ < 10000) { m += step; if (m > 12) { m -= 12; y++; } r = ymdToSerial(y, m, my.d); }
            int n = 0; guard = 0;
            while (r <= mt + 1e-9 && guard++ < 10000) {
                n++;
                m += step; if (m > 12) { m -= 12; y++; }
                r = ymdToSerial(y, m, my.d);
            }
            return Value::num((double)n);
        });
        registerFunction("COUPDAYBS", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt; int freq; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!i1(a[2], freq, e)) return e;
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            double pS, nS;
            if (!couponPeriod(st, mt, freq, pS, nS)) return Value::error(Err::Num);
            return Value::num(std::max(0.0, st - pS));
        });
        registerFunction("COUPDAYSNC", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt; int freq; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!i1(a[2], freq, e)) return e;
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            double pS, nS;
            if (!couponPeriod(st, mt, freq, pS, nS)) return Value::error(Err::Num);
            return Value::num(std::max(0.0, std::min(nS, mt) - st));
        });
        registerFunction("COUPNCD", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt; int freq; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!i1(a[2], freq, e)) return e;
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            // 同样改为以到期日为锚点，与 COUPDAYS/COUPDAYBS/COUPDAYSNC 一致
            double pS, nS;
            if (!couponPeriod(st, mt, freq, pS, nS)) return Value::error(Err::Num);
            if (nS <= st + 1e-9) nS = pS + (nS - pS);   // 结算日恰在付息日时取下一期
            return Value::num(std::min(nS, mt));
        });
        registerFunction("COUPPCD", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt; int freq; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!i1(a[2], freq, e)) return e;
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            double pS, nS;
            if (!couponPeriod(st, mt, freq, pS, nS)) return Value::error(Err::Num);
            return Value::num(pS);
        });
        // PRICE / YIELD：按标准债券定价公式（年/半年/季付息）
        registerFunction("PRICE", 6, 7, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, rate, yld, red; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            // 参数顺序是 (settlement, maturity, rate, yld, redemption, frequency, [basis])：
            // redemption 在前、frequency 在后。写反会让 freq 拿到面值 100 而报 #NUM!。
            if (!n1(a[2], rate) || !n1(a[3], yld) || !n1(a[4], red)) return Value::error(Err::Value);
            int freq; if (!i1(a[5], freq, e)) return e;
            int basis = (a.size() >= 7) ? basisOf(a, 6) : 0;
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            if (rate < 0 || yld < 0 || st >= mt) return Value::error(Err::Num);
            if (basis < 0) return Value::error(Err::Num);
            double N = std::max(1.0, std::ceil((mt - st) / (365.25 / freq)));
            double dim = yearFrac(st, mt, basis);
            double dsc = 100.0 * rate / freq;
            double p = 0;
            for (int k = 1; k <= (int)N; k++)
                p += dsc / std::pow(1.0 + yld / freq, (double)k);
            p += red / std::pow(1.0 + yld / freq, N);
            // 减去应计利息（简化：按 basis 1 的线性应计）
            double accrued = 100.0 * rate / freq * (1.0 - dim * freq + std::floor(dim * freq));
            (void)dsc;
            return Value::num(p - accrued);
        });
        registerFunction("YIELD", 6, 7, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, rate, pr, red; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            // 与 PRICE 同样的顺序：redemption(a[4]) 在 frequency(a[5]) 之前
            if (!n1(a[2], rate) || !n1(a[3], pr) || !n1(a[4], red)) return Value::error(Err::Value);
            int freq; if (!i1(a[5], freq, e)) return e;
            if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
            if (rate < 0 || pr <= 0 || red <= 0 || st >= mt) return Value::error(Err::Num);
            std::vector<Value> args = a;
            args[3] = Value::num(0.05);
            double root;
            bool ok = fin::solve([&](double y) {
                std::vector<Value> aa = a;
                std::vector<Value> pArgs = {a[0], a[1], a[2], Value::num(y), a[4], a[5]};
                if (a.size() >= 7) pArgs.push_back(a[6]);
                Value pv = findFunction("PRICE")->impl(pArgs, *(EvalCtx*)nullptr);
                if (pv.isError()) return 1e300;
                return pv.n - pr;
            }, root, 0.0, 1.0);
            if (!ok) return Value::error(Err::Num);
            return Value::num(root);
        });
    }
};
static FinRegistrar g_finReg;

} // namespace xl
