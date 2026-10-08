#include "functions.hpp"
#include "utf8.hpp"
#include "date.hpp"
#include "lexer.hpp"
#include "ast.hpp"
#include "sheet.hpp"
#include <algorithm>
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
static Value txt(const std::vector<Value>& a, size_t idx) {
    std::string s; Value e;
    if (!toText(a[idx], s, e)) return e;
    return Value::str(s);
}
static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// 把任意参数摊平成二维数组（保留结构，用于 TRANSPOSE / 数据库函数）
static void toMatrix(const Value& v, std::vector<std::vector<Value>>& m, Value& err) {
    if (v.isError()) { err = v; return; }
    if (v.isArray()) { if (v.arr) m = *v.arr; return; }
    m.push_back({v});
}
static void flatKeep(const Value& v, std::vector<Value>& out, Value& err) {
    if (v.isError()) { err = v; return; }
    if (v.isArray()) {
        if (!v.arr) return;
        for (auto& r : *v.arr) for (auto& x : r) {
            if (x.isError()) { err = x; return; }
            out.push_back(x);
        }
    } else out.push_back(v);
}

// ===========================================================================
// 文本
// ===========================================================================
struct TextRegistrar {
    TextRegistrar() {
        registerFunction("EXACT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value x = txt(a, 0); if (x.isError()) return x;
            Value y = txt(a, 1); if (y.isError()) return y;
            return Value::boolean(x.s == y.s);        // 大小写敏感
        });
        registerFunction("PROPER", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = txt(a, 0); if (s.isError()) return s;
            std::string out; bool newWord = true;
            for (char c : s.s) {
                if (std::isalpha((unsigned char)c)) {
                    out.push_back(newWord ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c));
                    newWord = false;
                } else { out.push_back(c); newWord = !std::isalnum((unsigned char)c); }
            }
            return Value::str(out);
        });
        registerFunction("SEARCH", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value n = txt(a, 0); if (n.isError()) return n;
            Value h = txt(a, 1); if (h.isError()) return h;
            size_t from = 0;
            if (a.size() >= 3) {
                int f; Value e; if (!i1(a[2], f, e)) return e;
                if (f < 1) return Value::error(Err::Value);
                from = (size_t)(f - 1);
            }
            if (n.s.empty()) return Value::num((double)(from + 1));
            std::string nu = upper(n.s), hu = upper(h.s);
            size_t p = hu.find(nu, from);
            if (p == std::string::npos) return Value::error(Err::Value);
            return Value::num((double)(p + 1));
        });
        registerFunction("REPLACE", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = txt(a, 0); if (s.isError()) return s;
            int start, cnt; Value e;
            if (!i1(a[1], start, e)) return e;
            if (!i1(a[2], cnt, e)) return e;
            Value ns = txt(a, 3); if (ns.isError()) return ns;
            if (start < 1 || cnt < 0) return Value::error(Err::Value);
            // 按**字符**定位，不是字节。原先 out.replace(p, cnt, ...)
            // 在中文上会从汉字中间切开：
            //   REPLACE("中文abc",1,2,"X") = "X\xb8文abc"（乱码）
            //   正确 = "Xabc"
            size_t p = (size_t)(start - 1);
            if (p > utf::len(s.s)) return Value::error(Err::Value);
            return Value::str(utf::splice(s.s, p, (size_t)cnt, ns.s));
        });
        registerFunction("CODE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = txt(a, 0); if (s.isError()) return s;
            if (s.s.empty()) return Value::error(Err::Value);
            return Value::num((double)(unsigned char)s.s[0]);
        });
        registerFunction("CHAR", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n; Value e; if (!i1(a[0], n, e)) return e;
            if (n < 1 || n > 255) return Value::error(Err::Value);
            return Value::str(std::string(1, (char)n));
        });
        registerFunction("UNICHAR", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int n; Value e; if (!i1(a[0], n, e)) return e;
            if (n < 1 || n > 0x10FFFF) return Value::error(Err::Value);
            if (n < 0x80) return Value::str(std::string(1, (char)n));
            std::string out;
            if (n < 0x800) {
                out += (char)(0xC0 | (n >> 6));
                out += (char)(0x80 | (n & 0x3F));
            } else if (n < 0x10000) {
                out += (char)(0xE0 | (n >> 12));
                out += (char)(0x80 | ((n >> 6) & 0x3F));
                out += (char)(0x80 | (n & 0x3F));
            } else {
                out += (char)(0xF0 | (n >> 18));
                out += (char)(0x80 | ((n >> 12) & 0x3F));
                out += (char)(0x80 | ((n >> 6) & 0x3F));
                out += (char)(0x80 | (n & 0x3F));
            }
            return Value::str(out);
        });
        registerFunction("UNICODE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = txt(a, 0); if (s.isError()) return s;
            if (s.s.empty()) return Value::error(Err::Value);
            unsigned char c0 = (unsigned char)s.s[0];
            if (c0 < 0x80) return Value::num((double)c0);
            if ((c0 & 0xE0) == 0xC0 && s.s.size() >= 2)
                return Value::num((double)(((c0 & 0x1F) << 6) | ((unsigned char)s.s[1] & 0x3F)));
            if ((c0 & 0xF0) == 0xE0 && s.s.size() >= 3)
                return Value::num((double)(((c0 & 0x0F) << 12) | (((unsigned char)s.s[1] & 0x3F) << 6) | ((unsigned char)s.s[2] & 0x3F)));
            if ((c0 & 0xF8) == 0xF0 && s.s.size() >= 4)
                return Value::num((double)(((c0 & 0x07) << 18) | (((unsigned char)s.s[1] & 0x3F) << 12) | (((unsigned char)s.s[2] & 0x3F) << 6) | ((unsigned char)s.s[3] & 0x3F)));
            return Value::error(Err::Value);
        });
        registerFunction("CLEAN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = txt(a, 0); if (s.isError()) return s;
            std::string out;
            for (char c : s.s) if ((unsigned char)c >= 32) out.push_back(c);
            return Value::str(out);
        });
        registerFunction("FIXED", 1, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            int d = 2;
            if (a.size() >= 2) { Value e; if (!i1(a[1], d, e)) return e; }
            if (d > 127) return Value::error(Err::Value);
            std::ostringstream os;
            os << std::fixed << std::setprecision(std::max(0, d)) << x;
            return Value::str(os.str());
        });
        registerFunction("DOLLAR", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            int d = 2;
            if (a.size() >= 2) { Value e; if (!i1(a[1], d, e)) return e; }
            std::ostringstream os;
            os << "$" << std::fixed << std::setprecision(std::max(0, d)) << x;
            return Value::str(os.str());
        });
        registerFunction("NUMBERVALUE", 1, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value s = txt(a, 0); if (s.isError()) return s;
            char decSep = '.', grpSep = ',';
            if (a.size() >= 2) { Value d = txt(a, 1); if (d.isError()) return d; if (!d.s.empty()) decSep = d.s[0]; }
            if (a.size() >= 3) { Value g = txt(a, 2); if (g.isError()) return g; if (!g.s.empty()) grpSep = g.s[0]; }
            std::string clean;
            bool neg = false;
            for (char c : s.s) {
                if (c == grpSep) continue;
                if (c == decSep) { clean.push_back('.'); continue; }
                if (c == '-') { neg = !neg; continue; }
                if (c == '+' || std::isspace((unsigned char)c)) continue;
                if (c == '%') { /* 百分号：后续 /100 */ }
                clean.push_back(c);
            }
            double v;
            if (!textToNumber(clean, v)) return Value::error(Err::Value);
            return Value::num(neg ? -v : v);
        });
        registerFunction("T", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            return a[0].isStr() ? a[0] : Value::str("");
        });
        registerFunction("TEXTJOIN", 3, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string delim; Value e;
            if (!toText(a[0], delim, e)) return e;
            int skipEmpty; if (!i1(a[1], skipEmpty, e)) return e;
            std::string out; bool first = true;
            std::vector<Value> all;
            for (size_t k = 2; k < a.size(); k++) { flatKeep(a[k], all, e); if (e.isError()) return e; }
            for (auto& v : all) {
                if (v.isError()) return v;
                std::string s; Value ee;
                if (!toText(v, s, ee)) return ee;
                if (skipEmpty && s.empty()) continue;
                if (!first) out += delim;
                out += s; first = false;
            }
            return Value::str(out);
        });
        registerFunction("TEXT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            // 简化版：支持 "0.00"、"0"、"#,##0"、"0%"、"yyyy-mm-dd"
            std::string fmt; Value e;
            if (!toText(a[1], fmt, e)) return e;
            if (a[0].isError()) return a[0];
            if (fmt.find("yy") != std::string::npos || fmt.find("dd") != std::string::npos ||
                fmt.find("mmm") != std::string::npos) {
                double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
                YMD y = serialToYMD(std::floor(s));
                char buf[64];
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y.y, y.m, y.d);
                return Value::str(buf);
            }
            double x; if (!n1(a[0], x)) return Value::error(Err::Value);
            bool pct = fmt.find('%') != std::string::npos;
            bool comma = fmt.find(',') != std::string::npos;
            int dec = 0;
            size_t dot = fmt.find('.');
            if (dot != std::string::npos) {
                size_t k = dot + 1;
                while (k < fmt.size() && fmt[k] == '0') { dec++; k++; }
            }
            double v = pct ? x * 100.0 : x;
            std::ostringstream os;
            os << std::fixed << std::setprecision(dec) << v;
            std::string s = os.str();
            if (comma) {
                size_t dp = s.find('.');
                std::string ip = (dp == std::string::npos) ? s : s.substr(0, dp);
                std::string fp = (dp == std::string::npos) ? "" : s.substr(dp);
                std::string out; int cnt = 0;
                for (int i = (int)ip.size() - 1; i >= 0; i--) {
                    out.insert(out.begin(), ip[i]);
                    if (++cnt % 3 == 0 && i > 0) out.insert(out.begin(), ',');
                }
                s = out + fp;
            }
            return Value::str(s + (pct ? "%" : ""));
        });
    }
};

