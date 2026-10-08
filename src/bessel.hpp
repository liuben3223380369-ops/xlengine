#pragma once
// ---------------------------------------------------------------------------
// 贝塞尔函数的自持实现
// ---------------------------------------------------------------------------
// 原先直接用 std::cyl_bessel_i/j/k 与 std::cyl_neumann —— 它们是 C++17 的
// "数学特殊函数"，libstdc++ 有，但 **libc++（Android NDK 默认）没有**，
// 在 CI 上直接编译失败：
//     error: no member named 'cyl_bessel_i' in namespace 'std'
//
// 与其依赖一个并非到处都有的标准库扩展，不如自己实现，顺带统一精度行为。
//
// 实现方法（与 SciPy 对照验证过，见 tools/xcheck.py）：
//   J_n(x) —— n 为整数时用幂级数；小 x 收敛很快
//   Y_n(x) —— 由 J 的递推 + Neumann 级数得到（这里用 J 与 Y0 的组合）
//   I_n(x) —— 就是 J_n 的级数去掉交错符号（I_n(x) = i^-n J_n(ix)）
//   K_n(x) —— 用 n=0,1 的级数启动，再向上递推
//
// 适用范围：x > 0、n 为非负整数（Excel 的 BESSEL* 只接受非负整数阶）。
// x 很大时级数收敛慢，但 Excel 的常规用法（x 在 0~几十）精度足够。
#include <cmath>

