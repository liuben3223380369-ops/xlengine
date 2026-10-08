#include "functions.hpp"
#include "date.hpp"
#include "dist.hpp"
#include <algorithm>
#include <random>
#include <map>

namespace xl {

static bool n1(const Value& v, double& out) {
    Value e;
    if (!toNumber(v, out, e)) return false;
    return true;
}

static bool i1(const Value& v, int& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    double d; Value e;
    if (!toNumber(v, d, e)) { err = e; return false; }
    out = (int)std::floor(d);
    return true;
}
// 收集成 vector<double>。
//
// 原实现走 flattenArgs 先攒 vector<Value>（每元素 104 字节）再转 double ——
// 中间那层完全没必要。ctx 非空且参数是惰性区域时，直接按坐标遍历取数，
// 连区域的 Array2D 展开都省掉。
static void col2(const std::vector<Value>& a, std::vector<double>& out, Value& err,
                 EvalCtx* ctx = nullptr) {
    if (ctx) {
        bool anyRef = false;
        for (const Value& x : a) if (x.isRangeRef()) { anyRef = true; break; }
        if (anyRef) {
            Value e2;
            bool ok = forEachNumStat(a, *ctx, [&](double d){ out.push_back(d); }, e2);
            if (!ok) { err = e2; return; }
            return;
        }
    }
    std::vector<Value> v;
    if (!flattenArgs(a, v, true, err)) return;
    for (auto& x : v) out.push_back(x.n);
}

// 分布数学辅助见 dist.hpp


// ---------------------------------------------------------------------------
// 条件匹配（SUMIF / COUNTIF 家族共用）
// ---------------------------------------------------------------------------
static bool wildMatch(const std::string& pat, const std::string& txt) {
    // 支持 * 和 ? 通配符，大小写不敏感
    size_t pi = 0, ti = 0, star = std::string::npos, match = 0;
    auto lower = [](char c) { return (char)std::tolower((unsigned char)c); };
    while (ti < txt.size()) {
        if (pi < pat.size() && (pat[pi] == '?' || lower(pat[pi]) == lower(txt[ti]))) { pi++; ti++; }
        else if (pi < pat.size() && pat[pi] == '*') { star = pi++; match = ti; }
        else if (star != std::string::npos) { pi = star + 1; ti = ++match; }
        else return false;
    }
    while (pi < pat.size() && pat[pi] == '*') pi++;
    return pi == pat.size();
}

bool matchCrit(const Value& cellV, const std::string& crit, Value& err) {
    std::string c = crit;
    // 去首尾空格
    while (!c.empty() && std::isspace((unsigned char)c.front())) c.erase(c.begin());
    while (!c.empty() && std::isspace((unsigned char)c.back())) c.pop_back();

    std::string op;
    std::string rest = c;
    if (c.size() >= 2 && (c[0] == '>' || c[0] == '<') && (c[1] == '=' || c[1] == '>')) { op = c.substr(0, 2); rest = c.substr(2); }
    else if (c.size() >= 1 && (c[0] == '>' || c[0] == '<' || c[0] == '=')) { op = c.substr(0, 1); rest = c.substr(1); }

    // 尝试把比较目标解析为数字
    double targetNum = 0; bool isNum = textToNumber(rest, targetNum);

    if (op.empty()) {
        // 无运算符：相等比较
        if (cellV.isNum() && isNum) return cellV.n == targetNum;
        std::string cs; Value e;
        if (!toText(cellV, cs, e)) { err = e; return false; }
        return wildMatch(rest, cs);
    }
    // 有运算符：必须双方都可转数字，否则只对文本做字典序比较
    double cv; bool cvIsNum = n1(cellV, cv);
    if (isNum && cvIsNum) {
        if (op == "=")  return cv == targetNum;
        if (op == "<>") return cv != targetNum;
        if (op == ">")  return cv >  targetNum;
        if (op == "<")  return cv <  targetNum;
        if (op == ">=") return cv >= targetNum;
        if (op == "<=") return cv <= targetNum;
    }
    std::string cs; Value e;
    if (!toText(cellV, cs, e)) { err = e; return false; }
    std::string rs = rest;
    std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);
    std::transform(rs.begin(), rs.end(), rs.begin(), ::toupper);
    if (op == "=")  return cs == rs;
    if (op == "<>") return cs != rs;
    if (op == ">")  return cs > rs;
    if (op == "<")  return cs < rs;
    if (op == ">=") return cs >= rs;
    if (op == "<=") return cs <= rs;
    return false;
}