// ===========================================================================
// 查找与引用（需 EvalCtx）
// ===========================================================================
struct LookupRegistrar {
    LookupRegistrar() {
        registerFunction("ROW", 0, 1, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            int col = -1, row = -1;
            ctx.currentCell(col, row);
            if (a.empty()) {
                if (row < 0) return Value::error(Err::NA);
                return Value::num((double)(row + 1));
            }
            if (a[0].isArray()) {
                if (!a[0].arr || a[0].arr->empty()) return Value::error(Err::Ref);
                col = -1; row = -1;
                // 区域形式：返回区域首行；这里用当前单元格所在区域的第一行
                int cc, cr; ctx.currentCell(cc, cr);
                if (cr >= 0) return Value::num((double)(cr + 1));
                return Value::num(1);
            }
            if (row < 0) return Value::num(1);
            return Value::num((double)(row + 1));
        });
        registerFunction("COLUMN", 0, 1, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            int col = -1, row = -1;
            ctx.currentCell(col, row);
            (void)a;
            if (col < 0) return Value::error(Err::NA);
            return Value::num((double)(col + 1));
        });
        registerFunction("ROWS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            if (m.empty()) return Value::error(Err::Ref);
            return Value::num((double)std::max((size_t)1, m.size()));
        });
        registerFunction("COLUMNS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            if (m.empty()) return Value::error(Err::Ref);
            size_t w = 0; for (auto& r : m) w = std::max(w, r.size());
            return Value::num((double)std::max((size_t)1, w));
        });
        registerFunction("TRANSPOSE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            size_t w = 0; for (auto& r : m) w = std::max(w, r.size());
            auto out = std::make_shared<Array2D>(w);
            for (size_t i = 0; i < w; i++) {
                (*out)[i].resize(m.size());
                for (size_t j = 0; j < m.size(); j++)
                    (*out)[i][j] = (i < m[j].size()) ? m[j][i] : Value::empty();
            }
            return Value::array(out);
        });
        registerFunction("ADDRESS", 2, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int r, c, abs = 1; Value e;
            if (!i1(a[0], r, e)) return e;
            if (!i1(a[1], c, e)) return e;
            if (a.size() >= 3 && !i1(a[2], abs, e)) return e;
            bool a1 = true;
            if (a.size() >= 4) { int f; if (!i1(a[3], f, e)) return e; a1 = (f == 1); }
            if (!a1) return Value::error(Err::Value);      // R1C1 暂不支持
            if (r < 1 || c < 1) return Value::error(Err::Value);
            std::string cs = colToName(c - 1);
            std::string s;
            switch (abs) {
                case 1: s = "$" + cs + "$" + std::to_string(r); break;
                case 2: s = cs + "$" + std::to_string(r); break;
                case 3: s = "$" + cs + std::to_string(r); break;
                case 4: s = cs + std::to_string(r); break;
                default: return Value::error(Err::Value);
            }
            return Value::str(s);
        });
        registerFunction("INDIRECT", 1, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            std::string addr; Value e;
            if (!toText(a[0], addr, e)) return e;
            (void)ctx;
            Value out;
            // 通过 sheet 上下文解析；这里 ctx 若不支持则退化为区域解析
            if (!ctx.cellByAddr("", addr, out)) return Value::error(Err::Ref);
            return out;
        });
        registerFunction("OFFSET", 3, 5, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            int rOff, cOff; Value e;
            if (!i1(a[1], rOff, e)) return e;
            if (!i1(a[2], cOff, e)) return e;
            int h = -1, w = -1;
            if (a.size() >= 4 && !i1(a[3], h, e)) return e;
            if (a.size() >= 5 && !i1(a[4], w, e)) return e;

            // 基准必须是引用本身的坐标。
            //
            // 原实现用"当前单元格 - 区域尺寸/2"反推锚点，等于假定当前格正好是
            // 区域的中心 —— 这在任何情况下都不成立。实测 OFFSET(A1,1,0)
            // （公式在 F6）会去读 F7，返回空；正确应读 A2。
            //
            // 现在直接取引用的左上角：区域参数是 RangeRef（见 eval.cpp 的
            // supportsLazyRange 白名单），单格参数也已在 AST 层转成 1x1 的
            // RangeRef，两种情况都拿得到真实坐标。
            std::string sheet;
            int bc = -1, br = -1, rows = 1, cols = 1;
            if (a[0].isRangeRef()) {
                const RangeRefData& r = *a[0].rng;
                sheet = r.sheet; bc = r.c0; br = r.r0;
                rows = r.r1 - r.r0 + 1; cols = r.c1 - r.c0 + 1;
            } else {
                // 数组字面量没有位置信息，无从偏移
                return Value::error(Err::Ref);
            }
            if (h < 0) h = rows;
            if (w < 0) w = cols;
            if (h < 1 || w < 1) return Value::error(Err::Ref);
            int rr = br + rOff, cc = bc + cOff;
            if (rr < 0 || cc < 0) return Value::error(Err::Ref);
            auto out = std::make_shared<Array2D>();
            for (int i = 0; i < h; i++) {
                std::vector<Value> row;
                for (int j = 0; j < w; j++) row.push_back(ctx.cell(sheet, cc + j, rr + i));
                out->push_back(std::move(row));
            }
            return Value::array(out);
        });
        registerFunction("LOOKUP", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<double> keys, vals; Value e;
            std::vector<Value> kv; flatKeep(a[1], kv, e); if (e.isError()) return e;
            for (auto& v : kv) { double d; if (!n1(v, d)) continue; keys.push_back(d); }
            std::vector<Value> vv;
            if (a.size() >= 3) { flatKeep(a[2], vv, e); if (e.isError()) return e; }
            else vv = kv;
            if (keys.empty()) return Value::error(Err::NA);
            bool asc = true;
            for (size_t i = 1; i < keys.size(); i++) if (keys[i] < keys[i-1]) { asc = false; break; }
            if (!asc) return Value::error(Err::NA);
            double k; if (!n1(a[0], k)) return Value::error(Err::NA);
            if (k < keys[0]) return Value::error(Err::NA);
            size_t idx = 0;
            for (size_t i = 0; i < keys.size(); i++) { if (keys[i] <= k) idx = i; else break; }
            if (idx >= vv.size()) return Value::error(Err::Ref);
            return vv[idx];
        });
        registerFunction("XMATCH", 2, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flatKeep(a[1], v, e); if (e.isError()) return e;
            int mode = 0;
            if (a.size() >= 3 && !i1(a[2], mode, e)) return e;
            for (size_t i = 0; i < v.size(); i++) {
                int cmp; Value ee;
                if (!compareValues(a[0], v[i], cmp, ee)) continue;
                if (cmp == 0) return Value::num((double)(i + 1));
            }
            return Value::error(Err::NA);
        });
        registerFunction("XLOOKUP", 3, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            // XLOOKUP(lookup_value, lookup_array, return_array, [if_not_found])
            std::vector<Value> keys, vals; Value e;
            flatKeep(a[1], keys, e); if (e.isError()) return e;
            flatKeep(a[2], vals, e); if (e.isError()) return e;
            for (size_t i = 0; i < keys.size(); i++) {
                int cmp; Value ee;
                if (!compareValues(a[0], keys[i], cmp, ee)) continue;
                if (cmp == 0) {
                    if (i >= vals.size()) return Value::error(Err::Ref);
                    return vals[i];
                }
            }
            if (a.size() >= 4) return a[3];        // if_not_found
            return Value::error(Err::NA);
        });
        registerFunction("SORT", 1, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            int order = 1;
            if (a.size() >= 3 && !i1(a[2], order, e)) return e;
            if (m.empty()) return Value::error(Err::Value);
            if (m.size() == 1) {
                std::vector<Value> row = m[0];
                std::stable_sort(row.begin(), row.end(), [order](const Value& x, const Value& y) {
                    int c; Value ee;
                    if (!compareValues(x, y, c, ee)) return false;
                    return order == 1 ? (c < 0) : (c > 0);
                });
                auto out = std::make_shared<Array2D>(); out->push_back(row);
                return Value::array(out);
            }
            std::stable_sort(m.begin(), m.end(), [order](const std::vector<Value>& x, const std::vector<Value>& y) {
                int c; Value ee;
                if (!compareValues(x[0], y[0], c, ee)) return false;
                return order == 1 ? (c < 0) : (c > 0);
            });
            auto out = std::make_shared<Array2D>(m);
            return Value::array(out);
        });
        registerFunction("UNIQUE", 1, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flatKeep(a[0], v, e); if (e.isError()) return e;
            std::vector<Value> out;
            for (auto& x : v) {
                bool dup = false;
                for (auto& y : out) {
                    int c; Value ee;
                    if (compareValues(x, y, c, ee) && c == 0) { dup = true; break; }
                }
                if (!dup) out.push_back(x);
            }
            auto arr = std::make_shared<Array2D>();
            for (auto& x : out) arr->push_back({x});
            return Value::array(arr);
        });
        registerFunction("FILTER", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v, inc; Value e;
            flatKeep(a[0], v, e); if (e.isError()) return e;
            flatKeep(a[1], inc, e); if (e.isError()) return e;
            if (inc.size() != v.size()) return Value::error(Err::Value);
            std::vector<Value> out;
            for (size_t i = 0; i < v.size(); i++) {
                bool b; Value ee;
                if (!toBool(inc[i], b, ee)) return ee;
                if (b) out.push_back(v[i]);
            }
            if (out.empty() && a.size() >= 3) return a[2];
            auto arr = std::make_shared<Array2D>();
            for (auto& x : out) arr->push_back({x});
            return Value::array(arr);
        });
        registerFunction("IFS", 2, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (a.size() % 2 != 0) return Value::error(Err::NA);
            for (size_t i = 0; i < a.size(); i += 2) {
                if (a[i].isError()) return a[i];
                bool b; Value e;
                if (!toBool(a[i], b, e)) return e;
                if (b) return a[i+1];
            }
            return Value::error(Err::NA);
        });
        registerFunction("SWITCH", 3, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            for (size_t i = 1; i + 1 < a.size(); i += 2) {
                int cmp; Value e;
                if (!compareValues(a[0], a[i], cmp, e)) continue;
                if (cmp == 0) return a[i+1];
            }
            if (a.size() % 2 == 0) return a.back();     // 默认分支
            return Value::error(Err::NA);
        });
    }
};