namespace xl {
namespace bessel {

// ---- 修正贝塞尔 I_n(x)：I_n(x) = Σ (x/2)^(2k+n) / (k! (k+n)!) ----
inline double I(int n, double x) {
    if (x < 0) x = -x;              // I_n 对符号：n 为整数时 I_n(-x)=(-1)^n I_n(x)，
                                    // 但 Excel 的 BESSELI 只取非负整数阶且通常 x>=0
    double half = x / 2.0;
    double term = 1.0;
    for (int i = 1; i <= n; i++) term *= half / i;   // (x/2)^n / n!
    double sum = term;
    double h2 = half * half;
    for (int k = 1; k < 500; k++) {
        term *= h2 / (double)(k * (k + n));
        sum += term;
        // 项已经小到不影响结果就停 —— 否则大 x 时会跑满 500 次还发散
        if (term < 1e-18 * (sum > 0 ? sum : 1.0) || term == 0.0) break;
    }
    return sum;
}

// ---- 第一类贝塞尔 J_n(x)：交错符号的同一级数 ----
inline double J(int n, double x) {
    double half = x / 2.0;
    if (half < 0) half = -half;
    double term = 1.0;
    for (int i = 1; i <= n; i++) term *= half / i;
    double sum = term;
    double h2 = half * half;
    for (int k = 1; k < 500; k++) {
        term *= -h2 / (double)(k * (k + n));
        sum += term;
        if (std::fabs(term) < 1e-18 * (std::fabs(sum) > 0 ? std::fabs(sum) : 1.0) || term == 0.0)
            break;
    }
    // J_n(-x) = (-1)^n J_n(x)
    if (x < 0 && (n % 2 != 0)) return -sum;
    return sum;
}

namespace detail {
// Y_0(x)：含对数与欧拉常数的级数（与 SciPy 逐点核对过）
//
//   Y_0(x) = (2/π)[(ln(x/2)+γ)·J_0(x) + Σ_{k>=1} H_k·(-1)^(k+1)·(x²/4)^k/(k!)²]
//
// 符号极易写错：第一版把 (-1)^(k+1) 写成了 (-1)^k，
// 结果 x=0.5 时得 -0.5222，正确是 -0.4445 —— 全靠 SciPy 对照才发现。
inline double Y0(double x) {
    const double EULER = 0.5772156649015328606;
    double h = x / 2.0;
    double h2 = h * h;
    double term = 1.0;      // 累积 (-1)^k·h2^k/(k!)²
    double sum = 0.0;
    double H = 0.0;         // 调和数 H_k
    for (int k = 1; k < 400; k++) {
        H += 1.0 / k;
        term *= -h2 / (double)(k * k);          // (-1)^k
        double add = -term * H;                 // (-1)^(k+1)
        sum += add;
        if (std::fabs(add) < 1e-18 * (std::fabs(sum) + 1.0)) break;
    }
    return (2.0 / M_PI) * ((std::log(h) + EULER) * J(0, x) + sum);
}

// Y_1(x)：同样用级数，而不是靠 Wronskian 关系从 Y_0 反推。
//
// Wronskian 是 J_0·Y_1 - J_1·Y_0 = 2/(πx) ⇒ Y_1 = (J_1·Y_0 + 2/(πx))/J_0，
// 但 **J_0 有零点**（第一个在 x≈2.4048），在那里除法会把结果放大到荒谬的值。
// 所以直接上级数：
//
//   Y_1(x) = (2/π)(ln(x/2)+γ)·J_1(x) - 2/(πx)
//            - (x/(2π))·Σ_{k>=0} (-1)^k (x²/4)^k/(k!(k+1)!)·(H_k + H_{k+1})
inline double Y1(double x) {
    const double EULER = 0.5772156649015328606;
    double h2 = x * x / 4.0;
    double term = 1.0;      // (-1)^k h2^k/(k!(k+1)!)
    double sum = 0.0;
    double Hk = 0.0, Hk1 = 1.0;
    for (int k = 0; k < 400; k++) {
        if (k > 0) {
            Hk += 1.0 / k;
            Hk1 += 1.0 / (k + 1);
            term *= -h2 / (double)(k * (k + 1));
        }
        sum += term * (Hk + Hk1);
        if (std::fabs(term * (Hk + Hk1)) < 1e-18 * (std::fabs(sum) + 1.0)) break;
    }
    return (2.0 / M_PI) * (std::log(x / 2.0) + EULER) * J(1, x)
           - 2.0 / (M_PI * x)
           - (x / (2.0 * M_PI)) * sum;
}
}  // namespace detail

// ---- 第二类贝塞尔 Y_n(x)（Neumann 函数）----
// Y_0 / Y_1 用级数，更高阶用向上递推 —— Y 在这个方向是稳定的。
inline double Y(int n, double x) {
    if (x <= 0) return 0.0;
    if (n == 0) return detail::Y0(x);
    double y0 = detail::Y0(x);
    double y1 = detail::Y1(x);
    if (n == 1) return y1;
    double prev = y0, cur = y1;
    for (int k = 1; k < n; k++) {
        double next = (2.0 * k / x) * cur - prev;
        prev = cur;
        cur = next;
    }
    return cur;
}

// ---- 第二类修正贝塞尔 K_n(x) ----
// K_0 用级数（含对数），K_1 由 K_1 = K_0*I_1 + 1/x 的关系（Wronskian 变形），
// 之后向上递推 K_{n+1} = K_{n-1} + (2n/x) K_n。
inline double K(int n, double x) {
    if (x <= 0) return 0.0;
    const double EULER = 0.5772156649015328606;
    double half = x / 2.0;
    double l = std::log(half);
    double h2 = half * half;
    // K_0(x) = -(ln(x/2)+γ) I_0(x) + Σ_{k>=1} (h2)^k/(k!)^2 * H_k
    double term = 1.0, sum = 0.0, H = 0.0;
    for (int k = 1; k < 300; k++) {
        H += 1.0 / k;
        term *= h2 / (double)(k * k);
        double add = term * H;
        sum += add;
        if (add < 1e-18 * (sum + 1.0)) break;
    }
    double k0 = -(l + EULER) * I(0, x) + sum;
    if (n == 0) return k0;
    // I_0*K_1 + I_1*K_0 = 1/x
    double k1 = (1.0 / x - I(1, x) * k0) / (I(0, x) != 0 ? I(0, x) : 1e-300);
    if (n == 1) return k1;
    double prev = k0, cur = k1;
    for (int k = 1; k < n; k++) {
        double next = prev + (2.0 * k / x) * cur;
        prev = cur;
        cur = next;
    }
    return cur;
}

}  // namespace bessel
}  // namespace xl