// 把区域/值摊平成一维（保留空单元格占位，用于 SUMIF 的错位求和）
static void flatKeep(const Value& v, std::vector<Value>& out, Value& err) {
    if (v.isError()) { err = v; return; }
    if (v.isArray()) {
        if (!v.arr) return;
        for (auto& row : *v.arr)
            for (auto& x : row) {
                if (x.isError()) { err = x; return; }
                out.push_back(x);
            }
    } else out.push_back(v);
}
// 条件筛选：返回命中的下标
static bool critIndices(const Value& range, const std::string& crit,
                        std::vector<Value>& cells, std::vector<size_t>& idx, Value& err) {
    flatKeep(range, cells, err);
    if (err.isError()) return false;
    for (size_t i = 0; i < cells.size(); i++) {
        bool m = matchCrit(cells[i], crit, err);
        if (err.isError()) return false;
        if (m) idx.push_back(i);
    }
    return true;
}

// ===========================================================================
// 统计函数
// ===========================================================================
struct StatRegistrar {
    StatRegistrar() {
        registerFunction("STDEV.P", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.empty()) return Value::error(Err::Div0);
            double m = 0; for (auto x : v) m += x; m /= v.size();
            double s = 0; for (auto x : v) s += (x-m)*(x-m);
            return Value::num(std::sqrt(s / v.size()));
        });
        registerFunction("STDEV.S", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.size() < 2) return Value::error(Err::Div0);
            double m = 0; for (auto x : v) m += x; m /= v.size();
            double s = 0; for (auto x : v) s += (x-m)*(x-m);
            return Value::num(std::sqrt(s / (v.size()-1)));
        });
        registerFunction("STDEVP", 1, 255, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("STDEV.P")->impl(a, c); });
        registerFunction("VAR.P", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.empty()) return Value::error(Err::Div0);
            double m = 0; for (auto x : v) m += x; m /= v.size();
            double s = 0; for (auto x : v) s += (x-m)*(x-m);
            return Value::num(s / v.size());
        });
        registerFunction("VAR.S", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.size() < 2) return Value::error(Err::Div0);
            double m = 0; for (auto x : v) m += x; m /= v.size();
            double s = 0; for (auto x : v) s += (x-m)*(x-m);
            return Value::num(s / (v.size()-1));
        });
        registerFunction("VARP", 1, 255, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("VAR.P")->impl(a, c); });
        registerFunction("STDEVA", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> v;
            for (auto& x : a) {
                if (x.isArray()) { if (x.arr) for (auto& r : *x.arr) for (auto& c2 : r) {
                    if (c2.isError()) return c2;
                    if (c2.isEmpty()) continue;
                    if (c2.isStr()) { v.push_back(0); continue; }
                    double d; if (!n1(c2, d)) return Value::error(Err::Value);
                    v.push_back(d);
                } }
                else { double d; if (!n1(x, d)) return Value::error(Err::Value); v.push_back(d); }
            }
            if (v.size() < 2) return Value::error(Err::Div0);
            double m = 0; for (auto x : v) m += x; m /= v.size();
            double s = 0; for (auto x : v) s += (x-m)*(x-m);
            return Value::num(std::sqrt(s / (v.size()-1)));
        });
        registerFunction("VARA", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value sd = findFunction("STDEVA")->impl(a, *(EvalCtx*)nullptr);
            if (sd.isError()) return sd;
            return Value::num(sd.n * sd.n);
        });

        registerFunction("AVEDEV", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.empty()) return Value::error(Err::Num);
            double m = 0; for (auto x : v) m += x; m /= v.size();
            double s = 0; for (auto x : v) s += std::fabs(x-m);
            return Value::num(s / v.size());
        });
        registerFunction("DEVSQ", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            double m = 0; for (auto x : v) m += x; if (!v.empty()) m /= v.size();
            double s = 0; for (auto x : v) s += (x-m)*(x-m);
            return Value::num(s);
        });
        registerFunction("GEOMEAN", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.empty()) return Value::error(Err::Num);
            double s = 0; for (auto x : v) { if (x <= 0) return Value::error(Err::Num); s += std::log(x); }
            return Value::num(std::exp(s / v.size()));
        });
        registerFunction("HARMEAN", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.empty()) return Value::error(Err::Num);
            double s = 0; for (auto x : v) { if (x <= 0) return Value::error(Err::Num); s += 1.0/x; }
            return Value::num(v.size() / s);
        });
        registerFunction("TRIMMEAN", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2({a[0]}, v, e, &ctx); if (e.isError()) return e;
            double pct; if (!n1(a[1], pct)) return Value::error(Err::Value);
            if (pct < 0 || pct >= 1) return Value::error(Err::Num);
            std::sort(v.begin(), v.end());
            int cut = (int)std::floor(v.size() * pct / 2.0);
            double s = 0;
            for (int i = cut; i < (int)v.size() - cut; i++) s += v[i];
            int cnt = (int)v.size() - 2 * cut;
            if (cnt <= 0) return Value::error(Err::Num);
            return Value::num(s / cnt);
        });
        registerFunction("KURT", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.size() < 4) return Value::error(Err::Div0);
            double m = 0; for (auto x : v) m += x; m /= v.size();
            double s2 = 0, s4 = 0;
            for (auto x : v) { s2 += (x-m)*(x-m); s4 += std::pow(x-m, 4); }
            // Excel 的 KURT 用**样本**标准差（除以 n-1）。
            // 用总体标准差（除以 n）会让结果整体偏大：实测
            // KURT({1,2,3,4,5}) 得到 2.625，而 Excel 是 -1.2。
            // 这与 SKEW 是同一类错，之前 SKEW 已修正过。
            double sd = std::sqrt(s2 / (v.size() - 1));
            double n = v.size();
            return Value::num((n*(n+1)/((n-1)*(n-2)*(n-3))) * (s4/std::pow(sd,4))
                              - 3.0*std::pow(n-1,2)/((n-2)*(n-3)));
        });
        registerFunction("SKEW", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.size() < 3) return Value::error(Err::Div0);
            double m = 0; for (auto x : v) m += x; m /= v.size();
            double s2 = 0, s3 = 0;
            for (auto x : v) { s2 += (x-m)*(x-m); s3 += std::pow(x-m, 3); }
            if (s2 == 0) return Value::error(Err::Div0);
            double n = v.size();
            // Excel SKEW 用样本标准差 s = sqrt(Σ(x-x̄)²/(n-1))，不是总体标准差
            return Value::num((n/((n-1)*(n-2))) * (s3 / std::pow(s2/(n-1), 1.5)));
        });
        registerFunction("MODE.SNGL", 1, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2(a, v, e, &ctx); if (e.isError()) return e;
            if (v.empty()) return Value::error(Err::NA);
            std::map<double,int> cnt;
            for (auto x : v) cnt[x]++;
            double best = v[0]; int bc = 0;
            for (auto& kv : cnt) if (kv.second > bc) { bc = kv.second; best = kv.first; }
            if (bc <= 1) return Value::error(Err::NA);
            return Value::num(best);
        });
        registerFunction("MODE", 1, 255, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("MODE.SNGL")->impl(a, c); });

        // --- 百分位 / 四分位 / 排名 ---
        registerFunction("PERCENTILE.INC", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2({a[0]}, v, e, &ctx); if (e.isError()) return e;
            double k; if (!n1(a[1], k)) return Value::error(Err::Value);
            if (v.empty() || k < 0 || k > 1) return Value::error(Err::Num);
            std::sort(v.begin(), v.end());
            double pos = (v.size() - 1) * k;
            int lo = (int)std::floor(pos); double frac = pos - lo;
            if (lo + 1 >= (int)v.size()) return Value::num(v.back());
            return Value::num(v[lo] + frac * (v[lo+1] - v[lo]));
        });
        registerFunction("PERCENTILE.EXC", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2({a[0]}, v, e, &ctx); if (e.isError()) return e;
            double k; if (!n1(a[1], k)) return Value::error(Err::Value);
            if (v.size() < 2 || k <= 0 || k >= 1) return Value::error(Err::Num);
            std::sort(v.begin(), v.end());
            double pos = (v.size() + 1) * k - 1;
            if (pos < 0) pos = 0;
            int lo = (int)std::floor(pos); double frac = pos - lo;
            if (lo + 1 >= (int)v.size()) return Value::num(v.back());
            return Value::num(v[lo] + frac * (v[lo+1] - v[lo]));
        });
        registerFunction("PERCENTILE", 2, 2, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("PERCENTILE.INC")->impl(a, c); });
        registerFunction("QUARTILE.INC", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int q; Value e; if (!i1(a[1], q, e)) return e;
            if (q < 0 || q > 4) return Value::error(Err::Num);
            std::vector<Value> args = {a[0], Value::num(q / 4.0)};
            return findFunction("PERCENTILE.INC")->impl(args, *(EvalCtx*)nullptr);
        });
        registerFunction("QUARTILE.EXC", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int q; Value e; if (!i1(a[1], q, e)) return e;
            if (q < 1 || q > 3) return Value::error(Err::Num);
            std::vector<Value> args = {a[0], Value::num(q / 4.0)};
            return findFunction("PERCENTILE.EXC")->impl(args, *(EvalCtx*)nullptr);
        });
        registerFunction("QUARTILE", 2, 2, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("QUARTILE.INC")->impl(a, c); });
        registerFunction("PERCENTRANK.INC", 2, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2({a[0]}, v, e, &ctx); if (e.isError()) return e;
            double x; if (!n1(a[1], x)) return Value::error(Err::Value);
            if (v.empty()) return Value::error(Err::NA);
            std::sort(v.begin(), v.end());
            int below = 0, above = 0;
            for (auto y : v) { if (y < x) below++; else if (y > x) above++; }
            int eq = (int)v.size() - below - above;
            if (eq == 0) return Value::error(Err::NA);
            return Value::num(below / (double)(v.size() - 1));
        });
        registerFunction("PERCENTRANK.EXC", 2, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2({a[0]}, v, e, &ctx); if (e.isError()) return e;
            double x; if (!n1(a[1], x)) return Value::error(Err::Value);
            if (v.empty()) return Value::error(Err::NA);
            std::sort(v.begin(), v.end());
            int below = 0, eq = 0;
            for (auto y : v) { if (y < x) below++; else if (y == x) eq++; }
            if (eq == 0) return Value::error(Err::NA);
            // EXC 版把区间分成 n+1 段，第 k 小的值对应 (k)/(n+1)，
            // 所以分子是「小于 x 的个数 + 1」。原先少加这个 1，结果整体
            // 偏小 1/(n+1)，且与 PERCENTILE.EXC 不再互逆：
            //   PERCENTILE.EXC({1,2,3,4},0.2)=1，但 PERCENTRANK.EXC(...,1)=0
            return Value::num((below + 1) / (double)(v.size() + 1));
        });
        registerFunction("PERCENTRANK", 2, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("PERCENTRANK.INC")->impl(a, c); });
        registerFunction("RANK.EQ", 2, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2({a[1]}, v, e, &ctx); if (e.isError()) return e;
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            int order = 0;
            if (a.size() >= 3 && !i1(a[2], order, e)) return e;
            int rank = 0;
            for (auto y : v) if (order == 0 ? (y > x) : (y < x)) rank++;
            for (auto y : v) if (y == x) break;
            bool found = false;
            for (auto y : v) if (y == x) { found = true; break; }
            if (!found) return Value::error(Err::NA);
            return Value::num((double)(rank + 1));
        });
        registerFunction("RANK.AVG", 2, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2({a[1]}, v, e, &ctx); if (e.isError()) return e;
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            int order = 0;
            if (a.size() >= 3 && !i1(a[2], order, e)) return e;
            int below = 0, eq = 0;
            for (auto y : v) { if (order == 0 ? (y > x) : (y < x)) below++; else if (y == x) eq++; }
            if (eq == 0) return Value::error(Err::NA);
            return Value::num(below + (eq + 1) / 2.0);
        });
        registerFunction("RANK", 2, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("RANK.EQ")->impl(a, c); });

        // --- 相关与回归 ---
        registerFunction("CORREL", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> x, y; Value e;
            col2({a[0]}, x, e, &ctx); if (e.isError()) return e;
            col2({a[1]}, y, e, &ctx); if (e.isError()) return e;
            if (x.size() != y.size() || x.empty()) return Value::error(Err::NA);
            double mx = 0, my = 0; for (auto v : x) mx += v; for (auto v : y) my += v;
            mx /= x.size(); my /= y.size();
            double sxy = 0, sxx = 0, syy = 0;
            for (size_t i = 0; i < x.size(); i++) {
                sxy += (x[i]-mx)*(y[i]-my); sxx += (x[i]-mx)*(x[i]-mx); syy += (y[i]-my)*(y[i]-my);
            }
            if (sxx == 0 || syy == 0) return Value::error(Err::Div0);
            return Value::num(sxy / std::sqrt(sxx * syy));
        });
        registerFunction("PEARSON", 2, 2, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("CORREL")->impl(a, c); });
        registerFunction("RSQ", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value r = findFunction("CORREL")->impl(a, *(EvalCtx*)nullptr);
            if (r.isError()) return r;
            return Value::num(r.n * r.n);
        });
        registerFunction("COVARIANCE.P", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> x, y; Value e;
            col2({a[0]}, x, e, &ctx); if (e.isError()) return e;
            col2({a[1]}, y, e, &ctx); if (e.isError()) return e;
            if (x.size() != y.size() || x.empty()) return Value::error(Err::NA);
            double mx = 0, my = 0; for (auto v : x) mx += v; for (auto v : y) my += v;
            mx /= x.size(); my /= y.size();
            double s = 0; for (size_t i = 0; i < x.size(); i++) s += (x[i]-mx)*(y[i]-my);
            return Value::num(s / x.size());
        });
        registerFunction("COVARIANCE.S", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> x, y; Value e;
            col2({a[0]}, x, e, &ctx); if (e.isError()) return e;
            col2({a[1]}, y, e, &ctx); if (e.isError()) return e;
            if (x.size() != y.size() || x.size() < 2) return Value::error(Err::NA);
            double mx = 0, my = 0; for (auto v : x) mx += v; for (auto v : y) my += v;
            mx /= x.size(); my /= y.size();
            double s = 0; for (size_t i = 0; i < x.size(); i++) s += (x[i]-mx)*(y[i]-my);
            return Value::num(s / (x.size() - 1));
        });
        registerFunction("COVAR", 2, 2, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("COVARIANCE.P")->impl(a, c); });
        registerFunction("SLOPE", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> y, x; Value e;
            col2({a[0]}, y, e, &ctx); if (e.isError()) return e;
            col2({a[1]}, x, e, &ctx); if (e.isError()) return e;
            if (x.size() != y.size() || x.empty()) return Value::error(Err::NA);
            double mx = 0, my = 0; for (auto v : x) mx += v; for (auto v : y) my += v;
            mx /= x.size(); my /= y.size();
            double sxy = 0, sxx = 0;
            for (size_t i = 0; i < x.size(); i++) { sxy += (x[i]-mx)*(y[i]-my); sxx += (x[i]-mx)*(x[i]-mx); }
            if (sxx == 0) return Value::error(Err::Div0);
            return Value::num(sxy / sxx);
        });
        registerFunction("INTERCEPT", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> y, x; Value e;
            col2({a[0]}, y, e, &ctx); if (e.isError()) return e;
            col2({a[1]}, x, e, &ctx); if (e.isError()) return e;
            double mx = 0, my = 0; for (auto v : x) mx += v; for (auto v : y) my += v;
            mx /= x.size(); my /= y.size();
            Value sl = findFunction("SLOPE")->impl(a, *(EvalCtx*)nullptr);
            if (sl.isError()) return sl;
            return Value::num(my - sl.n * mx);
        });
        registerFunction("FORECAST", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value sl = findFunction("SLOPE")->impl({a[1], a[2]}, *(EvalCtx*)nullptr);
            if (sl.isError()) return sl;
            Value ic = findFunction("INTERCEPT")->impl({a[1], a[2]}, *(EvalCtx*)nullptr);
            if (ic.isError()) return ic;
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            return Value::num(ic.n + sl.n * x);
        });
        registerFunction("STEYX", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> y, x; Value e;
            col2({a[0]}, y, e, &ctx); if (e.isError()) return e;
            col2({a[1]}, x, e, &ctx); if (e.isError()) return e;
            if (x.size() != y.size() || x.size() < 3) return Value::error(Err::NA);
            double mx = 0, my = 0; for (auto v : x) mx += v; for (auto v : y) my += v;
            mx /= x.size(); my /= y.size();
            Value sl = findFunction("SLOPE")->impl(a, *(EvalCtx*)nullptr);
            if (sl.isError()) return sl;
            double ss = 0;
            for (size_t i = 0; i < x.size(); i++) {
                double p = my + sl.n * (x[i] - mx);
                ss += (y[i] - p) * (y[i] - p);
            }
            return Value::num(std::sqrt(ss / (x.size() - 2)));
        });
        registerFunction("STANDARDIZE", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, m, sd;
            if (!n1(a[0], x) || !n1(a[1], m) || !n1(a[2], sd)) return Value::error(Err::Value);
            if (sd <= 0) return Value::error(Err::Num);
            return Value::num((x - m) / sd);
        });

        // --- 条件聚合 ---
        registerFunction("COUNTIF", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string crit; Value e;
            if (!toText(a[1], crit, e)) return e;
            std::vector<Value> cells; std::vector<size_t> idx;
            if (!critIndices(a[0], crit, cells, idx, e)) return e;
            return Value::num((double)idx.size());
        });
        registerFunction("SUMIF", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string crit; Value e;
            if (!toText(a[1], crit, e)) return e;
            std::vector<Value> cells; std::vector<size_t> idx;
            if (!critIndices(a[0], crit, cells, idx, e)) return e;
            std::vector<Value> sums;
            if (a.size() >= 3) { flatKeep(a[2], sums, e); if (e.isError()) return e; }
            double s = 0;
            for (size_t i : idx) {
                const Value& src = sums.empty() ? cells[i] : (i < sums.size() ? sums[i] : Value::empty());
                if (src.isError()) return src;
                if (src.isEmpty() || src.isStr() || src.t == Value::T::Bool) continue;
                s += src.n;
            }
            return Value::num(s);
        });
        registerFunction("AVERAGEIF", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string crit; Value e;
            if (!toText(a[1], crit, e)) return e;
            std::vector<Value> cells; std::vector<size_t> idx;
            if (!critIndices(a[0], crit, cells, idx, e)) return e;
            std::vector<Value> sums;
            if (a.size() >= 3) { flatKeep(a[2], sums, e); if (e.isError()) return e; }
            double s = 0; int cnt = 0;
            for (size_t i : idx) {
                const Value& src = sums.empty() ? cells[i] : (i < sums.size() ? sums[i] : Value::empty());
                if (src.isError()) return src;
                if (src.isEmpty() || src.isStr() || src.t == Value::T::Bool) continue;
                s += src.n; cnt++;
            }
            if (cnt == 0) return Value::error(Err::Div0);
            return Value::num(s / cnt);
        });
        registerFunction("COUNTIFS", 2, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (a.size() % 2 != 0) return Value::error(Err::Value);
            size_t nCrit = a.size() / 2;
            std::vector<std::vector<Value>> cells(nCrit);
            std::vector<std::vector<size_t>> idx(nCrit);
            for (size_t k = 0; k < nCrit; k++) {
                std::string crit; Value e;
                if (!toText(a[2*k+1], crit, e)) return e;
                if (!critIndices(a[2*k], crit, cells[k], idx[k], e)) return e;
                if (k > 0 && cells[k].size() != cells[0].size()) return Value::error(Err::Value);
            }
            size_t n = cells[0].size();
            size_t cnt = 0;
            for (size_t i = 0; i < n; i++) {
                bool all = true;
                for (size_t k = 0; k < nCrit; k++) {
                    if (std::find(idx[k].begin(), idx[k].end(), i) == idx[k].end()) { all = false; break; }
                }
                if (all) cnt++;
            }
            return Value::num((double)cnt);
        });
        registerFunction("SUMIFS", 3, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (a.size() < 3 || (a.size() - 1) % 2 != 0) return Value::error(Err::Value);
            std::vector<Value> sums; Value e;
            flatKeep(a[0], sums, e); if (e.isError()) return e;
            size_t nCrit = (a.size() - 1) / 2;
            std::vector<std::vector<Value>> cells(nCrit);
            std::vector<std::vector<size_t>> idx(nCrit);
            // SUMIFS(sum_range, crit_range1, crit1, [crit_range2, crit2], ...)
            for (size_t k = 0; k < nCrit; k++) {
                std::string crit;
                if (!toText(a[2 + 2*k], crit, e)) return e;          // 条件
                if (!critIndices(a[1 + 2*k], crit, cells[k], idx[k], e)) return e;  // 条件区域
                if (cells[k].size() != sums.size()) return Value::error(Err::Value);
            }
            double s = 0;
            for (size_t i = 0; i < sums.size(); i++) {
                bool all = true;
                for (size_t k = 0; k < nCrit; k++)
                    if (std::find(idx[k].begin(), idx[k].end(), i) == idx[k].end()) { all = false; break; }
                if (!all) continue;
                const Value& src = sums[i];
                if (src.isError()) return src;
                if (src.isEmpty() || src.isStr() || src.t == Value::T::Bool) continue;
                s += src.n;
            }
            return Value::num(s);
        });
        registerFunction("AVERAGEIFS", 3, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (a.size() < 3 || (a.size() - 1) % 2 != 0) return Value::error(Err::Value);
            std::vector<Value> sums; Value e;
            flatKeep(a[0], sums, e); if (e.isError()) return e;
            size_t nCrit = (a.size() - 1) / 2;
            std::vector<std::vector<Value>> cells(nCrit);
            std::vector<std::vector<size_t>> idx(nCrit);
            // SUMIFS(sum_range, crit_range1, crit1, [crit_range2, crit2], ...)
            for (size_t k = 0; k < nCrit; k++) {
                std::string crit;
                if (!toText(a[2 + 2*k], crit, e)) return e;          // 条件
                if (!critIndices(a[1 + 2*k], crit, cells[k], idx[k], e)) return e;  // 条件区域
                if (cells[k].size() != sums.size()) return Value::error(Err::Value);
            }
            double s = 0; int cnt = 0;
            for (size_t i = 0; i < sums.size(); i++) {
                bool all = true;
                for (size_t k = 0; k < nCrit; k++)
                    if (std::find(idx[k].begin(), idx[k].end(), i) == idx[k].end()) { all = false; break; }
                if (!all) continue;
                const Value& src = sums[i];
                if (src.isError()) return src;
                if (src.isEmpty() || src.isStr() || src.t == Value::T::Bool) continue;
                s += src.n; cnt++;
            }
            if (cnt == 0) return Value::error(Err::Div0);
            return Value::num(s / cnt);
        });
        registerFunction("FREQUENCY", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> data; Value e; col2({a[0]}, data, e, &ctx); if (e.isError()) return e;
            std::vector<double> bins; col2({a[1]}, bins, e, &ctx); if (e.isError()) return e;
            std::sort(bins.begin(), bins.end());
            auto arr = std::make_shared<Array2D>();
            // FREQUENCY 返回**列向量**（n 个分箱 + 1 个"超出"档，共 n+1 行）。
            // 原先堆成一行：1x3 {2,2,1}。Excel 里它是竖向溢出的 —— 写成行向量
            // 时横向溢出结果看起来"数值都对"，只有比对形状才暴露方向反了。
            std::vector<Value> row;
            for (size_t b = 0; b < bins.size(); b++) {
                double lo = (b == 0) ? -1e300 : bins[b-1];
                double hi = bins[b];
                size_t c = 0;
                for (auto d : data) if (d > lo && d <= hi) c++;
                row.push_back(Value::num((double)c));
            }
            size_t c = 0;
            for (auto d : data) if (d > bins.back()) c++;
            row.push_back(Value::num((double)c));
            for (auto& v : row) arr->push_back(std::vector<Value>{v});
            return Value::array(arr);
        });

        // --- 分布 ---
        registerFunction("NORM.S.DIST", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double z; if (!n1(a[0], z)) return Value::error(Err::Value);
            int cum; Value e; if (!i1(a[1], cum, e)) return e;
            return Value::num(cum ? dist::normCdf(z) : dist::normPdf(z));
        });
        registerFunction("NORMSDIST", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double z; if (!n1(a[0], z)) return Value::error(Err::Value);
            return Value::num(dist::normCdf(z));
        });
        registerFunction("NORM.DIST", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, m, sd; int cum;
            if (!n1(a[0], x) || !n1(a[1], m) || !n1(a[2], sd)) return Value::error(Err::Value);
            Value e; if (!i1(a[3], cum, e)) return e;
            if (sd <= 0) return Value::error(Err::Num);
            if (cum) return Value::num(dist::normCdf((x - m) / sd));
            return Value::num(dist::normPdf((x - m) / sd) / sd);
        });
        registerFunction("NORMDIST", 4, 4, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("NORM.DIST")->impl(a, c); });
        registerFunction("NORM.S.INV", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double p; if (!n1(a[0], p)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1) return Value::error(Err::Num);
            return Value::num(dist::normInv(p));
        });
        registerFunction("NORMSINV", 1, 1, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("NORM.S.INV")->impl(a, c); });
        registerFunction("NORM.INV", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double p, m, sd;
            if (!n1(a[0], p) || !n1(a[1], m) || !n1(a[2], sd)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1 || sd <= 0) return Value::error(Err::Num);
            return Value::num(m + sd * dist::normInv(p));
        });
        registerFunction("NORMINV", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("NORM.INV")->impl(a, c); });

        registerFunction("T.DIST", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, df; int cum;
            if (!n1(a[0], x) || !n1(a[1], df)) return Value::error(Err::Value);
            Value e; if (!i1(a[2], cum, e)) return e;
            if (df < 1) return Value::error(Err::Num);
            if (cum) return Value::num(dist::tCdf(x, df));
            double lbeta = std::lgamma((df+1)/2.0) - std::lgamma(df/2.0) - 0.5*std::log(df*3.14159265358979323846);
            return Value::num(std::exp(lbeta) * std::pow(1.0 + x*x/df, -(df+1)/2.0));
        });
        registerFunction("T.DIST.RT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, df;
            if (!n1(a[0], x) || !n1(a[1], df)) return Value::error(Err::Value);
            if (df < 1) return Value::error(Err::Num);
            return Value::num(1.0 - dist::tCdf(x, df));
        });
        registerFunction("T.DIST.2T", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, df;
            if (!n1(a[0], x) || !n1(a[1], df)) return Value::error(Err::Value);
            if (df < 1 || x < 0) return Value::error(Err::Num);
            return Value::num(2.0 * (1.0 - dist::tCdf(std::fabs(x), df)));
        });
        registerFunction("TDIST", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, df; int tails;
            if (!n1(a[0], x) || !n1(a[1], df)) return Value::error(Err::Value);
            Value e; if (!i1(a[2], tails, e)) return e;
            if (x < 0 || df < 1 || (tails != 1 && tails != 2)) return Value::error(Err::Num);
            return Value::num(tails == 1 ? (1.0 - dist::tCdf(x, df))
                                         : 2.0 * (1.0 - dist::tCdf(x, df)));
        });
        registerFunction("T.INV", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double p, df;
            if (!n1(a[0], p) || !n1(a[1], df)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1 || df < 1) return Value::error(Err::Num);
            return Value::num(dist::tInv(p, df));
        });
        registerFunction("T.INV.2T", 2, 2, [](const std::vector<Value>& a, EvalCtx& c) -> Value {
            double p, df;
            if (!n1(a[0], p) || !n1(a[1], df)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1 || df < 1) return Value::error(Err::Num);
            // 双尾反函数返回**正**的临界值：P(|T| > t) = p。
            // 用 p/2 直接喂给 T.INV 会得到左尾的负值 —— 实测
            // TINV(0.5,10) 因此返回 -0.6998，而 Excel 是 +0.6998。
            // 右尾对应的累积概率是 1 - p/2。
            return findFunction("T.INV")->impl({Value::num(1.0 - p/2.0), Value::num(df)}, c);
        });
        registerFunction("TINV", 2, 2, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("T.INV.2T")->impl(a, c); });

        registerFunction("F.DIST.RT", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, d1, d2;
            if (!n1(a[0], x) || !n1(a[1], d1) || !n1(a[2], d2)) return Value::error(Err::Value);
            if (x < 0 || d1 < 1 || d2 < 1) return Value::error(Err::Num);
            return Value::num(dist::fCdfR(x, d1, d2));
        });
        registerFunction("FDIST", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("F.DIST.RT")->impl(a, c); });
        registerFunction("F.INV.RT", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double p, d1, d2;
            if (!n1(a[0], p) || !n1(a[1], d1) || !n1(a[2], d2)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1 || d1 < 1 || d2 < 1) return Value::error(Err::Num);
            return Value::num(dist::fInv(p, d1, d2));
        });
        registerFunction("FINV", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("F.INV.RT")->impl(a, c); });

        registerFunction("CHISQ.DIST.RT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, df;
            if (!n1(a[0], x) || !n1(a[1], df)) return Value::error(Err::Value);
            if (x < 0 || df < 1) return Value::error(Err::Num);
            return Value::num(dist::chiCdfR(x, df));
        });
        registerFunction("CHISQ.INV.RT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double p, df;
            if (!n1(a[0], p) || !n1(a[1], df)) return Value::error(Err::Value);
            if (p <= 0 || p >= 1 || df < 1) return Value::error(Err::Num);
            return Value::num(dist::chiInv(p, df));
        });

        registerFunction("EXPON.DIST", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, lam; int cum;
            if (!n1(a[0], x) || !n1(a[1], lam)) return Value::error(Err::Value);
            Value e; if (!i1(a[2], cum, e)) return e;
            if (x < 0 || lam <= 0) return Value::error(Err::Num);
            if (x == 0) return Value::num(cum ? 0.0 : lam);
            return Value::num(cum ? 1.0 - std::exp(-lam * x) : lam * std::exp(-lam * x));
        });
        registerFunction("EXPONDIST", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("EXPON.DIST")->impl(a, c); });
        registerFunction("POISSON.DIST", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x, mean; int cum;
            if (!n1(a[0], x) || !n1(a[1], mean)) return Value::error(Err::Value);
            Value e; if (!i1(a[2], cum, e)) return e;
            if (x < 0 || mean < 0) return Value::error(Err::Num);
            int k = (int)std::floor(x);
            if (!cum) return Value::num(std::exp(-mean + k * std::log(mean) - dist::factLn(k)));
            double s = 0;
            for (int i = 0; i <= k; i++) s += std::exp(-mean + i * std::log(mean) - dist::factLn(i));
            return Value::num(s);
        });
        registerFunction("POISSON", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("POISSON.DIST")->impl(a, c); });
        registerFunction("BINOM.DIST", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double k, n, p; int cum;
            if (!n1(a[0], k) || !n1(a[1], n) || !n1(a[2], p)) return Value::error(Err::Value);
            Value e; if (!i1(a[3], cum, e)) return e;
            if (k < 0 || k > n || p < 0 || p > 1) return Value::error(Err::Num);
            if (!cum) return Value::num(dist::binomPdf(std::floor(k), std::floor(n), p));
            double s = 0;
            for (int i = 0; i <= (int)std::floor(k); i++) s += dist::binomPdf(i, std::floor(n), p);
            return Value::num(s);
        });
        registerFunction("BINOMDIST", 4, 4, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("BINOM.DIST")->impl(a, c); });

        registerFunction("CONFIDENCE.NORM", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double alpha, sd, n;
            if (!n1(a[0], alpha) || !n1(a[1], sd) || !n1(a[2], n)) return Value::error(Err::Value);
            if (alpha <= 0 || alpha >= 1 || sd <= 0 || n < 1) return Value::error(Err::Num);
            return Value::num(dist::normInv(1.0 - alpha/2.0) * sd / std::sqrt(n));
        });
        registerFunction("CONFIDENCE", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("CONFIDENCE.NORM")->impl(a, c); });
        registerFunction("CONFIDENCE.T", 3, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            double alpha, sd, n;
            if (!n1(a[0], alpha) || !n1(a[1], sd) || !n1(a[2], n)) return Value::error(Err::Value);
            if (alpha <= 0 || alpha >= 1 || sd <= 0 || n < 1) return Value::error(Err::Num);
            std::vector<double> v; Value e; col2({a[1]}, v, e, &ctx);
            (void)v;
            double t = std::fabs(dist::tInv(alpha/2.0, n - 1));
            return Value::num(t * sd / std::sqrt(n));
        });
        registerFunction("Z.TEST", 2, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::vector<double> v; Value e; col2({a[0]}, v, e, &ctx); if (e.isError()) return e;
            double x; if (!n1(a[1], x)) return Value::error(Err::Value);
            double sigma = 0;
            if (a.size() >= 3) { if (!n1(a[2], sigma)) return Value::error(Err::Value); }
            if (v.empty()) return Value::error(Err::NA);
            double m = 0; for (auto y : v) m += y; m /= v.size();
            if (sigma == 0) {
                double s2 = 0; for (auto y : v) s2 += (y-m)*(y-m);
                sigma = std::sqrt(s2 / (v.size()-1));
            }
            if (sigma == 0) return Value::error(Err::NA);
            double z = (m - x) / (sigma / std::sqrt(v.size()));
            return Value::num(1.0 - dist::normCdf(z));
        });
        registerFunction("ZTEST", 2, 3, [](const std::vector<Value>& a, EvalCtx& c) { return findFunction("Z.TEST")->impl(a, c); });
    }
};
static StatRegistrar g_statReg;

} // namespace xl