// ===========================================================================
// 信息
// ===========================================================================
struct InfoRegistrar {
    InfoRegistrar() {
        registerFunction("ERROR.TYPE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            if (!a[0].isError()) return Value::error(Err::NA);
            switch (a[0].e) {
                case Err::Null:  return Value::num(1);
                case Err::Div0:  return Value::num(2);
                case Err::Value: return Value::num(3);
                case Err::Ref:   return Value::num(4);
                case Err::Name:  return Value::num(5);
                case Err::Num:   return Value::num(6);
                case Err::NA:    return Value::num(7);
                case Err::Spill: return Value::num(9);
                case Err::Calc:  return Value::num(14);
            }
            return Value::error(Err::NA);
        });
        registerFunction("TYPE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            const Value& v = a[0];
            if (v.isNum()) return Value::num(1);
            if (v.isStr()) return Value::num(2);
            if (v.t == Value::T::Bool) return Value::num(4);
            if (v.isError()) return Value::num(16);
            if (v.isArray()) return Value::num(64);
            return Value::num(1);
        });
        registerFunction("N", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            const Value& v = a[0];
            if (v.isNum()) return Value::num(v.n);
            if (v.t == Value::T::Bool) return Value::num(v.b ? 1.0 : 0.0);
            if (v.isError()) return v;
            if (v.t == Value::T::Empty) return Value::num(0);
            return Value::num(0);
        });
        registerFunction("ISNONTEXT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return Value::boolean(!a[0].isStr()); });
        registerFunction("ISREF", 1, 1, [](const std::vector<Value>& a, EvalCtx&) { return Value::boolean(a[0].isArray()); });
        registerFunction("ISERR", 1, 1, [](const std::vector<Value>& a, EvalCtx&) {
            return Value::boolean(a[0].isError() && a[0].e != Err::NA);
        });
        registerFunction("SHEET", 0, 1, [](const std::vector<Value>&, EvalCtx&) { return Value::num(1); });
        registerFunction("SHEETS", 0, 1, [](const std::vector<Value>&, EvalCtx&) { return Value::num(1); });
    }
};

