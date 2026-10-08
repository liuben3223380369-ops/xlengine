// 补齐批次 C：分布函数补全 + 数据库函数补全 + 舍入/位置补全
//
// 这一批的共同点：都是"配套完整性"缺口 ——
// 有 .RT 却没有正向版（CHISQ.DIST / F.INV），有 DSUM 却没有 DGET。
// 单独看每个都不起眼，但缺了会让真实表格出现 #NAME?。
#include "functions.hpp"
#include "value.hpp"
#include "ast.hpp"
#include "dist.hpp"
#include "date.hpp"
#include <functional>
#include <cmath>
#include <vector>
#include <map>
#include <algorithm>

namespace xl {

namespace {

bool numArg(const Value& v, double& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    Value e;
    if (!toNumber(v, out, e)) { err = e; return false; }
    return true;
}
void nums(const std::vector<Value>& a, std::vector<double>& out, Value& err) {
    std::vector<Value> v;
    if (!flattenArgs(a, v, true, err)) return;
    for (auto& x : v) { double d; Value e; if (toNumber(x, d, e)) out.push_back(d); }
}

// 卡方分布 CDF：P(X<=x) = P(df/2, x/2)
double chiCdf(double x, double df) {
    if (x <= 0) return 0.0;
    return dist::gammp(df / 2.0, x / 2.0);
}
double chiPdf(double x, double df) {
    if (x <= 0) return 0.0;
    return std::exp((df / 2.0 - 1.0) * std::log(x) - x / 2.0
                    - (df / 2.0) * std::log(2.0) - std::lgamma(df / 2.0));
}
// F 分布 CDF：用不完全 beta
double fCdf(double x, double d1, double d2) {
    if (x <= 0) return 0.0;
    return dist::betai(d1 / 2.0, d2 / 2.0, d1 * x / (d1 * x + d2));
}
double fPdf(double x, double d1, double d2) {
    if (x <= 0) return 0.0;
    double lg = std::lgamma((d1 + d2) / 2.0) - std::lgamma(d1 / 2.0) - std::lgamma(d2 / 2.0);
    return std::exp(lg + (d1 / 2.0) * std::log(d1 / d2) + (d1 / 2.0 - 1.0) * std::log(x)
                    - ((d1 + d2) / 2.0) * std::log1p(d1 * x / d2));
}

// 反函数：单调二分求根
double invertMono(std::function<double(double)> f, double target, double lo, double hi) {
    for (int i = 0; i < 200; i++) {
        double mid = (lo + hi) / 2;
        double fm = f(mid);
        if (std::fabs(fm - target) < 1e-14) return mid;
        if (fm < target) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2;
}

} // namespace

struct Fill3Registrar {
    Fill3Registrar();
};
static Fill3Registrar g_fill3Reg;

Fill3Registrar::Fill3Registrar() {
    // -----------------------------------------------------------------------
    // 分布函数补全
    // -----------------------------------------------------------------------
    // CHISQ.DIST(x, df, cumulative)
    registerFunction("CHISQ.DIST", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; double x, df, cum;
        if (!numArg(a[0], x, e)) return e;
        if (!numArg(a[1], df, e)) return e;
        if (!numArg(a[2], cum, e)) return e;
        if (x < 0 || df < 1 || df > 1e10) return Value::error(Err::Num);
        return Value::num(cum != 0 ? chiCdf(x, df) : chiPdf(x, df));
    });
    // CHISQ.INV(p, df)
    registerFunction("CHISQ.INV", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; double p, df;
        if (!numArg(a[0], p, e)) return e;
        if (!numArg(a[1], df, e)) return e;
        if (p < 0 || p > 1 || df < 1) return Value::error(Err::Num);
        if (p == 0) return Value::num(0);
        // 上界随 df 放大，避免大自由度时二分区间不够
        double hi = std::max(100.0, df * 10.0);
        return Value::num(invertMono([&](double x) { return chiCdf(x, df); }, p, 0.0, hi));
    });
    // F.INV(p, df1, df2)：与已有 F.INV.RT 互补
    registerFunction("F.INV", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; double p, d1, d2;
        if (!numArg(a[0], p, e)) return e;
        if (!numArg(a[1], d1, e)) return e;
        if (!numArg(a[2], d2, e)) return e;
        if (p < 0 || p > 1 || d1 < 1 || d2 < 1) return Value::error(Err::Num);
        if (p == 0) return Value::num(0);
        return Value::num(invertMono([&](double x) { return fCdf(x, d1, d2); }, p, 0.0, 1e6));
    });
    // F.DIST 的正向版（若已存在则会覆盖，这里只补 PDF 分支的健壮性）
    registerFunction("F.DIST", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; double x, d1, d2, cum;
        if (!numArg(a[0], x, e)) return e;
        if (!numArg(a[1], d1, e)) return e;
        if (!numArg(a[2], d2, e)) return e;
        if (!numArg(a[3], cum, e)) return e;
        if (x < 0 || d1 < 1 || d2 < 1) return Value::error(Err::Num);
        return Value::num(cum != 0 ? fCdf(x, d1, d2) : fPdf(x, d1, d2));
    });
    // GAMMALN.PRECISE 与 GAMMALN 同义（Excel 保留两者）
    registerFunction("GAMMALN.PRECISE", 1, 1, [](const std::vector<Value>& a, EvalCtx& c) -> Value {
        return findFunction("GAMMALN")->impl(a, c);
    });
    // 旧式兼容名：Excel 为向后兼容保留，老表格里很常见
    registerFunction("CHIDIST", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; double x, df;
        if (!numArg(a[0], x, e)) return e;
        if (!numArg(a[1], df, e)) return e;
        if (x < 0 || df < 1) return Value::error(Err::Num);
        return Value::num(1.0 - chiCdf(x, df));          // = CHISQ.DIST.RT
    });
    registerFunction("CHIINV", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; double p, df;
        if (!numArg(a[0], p, e)) return e;
        if (!numArg(a[1], df, e)) return e;
        if (p < 0 || p > 1 || df < 1) return Value::error(Err::Num);
        // invertMono 要求 f 单调递增（fm < target 时抬 lo）。
        // 传 1-CDF（递减）会让二分方向整个反过来，收敛到区间上界 ——
        // 实测 CHIINV(0.5,5) 因此返回 100，而正确值是与 CHISQ.INV.RT
        // 相同的 4.35146。改成对递增的 CDF 求 1-p 的分位点。
        if (p == 0) return Value::num(0);
        return Value::num(invertMono([&](double x) { return chiCdf(x, df); }, 1.0 - p, 0.0,
                                     std::max(100.0, df * 10.0)));
    });
    registerFunction("LOGINV", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) -> Value {
        // LOGINV(p, mean, sd) = LOGNORM.INV(p, mean, sd)
        std::vector<Value> b = {a[0], a[1], a[2]};
        return findFunction("LOGNORM.INV")->impl(b, c);
    });

    // -----------------------------------------------------------------------
    // 数据库函数补全
    // 三个参数的语义：database（含首行字段名）、field（列名或序号）、criteria
    // -----------------------------------------------------------------------
    // 取 field 指定列在 database 中的下标
    auto fieldIndex = [](const Value& db, const Value& field, int& out, Value& err) -> bool {
        if (!db.isArray() || !db.arr || db.arr->empty()) { err = Value::error(Err::Value); return false; }
        const Array2D& rows = *db.arr;
        int idx = -1;
        if (field.isStr()) {
            std::string name = field.s;
            std::transform(name.begin(), name.end(), name.begin(), ::toupper);
            const auto& header = rows[0];
            for (size_t i = 0; i < header.size(); i++) {
                std::string h; Value e;
                if (!toText(header[i], h, e)) continue;
                std::transform(h.begin(), h.end(), h.begin(), ::toupper);
                if (h == name) { idx = (int)i; break; }
            }
        } else {
            double d;
            if (!numArg(field, d, err)) return false;
            idx = (int)std::floor(d) - 1;
        }
        if (idx < 0 || (size_t)idx >= rows[0].size()) { err = Value::error(Err::Value); return false; }
        out = idx;
        return true;
    };
    // 条件区：第一行是字段名，其余行是"或"关系，同一行内是"与"关系
    auto matchRow = [](const Array2D& db, size_t r, const Value& crit, Value& err) -> bool {
        if (!crit.isArray() || !crit.arr || crit.arr->empty()) return true;
        const Array2D& c = *crit.arr;
        const auto& header = db[0];
        for (size_t ci = 0; ci < c[0].size(); ci++) {
            std::string fname; Value e2;
            if (!toText(c[0][ci], fname, e2)) continue;
            if (fname.empty()) continue;
            std::transform(fname.begin(), fname.end(), fname.begin(), ::toupper);
            int col = -1;
            for (size_t i = 0; i < header.size(); i++) {
                std::string h; Value e3;
                if (!toText(header[i], h, e3)) continue;
                std::transform(h.begin(), h.end(), h.begin(), ::toupper);
                if (h == fname) { col = (int)i; break; }
            }
            if (col < 0) continue;                              // 条件列不在库里：忽略
            // 该列的条件可能有多行，任意一行满足即可（"或"）
            bool anyRow = false;
            for (size_t ri = 1; ri < c.size(); ri++) {
                Value cellV = (col < (int)db[r].size()) ? db[r][col] : Value::empty();
                std::string critText; Value e4;
                bool hasCrit = (ci < c[ri].size()) && toText(c[ri][ci], critText, e4) && !critText.empty();
                if (!hasCrit) { anyRow = true; break; }         // 空条件行 = 全匹配
                if (matchCrit(cellV, critText, err)) { anyRow = true; break; }
                if (err.isError()) return false;
            }
            if (!anyRow) return false;                          // 该列所有条件都不满足
        }
        return true;
    };
    // 遍历满足条件的行，收集 field 列的数值
    auto collect = [&](const std::vector<Value>& a, std::vector<double>& out, Value& err) -> bool {
        int fi;
        if (!fieldIndex(a[0], a[1], fi, err)) return false;
        const Array2D& rows = *a[0].arr;
        for (size_t r = 1; r < rows.size(); r++) {
            if (!matchRow(rows, r, a[2], err)) { if (err.isError()) return false; continue; }
            Value v = (fi < (int)rows[r].size()) ? rows[r][fi] : Value::empty();
            double d; Value e2;
            if (toNumber(v, d, e2)) out.push_back(d);
        }
        return true;
    };
    auto collectAll = [&](const std::vector<Value>& a, std::vector<Value>& out, Value& err) -> bool {
        int fi;
        if (!fieldIndex(a[0], a[1], fi, err)) return false;
        const Array2D& rows = *a[0].arr;
        for (size_t r = 1; r < rows.size(); r++) {
            if (!matchRow(rows, r, a[2], err)) { if (err.isError()) return false; continue; }
            if (fi < (int)rows[r].size()) out.push_back(rows[r][fi]);
        }
        return true;
    };

    registerFunction("DCOUNTA", 3, 3, [&](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; std::vector<Value> vs;
        if (!collectAll(a, vs, e)) return e;
        size_t n = 0;
        for (auto& v : vs) if (!v.isEmpty()) n++;
        return Value::num((double)n);
    });
    registerFunction("DSTDEVP", 3, 3, [&](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; std::vector<double> v;
        if (!collect(a, v, e)) return e;
        if (v.empty()) return Value::error(Err::Div0);
        double m = 0; for (double x : v) m += x; m /= v.size();
        double s = 0; for (double x : v) s += (x - m) * (x - m);
        return Value::num(std::sqrt(s / v.size()));
    });
    registerFunction("DVARP", 3, 3, [&](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; std::vector<double> v;
        if (!collect(a, v, e)) return e;
        if (v.empty()) return Value::error(Err::Div0);
        double m = 0; for (double x : v) m += x; m /= v.size();
        double s = 0; for (double x : v) s += (x - m) * (x - m);
        return Value::num(s / v.size());
    });
    registerFunction("DGET", 3, 3, [&](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; std::vector<Value> vs;
        if (!collectAll(a, vs, e)) return e;
        if (vs.empty()) return Value::error(Err::Value);        // 无匹配 -> #VALUE!
        if (vs.size() > 1) return Value::error(Err::Num);       // 多匹配 -> #NUM!
        return vs[0];
    });

    // -----------------------------------------------------------------------
    // 位置 / 舍入补全
    // -----------------------------------------------------------------------
    registerFunction("TRIMRANGE", 1, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        // 去掉区域边缘的空行空列。Excel 2024 新增，用于让动态数组随数据收缩。
        if (!a[0].isArray() || !a[0].arr) return a[0];
        const Array2D& src = *a[0].arr;
        size_t r0 = 0, r1 = src.size(), c0 = 0, c1 = src.empty() ? 0 : src[0].size();
        auto rowEmpty = [&](size_t r) {
            for (size_t c = 0; c < src[r].size(); c++) if (!src[r][c].isEmpty()) return false;
            return true;
        };
        while (r0 < r1 && rowEmpty(r0)) r0++;
        while (r1 > r0 && rowEmpty(r1 - 1)) r1--;
        if (r0 >= r1) return Value::array(std::make_shared<Array2D>());
        auto colEmpty = [&](size_t c) {
            for (size_t r = r0; r < r1; r++)
                if (c < src[r].size() && !src[r][c].isEmpty()) return false;
            return true;
        };
        while (c0 < c1 && colEmpty(c0)) c0++;
        while (c1 > c0 && colEmpty(c1 - 1)) c1--;
        auto out = std::make_shared<Array2D>();
        for (size_t r = r0; r < r1; r++) {
            std::vector<Value> row;
            for (size_t c = c0; c < c1; c++)
                row.push_back(c < src[r].size() ? src[r][c] : Value::empty());
            out->push_back(std::move(row));
        }
        return Value::array(out);
    });
    registerFunction("PERCENTOF", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        // IronCalc 增加的便捷函数：部分 / 整体
        Value e; std::vector<double> part, whole;
        nums({a[0]}, part, e); if (e.isError()) return e;
        nums({a[1]}, whole, e); if (e.isError()) return e;
        double ps = 0, ws = 0;
        for (double x : part) ps += x;
        for (double x : whole) ws += x;
        if (ws == 0) return Value::error(Err::Div0);
        return Value::num(ps / ws);
    });
    registerFunction("AMORLINC", 6, 7, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        // 法国会计线性折旧：与 AMORLCOST 的区别是首期按天数比例
        Value e; double cost, rate;
        double purch, first, salvage, period;
        if (!valueToSerial(a[0], purch)) return Value::error(Err::Value);
        if (!valueToSerial(a[1], first)) return Value::error(Err::Value);
        if (!valueToSerial(a[2], salvage)) return Value::error(Err::Value);
        if (!numArg(a[3], cost, e)) return e;
        if (!numArg(a[4], period, e)) return e;
        if (!numArg(a[5], rate, e)) return e;
        if (rate <= 0 || cost <= 0) return Value::error(Err::Num);
        // 折旧期 = 1/rate（向上取整到月）
        double lifeMonths = std::ceil(1.0 / rate);
        double lifeDays = lifeMonths * 30.0;
        if (period < 0 || period > lifeMonths) return Value::error(Err::Num);
        if (period == 0) {
            // 首期按购买日到第一期末的天数比例
            double days = first - purch;
            if (days < 0) return Value::error(Err::Num);
            return Value::num(cost * rate * days / lifeDays * 12.0 / 12.0);
        }
        return Value::num(cost * rate);
    });
}

} // namespace xl
