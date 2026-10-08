#include "functions.hpp"
#include "ast.hpp"
#include "sheet.hpp"
#include <algorithm>
#include <random>
#include <sstream>

namespace xl {

static bool n1(const Value& v, double& out) {
    Value e; if (!toNumber(v, out, e)) return false; return true;
}
static bool i1(const Value& v, int& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    double d; Value e; if (!toNumber(v, d, e)) { err = e; return false; }
    out = (int)std::floor(d); return true;
}
static void flatKeep(const Value& v, std::vector<Value>& out, Value& err) {
    if (v.isError()) { err = v; return; }
    if (v.isArray()) { if (v.arr) for (auto& r : *v.arr) for (auto& x : r) {
        if (x.isError()) { err = x; return; } out.push_back(x); } }
    else out.push_back(v);
}
static void toMatrix(const Value& v, std::vector<std::vector<Value>>& m, Value& err) {
    if (v.isError()) { err = v; return; }
    if (v.isArray()) { if (v.arr) m = *v.arr; return; }
    m.push_back({v});
}
static Value oneCol(const std::vector<Value>& v) {
    auto a = std::make_shared<Array2D>();
    for (auto& x : v) {
        if (x.isArray() && x.arr) a->insert(a->end(), x.arr->begin(), x.arr->end());
        else a->push_back({x});
    }
    return Value::array(a);
}

// ===========================================================================
// 动态数组（Excel 365 引入，是"现代 Excel"与旧版最大的能力断层）
// ===========================================================================
struct ArrayRegistrar {
    ArrayRegistrar() {
        // 占位注册：真正的语义在 Node::eval 中特判（需要访问 AST 与作用域）
        registerFunction("LET", 3, 254, [](const std::vector<Value>&, EvalCtx&) {
            return Value::error(Err::Name);
        });
        registerFunction("LAMBDA", 2, 254, [](const std::vector<Value>&, EvalCtx&) {
            return Value::error(Err::Name);
        });

        registerFunction("SEQUENCE", 1, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int rows = 1, cols = 1; double start = 1, step = 1; Value e;
            if (!i1(a[0], rows, e)) return e;
            if (a.size() >= 2 && !i1(a[1], cols, e)) return e;
            if (a.size() >= 3 && !n1(a[2], start)) return Value::error(Err::Value);
            if (a.size() >= 4 && !n1(a[3], step)) return Value::error(Err::Value);
            if (rows < 1 || cols < 1) return Value::error(Err::Value);
            if (rows > 100000 || cols > 10000) return Value::error(Err::Num);
            auto m = std::make_shared<Array2D>(rows);
            double v = start;
            for (int r = 0; r < rows; r++) {
                (*m)[r].resize(cols);
                for (int c = 0; c < cols; c++) { (*m)[r][c] = Value::num(v); v += step; }
            }
            return Value::array(m);
        });

        registerFunction("RANDARRAY", 0, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int rows = 1, cols = 1; Value e;
            double lo = 0, hi = 1; bool isInt = false;
            if (a.size() >= 1 && !i1(a[0], rows, e)) return e;
            if (a.size() >= 2 && !i1(a[1], cols, e)) return e;
            if (a.size() >= 3 && !n1(a[2], lo)) return Value::error(Err::Value);
            if (a.size() >= 4 && !n1(a[3], hi)) return Value::error(Err::Value);
            if (a.size() >= 5) { int f; if (!i1(a[4], f, e)) return e; isInt = (f != 0); }
            if (rows < 1 || cols < 1) return Value::error(Err::Value);
            static std::mt19937_64 gen(20240923);
            auto m = std::make_shared<Array2D>(rows);
            for (int r = 0; r < rows; r++) {
                (*m)[r].resize(cols);
                for (int c = 0; c < cols; c++) {
                    if (isInt) {
                        long long l = (long long)std::ceil(lo), h = (long long)std::floor(hi);
                        if (h < l) { (*m)[r][c] = Value::error(Err::Num); continue; }
                        std::uniform_int_distribution<long long> d(l, h);
                        (*m)[r][c] = Value::num((double)d(gen));
                    } else {
                        std::uniform_real_distribution<double> d(lo, hi);
                        (*m)[r][c] = Value::num(d(gen));
                    }
                }
            }
            return Value::array(m);
        }, true);