// ===========================================================================
// 数据库（DSUM 家族）
// database: 首行为列标签；field: 列名或序号；criteria: 至少两行（标签 + 条件）
// ===========================================================================
struct DbRegistrar {
    static bool run(const std::vector<Value>& a,
                    std::function<Value(const std::vector<double>&)> agg,
                    std::function<double(const std::vector<double>&, size_t)> alt,
                    Value& out) {
        if (a.size() < 3) { out = Value::error(Err::Value); return false; }
        std::vector<std::vector<Value>> db, cr; Value e;
        toMatrix(a[0], db, e); if (e.isError()) { out = e; return false; }
        toMatrix(a[2], cr, e); if (e.isError()) { out = e; return false; }
        if (db.size() < 2) { out = Value::error(Err::Value); return false; }

        // field 定位
        size_t fi;
        if (a[1].isStr()) {
            std::string name = upper(a[1].s);
            fi = std::string::npos;
            for (size_t i = 0; i < db[0].size(); i++) {
                std::string h; Value ee;
                if (toText(db[0][i], h, ee) && upper(h) == name) { fi = i; break; }
            }
            if (fi == std::string::npos) { out = Value::error(Err::Value); return false; }
        } else {
            int k; Value ee;
            if (!i1(a[1], k, ee)) { out = ee; return false; }
            if (k < 1 || (size_t)k > db[0].size()) { out = Value::error(Err::Value); return false; }
            fi = (size_t)(k - 1);
        }

        // 条件列：标签 -> 索引
        std::vector<std::pair<size_t, std::string>> conds;
        if (cr.empty()) { out = Value::error(Err::Value); return false; }
        for (size_t c = 0; c < cr[0].size(); c++) {
            std::string h; Value ee;
            if (!toText(cr[0][c], h, ee)) continue;
            std::string hu = upper(h);
            if (hu.empty()) continue;
            for (size_t i = 0; i < db[0].size(); i++) {
                std::string dh; Value e2;
                if (toText(db[0][i], dh, e2) && upper(dh) == hu) {
                    if (cr.size() >= 2) {
                        std::string cv; Value e3;
                        if (toText(cr[1][c], cv, e3) && !cv.empty()) conds.push_back({i, cv});
                    }
                    break;
                }
            }
        }

        std::vector<double> picked;
        std::vector<double> all;
        for (size_t r = 1; r < db.size(); r++) {
            bool okRow = true;
            for (auto& cd : conds) {
                if (cd.first >= db[r].size()) { okRow = false; break; }
                Value ee;
                bool m = matchDb(cd, db[r], ee);
                if (ee.isError()) { out = ee; return false; }
                if (!m) { okRow = false; break; }
            }
            if (!okRow) continue;
            if (fi >= db[r].size()) continue;
            double d;
            if (db[r][fi].isError()) { out = db[r][fi]; return false; }
            if (db[r][fi].isEmpty() || db[r][fi].isStr()) continue;
            if (!n1(db[r][fi], d)) continue;
            picked.push_back(d);
            all.push_back(d);
        }
        (void)all;
        out = agg(picked);
        (void)alt;
        return true;
    }
    static bool matchDb(const std::pair<size_t, std::string>& cd,
                        const std::vector<Value>& row, Value& err) {
        std::string crit = cd.second;
        std::string op; std::string rest = crit;
        if (crit.size() >= 2 && (crit[0] == '>' || crit[0] == '<') && (crit[1] == '=' || crit[1] == '>')) {
            op = crit.substr(0, 2); rest = crit.substr(2);
        } else if (!crit.empty() && (crit[0] == '>' || crit[0] == '<' || crit[0] == '=')) {
            op = crit.substr(0, 1); rest = crit.substr(1);
        }
        double tn = 0; bool tnOk = textToNumber(rest, tn);
        const Value& cv = row[cd.first];
        if (op.empty()) {
            if (cv.isNum() && tnOk) return cv.n == tn;
            std::string s; if (!toText(cv, s, err)) return false;
            return upper(s) == upper(rest);
        }
        double v; bool vOk = n1(cv, v);
        if (tnOk && vOk) {
            if (op == ">")  return v >  tn;
            if (op == "<")  return v <  tn;
            if (op == ">=") return v >= tn;
            if (op == "<=") return v <= tn;
            if (op == "<>") return v != tn;
            if (op == "=")  return v == tn;
        }
        std::string s; if (!toText(cv, s, err)) return false;
        std::string su = upper(s), ru = upper(rest);
        if (op == ">")  return su >  ru;
        if (op == "<")  return su <  ru;
        if (op == ">=") return su >= ru;
        if (op == "<=") return su <= ru;
        if (op == "<>") return su != ru;
        if (op == "=")  return su == ru;
        return false;
    }

