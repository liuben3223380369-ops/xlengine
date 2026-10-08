// 补齐批次 A：三角补全 + 矩阵运算
//
// 选这批的原因：与 IronCalc 实测的 496 函数做精确 diff 后，
// 这 12 个是"真功能缺口"（其余多是命名/别名差异）。
// 矩阵函数尤其关键 —— MMULT/MINVERSE/MDETAIL 是少数无法用"逐元素"
// 实现、必须有真正矩阵算法的数组函数。
#include "functions.hpp"
#include "value.hpp"
#include "ast.hpp"
#include <cmath>
#include <vector>
#include <algorithm>
#include <sstream>

namespace xl {

// ---------------------------------------------------------------------------
// 本文件共用的小工具
// ---------------------------------------------------------------------------
namespace {

bool numArg(const Value& v, double& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    Value e;
    if (!toNumber(v, out, e)) { err = e; return false; }
    return true;
}

// 把参数解释成二维数值矩阵。
// - 单个标量 -> 1x1
// - 区域 / 数组 -> 按原样
// - 区域内的文本与空单元格在矩阵运算里是错误（Excel 报 #VALUE!），
//   这与 SUM 的"静默忽略"不同，不要复用 flattenArgs。
bool toMatrix(const Value& v, Array2D& m, Value& err) {
    m.clear();
    if (v.isError()) { err = v; return false; }
    if (v.isNum()) { m = {{v}}; return true; }
    if (v.isBool()) { m = {{Value::num(v.b ? 1.0 : 0.0)}}; return true; }
    if (v.isArray()) {
        if (!v.arr || v.arr->empty()) { err = Value::error(Err::Value); return false; }
        m = *v.arr;
        // 补齐成矩形（区域里缺的格子按 0，Excel 报 #VALUE!，这里更宽容些）
        size_t w = 0;
        for (auto& row : m) w = std::max(w, row.size());
        for (auto& row : m) row.resize(w, Value::empty());
        return true;
    }
    if (v.isEmpty()) { err = Value::error(Err::Value); return false; }
    err = Value::error(Err::Value);
    return false;
}

double matrixAt(const Array2D& m, size_t r, size_t c, Value& err) {
    if (r >= m.size() || c >= m[r].size()) { err = Value::error(Err::Value); return 0; }
    const Value& v = m[r][c];
    if (v.isNum()) return v.n;
    if (v.isEmpty()) return 0.0;                 // 空单元格按 0（比 Excel 宽松，方便实用）
    if (v.isBool()) return v.b ? 1.0 : 0.0;
    err = Value::error(Err::Value);              // 文本 -> #VALUE!
    return 0;
}

Value makeNumMatrix(const std::vector<std::vector<double>>& d) {
    auto a = std::make_shared<Array2D>();
    a->resize(d.size());
    for (size_t i = 0; i < d.size(); i++)
        for (double v : d[i]) (*a)[i].push_back(Value::num(v));
    return Value::array(a);
}

} // namespace

// 用 RAII registrar：静态对象在 main 前完成注册。
// 注意不能写成"需要外部调用的普通函数"—— 那样如果没人调用，
// 编译链接都不会报错，但函数表里根本没有这批函数（这次就踩了）。
struct FillRegistrar {
    FillRegistrar();
};
static FillRegistrar g_fillReg;

FillRegistrar::FillRegistrar() {
    // -----------------------------------------------------------------------
    // 三角补全：Excel 有完整的一套，缺一个就会 #NAME?
    // -----------------------------------------------------------------------
    registerFunction("COT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        double s = std::sin(x);
        if (s == 0) return Value::error(Err::Div0);
        return Value::num(std::cos(x) / s);
    });
    registerFunction("CSC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        double s = std::sin(x);
        if (s == 0) return Value::error(Err::Div0);
        return Value::num(1.0 / s);
    });
    registerFunction("SEC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        double c = std::cos(x);
        if (c == 0) return Value::error(Err::Div0);
        return Value::num(1.0 / c);
    });
    // ACOT 的值域是 (0, π)，与 atan 的 (-π/2, π/2) 不同 —— Excel 用前者
    registerFunction("ACOT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        return Value::num(1.5707963267948966 - std::atan(x));
    });
    registerFunction("COTH", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        if (x == 0) return Value::error(Err::Div0);   // coth(0) 发散
        return Value::num(1.0 / std::tanh(x));
    });
    registerFunction("CSCH", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        if (x == 0) return Value::error(Err::Div0);
        return Value::num(1.0 / std::sinh(x));
    });
    registerFunction("SECH", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        return Value::num(1.0 / std::cosh(x));        // cosh 恒 >= 1，不会除零
    });
    registerFunction("ACOTH", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        if (std::fabs(x) <= 1.0) return Value::error(Err::Num);  // 定义域 |x|>1
        return Value::num(0.5 * std::log((x + 1.0) / (x - 1.0)));
    });

    // -----------------------------------------------------------------------
    // 矩阵
    // -----------------------------------------------------------------------
    registerFunction("MUNIT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double dn; Value e;
        if (!numArg(a[0], dn, e)) return e;
        int n = (int)std::floor(dn);
        if (n < 1 || n > 1000) return Value::error(Err::Value);
        std::vector<std::vector<double>> m((size_t)n, std::vector<double>((size_t)n, 0.0));
        for (int i = 0; i < n; i++) m[i][i] = 1.0;
        return makeNumMatrix(m);
    });

    registerFunction("MMULT", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Array2D A, B; Value e;
        if (!toMatrix(a[0], A, e)) return e;
        if (!toMatrix(a[1], B, e)) return e;
        size_t m = A.size();
        size_t kA = A.empty() ? 0 : A[0].size();
        size_t kB = B.size();
        size_t n = B.empty() ? 0 : B[0].size();
        // 维度必须匹配：A 的列数 == B 的行数
        if (kA != kB || m == 0 || n == 0) return Value::error(Err::Value);

        std::vector<std::vector<double>> out(m, std::vector<double>(n, 0.0));
        for (size_t i = 0; i < m; i++)
            for (size_t j = 0; j < n; j++) {
                double sum = 0;
                for (size_t p = 0; p < kA; p++) {
                    double x = matrixAt(A, i, p, e); if (e.isError()) return e;
                    double y = matrixAt(B, p, j, e); if (e.isError()) return e;
                    sum += x * y;
                }
                out[i][j] = sum;
            }
        return makeNumMatrix(out);
    });

    registerFunction("MDETERM", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Array2D A; Value e;
        if (!toMatrix(a[0], A, e)) return e;
        size_t n = A.size();
        if (n == 0 || A[0].size() != n) return Value::error(Err::Value);  // 必须方阵

        std::vector<std::vector<double>> m(n, std::vector<double>(n));
        for (size_t i = 0; i < n; i++)
            for (size_t j = 0; j < n; j++) { m[i][j] = matrixAt(A, i, j, e); if (e.isError()) return e; }

        // LU 分解（部分选主元）：det = Π U[i][i] * (-1)^换主元次数
        double det = 1.0;
        int sign = 1;
        for (size_t k = 0; k < n; k++) {
            size_t piv = k;
            double best = std::fabs(m[k][k]);
            for (size_t i = k + 1; i < n; i++)
                if (std::fabs(m[i][k]) > best) { best = std::fabs(m[i][k]); piv = i; }
            if (best < 1e-300) return Value::num(0);        // 奇异
            if (piv != k) { std::swap(m[piv], m[k]); sign = -sign; }
            det *= m[k][k];
            for (size_t i = k + 1; i < n; i++) {
                double f = m[i][k] / m[k][k];
                for (size_t j = k; j < n; j++) m[i][j] -= f * m[k][j];
            }
        }
        return Value::num(sign * det);
    });

    registerFunction("MINVERSE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Array2D A; Value e;
        if (!toMatrix(a[0], A, e)) return e;
        size_t n = A.size();
        if (n == 0 || A[0].size() != n) return Value::error(Err::Value);

        std::vector<std::vector<double>> m(n, std::vector<double>(n));
        for (size_t i = 0; i < n; i++)
            for (size_t j = 0; j < n; j++) { m[i][j] = matrixAt(A, i, j, e); if (e.isError()) return e; }

        // 高斯-约当：增广 [A | I] 消元成 [I | A^-1]
        std::vector<std::vector<double>> inv(n, std::vector<double>(n, 0.0));
        for (size_t i = 0; i < n; i++) inv[i][i] = 1.0;

        for (size_t k = 0; k < n; k++) {
            size_t piv = k;
            double best = std::fabs(m[k][k]);
            for (size_t i = k + 1; i < n; i++)
                if (std::fabs(m[i][k]) > best) { best = std::fabs(m[i][k]); piv = i; }
            // 奇异矩阵：Excel 报 #NUM!
            if (best < 1e-12) return Value::error(Err::Num);
            if (piv != k) { std::swap(m[piv], m[k]); std::swap(inv[piv], inv[k]); }
            double d = m[k][k];
            for (size_t j = 0; j < n; j++) { m[k][j] /= d; inv[k][j] /= d; }
            for (size_t i = 0; i < n; i++) {
                if (i == k) continue;
                double f = m[i][k];
                if (f == 0) continue;
                for (size_t j = 0; j < n; j++) {
                    m[i][j] -= f * m[k][j];
                    inv[i][j] -= f * inv[k][j];
                }
            }
        }
        return makeNumMatrix(inv);
    });
}

} // namespace xl