        // MAKEARRAY(rows, cols, LAMBDA(r,c, expr))
        registerFunction("MAKEARRAY", 3, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            int rows, cols; Value e;
            if (!i1(a[0], rows, e)) return e;
            if (!i1(a[1], cols, e)) return e;
            if (!a[2].isLambda()) return Value::error(Err::Value);
            if (rows < 1 || cols < 1) return Value::error(Err::Value);
            auto& fn = *a[2].lambda;
            auto m = std::make_shared<Array2D>(rows);
            for (int r = 0; r < rows; r++) {
                (*m)[r].resize(cols);
                for (int c = 0; c < cols; c++) {
                    Value v = ctx.applyLambda(fn, {Value::num(r + 1), Value::num(c + 1)});
                    if (v.isError()) return v;
                    (*m)[r][c] = v;
                }
            }
            return Value::array(m);
        });

        // MAP(array, LAMBDA(x, expr))
        registerFunction("MAP", 2, 255, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            auto& fnv = a.back();
            if (!fnv.isLambda()) return Value::error(Err::Value);
            auto& fn = *fnv.lambda;
            size_t nArg = a.size() - 1;
            std::vector<std::vector<std::vector<Value>>> mats(nArg);
            Value e;
            for (size_t k = 0; k < nArg; k++) { toMatrix(a[k], mats[k], e); if (e.isError()) return e; }
            size_t rows = mats[0].size();
            size_t cols = rows ? mats[0][0].size() : 0;
            for (auto& m : mats) if (m.size() != rows || (rows && m[0].size() != cols)) return Value::error(Err::Value);
            auto out = std::make_shared<Array2D>(rows);
            for (size_t r = 0; r < rows; r++) {
                (*out)[r].resize(cols);
                for (size_t c = 0; c < cols; c++) {
                    std::vector<Value> args;
                    for (auto& m : mats) args.push_back(m[r][c]);
                    if (args.size() != fn.params.size()) return Value::error(Err::Value);
                    Value v = ctx.applyLambda(fn, args);
                    if (v.isError()) return v;
                    (*out)[r][c] = v;
                }
            }
            return Value::array(out);
        });

        // REDUCE(initial, array, LAMBDA(acc, x, expr))
        registerFunction("REDUCE", 3, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            if (!a[2].isLambda()) return Value::error(Err::Value);
            auto& fn = *a[2].lambda;
            if (fn.params.size() != 2) return Value::error(Err::Value);
            std::vector<Value> items; Value e;
            flatKeep(a[1], items, e); if (e.isError()) return e;
            Value acc = a[0];
            for (auto& it : items) {
                if (it.isEmpty()) continue;
                acc = ctx.applyLambda(fn, {acc, it});
                if (acc.isError()) return acc;
            }
            return acc;
        });

        // SCAN(initial, array, LAMBDA(acc,x,expr)) —— 返回每一步的中间结果
        registerFunction("SCAN", 3, 3, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            if (!a[2].isLambda()) return Value::error(Err::Value);
            auto& fn = *a[2].lambda;
            std::vector<Value> items; Value e;
            flatKeep(a[1], items, e); if (e.isError()) return e;
            Value acc = a[0];
            std::vector<Value> out;
            for (auto& it : items) {
                if (it.isEmpty()) continue;
                acc = ctx.applyLambda(fn, {acc, it});
                if (acc.isError()) return acc;
                out.push_back(acc);
            }
            return oneCol(out);
        });

        // BYROW(array, LAMBDA(row, expr))
        registerFunction("BYROW", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            if (!a[1].isLambda()) return Value::error(Err::Value);
            auto& fn = *a[1].lambda;
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            std::vector<Value> out;
            for (auto& row : m) {
                auto ra = std::make_shared<Array2D>();
                ra->push_back(row);
                Value v = ctx.applyLambda(fn, {Value::array(ra)});
                if (v.isError()) return v;
                out.push_back(v);
            }
            return oneCol(out);
        });

        // BYCOL(array, LAMBDA(col, expr))
        registerFunction("BYCOL", 2, 2, [](const std::vector<Value>& a, EvalCtx& ctx) -> Value {
            if (!a[1].isLambda()) return Value::error(Err::Value);
            auto& fn = *a[1].lambda;
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            size_t cols = 0; for (auto& r : m) cols = std::max(cols, r.size());
            std::vector<Value> out;
            for (size_t c = 0; c < cols; c++) {
                auto ca = std::make_shared<Array2D>();
                std::vector<Value> col;
                for (auto& r : m) col.push_back(c < r.size() ? r[c] : Value::empty());
                ca->push_back(col);
                Value v = ctx.applyLambda(fn, {Value::array(ca)});
                if (v.isError()) return v;
                out.push_back(v);
            }
            auto arr = std::make_shared<Array2D>();
            arr->push_back(out);
            return Value::array(arr);
        });

        // SORTBY(array, by1, [order1], ...)
        registerFunction("SORTBY", 2, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            if (m.size() != 1) return Value::error(Err::Value);    // 单行为主
            std::vector<Value> vals = m[0];
            std::vector<double> key;
            std::vector<Value> kv; flatKeep(a[1], kv, e); if (e.isError()) return e;
            for (auto& x : kv) { double d; if (!n1(x, d)) return Value::error(Err::Value); key.push_back(d); }
            int order = 1;
            if (a.size() >= 3 && !i1(a[2], order, e)) return e;
            if (key.size() != vals.size()) return Value::error(Err::Value);
            std::vector<size_t> idx(vals.size());
            for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
            std::stable_sort(idx.begin(), idx.end(), [&](size_t i, size_t j) {
                return order == 1 ? key[i] < key[j] : key[i] > key[j];
            });
            std::vector<Value> out;
            for (size_t i : idx) out.push_back(vals[i]);
            auto arr = std::make_shared<Array2D>();
            arr->push_back(out);
            return Value::array(arr);
        });

        registerFunction("TAKE", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            int k; if (!i1(a[1], k, e)) return e;
            if (m.size() != 1) return Value::error(Err::Value);
            auto& row = m[0];
            std::vector<Value> out;
            if (k >= 0) for (int i = 0; i < k && i < (int)row.size(); i++) out.push_back(row[i]);
            else for (int i = (int)row.size() + k; i < (int)row.size(); i++) if (i >= 0) out.push_back(row[i]);
            auto arr = std::make_shared<Array2D>();
            arr->push_back(out);
            return Value::array(arr);
        });
        registerFunction("DROP", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            int k; if (!i1(a[1], k, e)) return e;
            if (m.size() != 1) return Value::error(Err::Value);
            auto& row = m[0];
            std::vector<Value> out;
            if (k >= 0) for (size_t i = k; i < row.size(); i++) out.push_back(row[i]);
            else for (size_t i = 0; i + (size_t)(-k) < row.size(); i++) out.push_back(row[i]);
            auto arr = std::make_shared<Array2D>();
            arr->push_back(out);
            return Value::array(arr);
        });
        registerFunction("TOROW", 1, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flatKeep(a[0], v, e); if (e.isError()) return e;
            // TOROW 必须产出**一行**。原先直接复用 oneCol()，得到的是一列 ——
            // 于是 TOROW({1;2;3}) 返回 3行x1列，与 TOCOL 完全相同，
            // 转置方向整个反了。
            auto arr = std::make_shared<Array2D>();
            arr->push_back(v);
            return Value::array(arr);
        });
        registerFunction("TOCOL", 1, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flatKeep(a[0], v, e); if (e.isError()) return e;
            auto arr = std::make_shared<Array2D>(v.size());
            for (size_t i = 0; i < v.size(); i++) (*arr)[i].push_back(v[i]);
            return Value::array(arr);
        });
        registerFunction("WRAPROWS", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flatKeep(a[0], v, e); if (e.isError()) return e;
            int w; if (!i1(a[1], w, e)) return e;
            if (w < 1) return Value::error(Err::Value);
            auto arr = std::make_shared<Array2D>();
            for (size_t i = 0; i < v.size(); i += w) {
                std::vector<Value> row;
                for (size_t k = 0; k < (size_t)w; k++) row.push_back(i + k < v.size() ? v[i+k] : Value::empty());
                arr->push_back(row);
            }
            return Value::array(arr);
        });
        registerFunction("WRAPCOLS", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<Value> v; Value e;
            flatKeep(a[0], v, e); if (e.isError()) return e;
            int h; if (!i1(a[1], h, e)) return e;
            if (h < 1) return Value::error(Err::Value);
            size_t cols = (v.size() + h - 1) / h;
            auto arr = std::make_shared<Array2D>(h);
            for (int r = 0; r < h; r++)
                for (size_t c = 0; c < cols; c++)
                    (*arr)[r].push_back(c * h + r < v.size() ? v[c * h + r] : Value::empty());
            return Value::array(arr);
        });
        registerFunction("EXPAND", 2, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            int rows, cols;
            if (!i1(a[1], rows, e)) return e;
            if (!i1(a[2], cols, e)) return e;
            if (rows < 1 || cols < 1) return Value::error(Err::Value);
            auto arr = std::make_shared<Array2D>(rows);
            for (int r = 0; r < rows; r++) {
                (*arr)[r].resize(cols);
                for (int c = 0; c < cols; c++)
                    (*arr)[r][c] = (r < (int)m.size() && c < (int)m[r].size()) ? m[r][c] : Value::error(Err::NA);
            }
            return Value::array(arr);
        });
        registerFunction("CHOOSECOLS", 2, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            auto out = std::make_shared<Array2D>(m.size());
            for (size_t k = 1; k < a.size(); k++) {
                int idx; if (!i1(a[k], idx, e)) return e;
                size_t ci = (size_t)(idx > 0 ? idx - 1 : (int)m[0].size() + idx);
                for (size_t r = 0; r < m.size(); r++)
                    (*out)[r].push_back(ci < m[r].size() ? m[r][ci] : Value::error(Err::Ref));
            }
            return Value::array(out);
        });
        registerFunction("CHOOSEROWS", 2, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<Value>> m; Value e;
            toMatrix(a[0], m, e); if (e.isError()) return e;
            auto out = std::make_shared<Array2D>();
            for (size_t k = 1; k < a.size(); k++) {
                int idx; if (!i1(a[k], idx, e)) return e;
                size_t ri = (size_t)(idx > 0 ? idx - 1 : (int)m.size() + idx);
                if (ri < m.size()) out->push_back(m[ri]);
            }
            return Value::array(out);
        });
        registerFunction("VSTACK", 1, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            auto out = std::make_shared<Array2D>();
            Value e;
            for (auto& arg : a) {
                std::vector<std::vector<Value>> m;
                toMatrix(arg, m, e); if (e.isError()) return e;
                for (auto& r : m) out->push_back(r);
            }
            return Value::array(out);
        });
        registerFunction("HSTACK", 1, 254, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::vector<std::vector<std::vector<Value>>> mats;
            Value e;
            size_t rows = 0;
            for (auto& arg : a) {
                std::vector<std::vector<Value>> m;
                toMatrix(arg, m, e); if (e.isError()) return e;
                rows = std::max(rows, m.size());
                mats.push_back(m);
            }
            auto out = std::make_shared<Array2D>(rows);
            for (auto& m : mats)
                for (size_t r = 0; r < rows; r++) {
                    if (r >= m.size()) continue;
                    for (auto& v : m[r]) (*out)[r].push_back(v);
                }
            return Value::array(out);
        });
        registerFunction("ARRAYTOTEXT", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string s; Value e;
            if (!toText(a[0], s, e)) return e;
            return Value::str(s);
        });
        registerFunction("VALUETOTEXT", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            return Value::str(valueToText(a[0]));
        });
    }
};
static ArrayRegistrar g_arrayReg;

} // namespace xl