    DbRegistrar() {
        registerFunction("DSUM", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value out;
            run(a, [](const std::vector<double>& v) {
                double s = 0; for (auto x : v) s += x; return Value::num(s);
            }, nullptr, out);
            return out;
        });
        registerFunction("DAVERAGE", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value out;
            run(a, [](const std::vector<double>& v) {
                if (v.empty()) return Value::error(Err::Div0);
                double s = 0; for (auto x : v) s += x; return Value::num(s / v.size());
            }, nullptr, out);
            return out;
        });
        registerFunction("DCOUNT", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value out;
            run(a, [](const std::vector<double>& v) { return Value::num((double)v.size()); }, nullptr, out);
            return out;
        });
        registerFunction("DMAX", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value out;
            run(a, [](const std::vector<double>& v) {
                if (v.empty()) return Value::num(0);
                return Value::num(*std::max_element(v.begin(), v.end()));
            }, nullptr, out);
            return out;
        });
        registerFunction("DMIN", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value out;
            run(a, [](const std::vector<double>& v) {
                if (v.empty()) return Value::num(0);
                return Value::num(*std::min_element(v.begin(), v.end()));
            }, nullptr, out);
            return out;
        });
        registerFunction("DPRODUCT", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value out;
            run(a, [](const std::vector<double>& v) {
                double p = 1; for (auto x : v) p *= x; return Value::num(p);
            }, nullptr, out);
            return out;
        });
        registerFunction("DSTDEV", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value out;
            run(a, [](const std::vector<double>& v) {
                if (v.size() < 2) return Value::error(Err::Div0);
                double m = 0; for (auto x : v) m += x; m /= v.size();
                double s = 0; for (auto x : v) s += (x-m)*(x-m);
                return Value::num(std::sqrt(s / (v.size()-1)));
            }, nullptr, out);
            return out;
        });
        registerFunction("DVAR", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value out;
            run(a, [](const std::vector<double>& v) {
                if (v.size() < 2) return Value::error(Err::Div0);
                double m = 0; for (auto x : v) m += x; m /= v.size();
                double s = 0; for (auto x : v) s += (x-m)*(x-m);
                return Value::num(s / (v.size()-1));
            }, nullptr, out);
            return out;
        });
    }
};

static TextRegistrar   g_textReg;
static LookupRegistrar g_lookupReg;
static InfoRegistrar   g_infoReg;
static DbRegistrar     g_dbReg;

} // namespace xl
