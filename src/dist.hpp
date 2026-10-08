#pragma once
// ---------------------------------------------------------------------------
// 统计分布所需的数学辅助。
// 从 fn_stat.cpp 抽出以便 fn_stat2.cpp 共用：所有实现都是标准数值算法
// （Numerical Recipes 的连分数展开 + Acklam 正态逆），不依赖第三方库。
// ---------------------------------------------------------------------------
#include <cmath>
#include <vector>

namespace xl {
namespace dist {



// 正则化不完全 beta 的连分数展开（Numerical Recipes betacf）
static double betacf(double a, double b, double x) {
    const int MAXIT = 200;
    const double EPS = 3.0e-12, FPMIN = 1.0e-300;
    double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0, d = 1.0 - qab * x / qap;
    if (std::fabs(d) < FPMIN) d = FPMIN;
    d = 1.0 / d; double h = d;
    for (int m = 1; m <= MAXIT; m++) {
        double m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d; if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c; if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d; h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d; if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c; if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d; double del = d * c; h *= del;
        if (std::fabs(del - 1.0) < EPS) break;
    }
    return h;
}
// 正则化不完全 beta I_x(a,b)
inline double betai(double a, double b, double x) {
    if (x <= 0) return 0.0;
    if (x >= 1) return 1.0;
    double lbeta = std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) + b * std::log(1.0 - x);
    double bt = std::exp(lbeta);
    if (x < (a + 1.0) / (a + b + 2.0)) return bt * betacf(a, b, x) / a;
    return 1.0 - bt * betacf(b, a, 1.0 - x) / b;
}

// 下不完全 gamma 的级数展开 P(a,x)
static double gser(double a, double x) {
    const int ITMAX = 500;
    const double EPS = 3.0e-12;
    double ap = a, sum = 1.0 / a, del = sum;
    for (int n = 1; n <= ITMAX; n++) {
        ap += 1.0; del *= x / ap; sum += del;
        if (std::fabs(del) < std::fabs(sum) * EPS) break;
    }
    return sum * std::exp(-x + a * std::log(x) - std::lgamma(a));
}
// 上不完全 gamma 的连分数 Q(a,x)
static double gcf(double a, double x) {
    const int ITMAX = 500;
    const double EPS = 3.0e-12, FPMIN = 1.0e-300;
    double b = x + 1.0 - a, c = 1.0 / FPMIN, d = 1.0 / b, h = d;
    for (int i = 1; i <= ITMAX; i++) {
        double an = -i * (i - a);
        b += 2.0;
        d = an * d + b; if (std::fabs(d) < FPMIN) d = FPMIN;
        c = b + an / c; if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d; double del = d * c; h *= del;
        if (std::fabs(del - 1.0) < EPS) break;
    }
    return std::exp(-x + a * std::log(x) - std::lgamma(a)) * h;
}
inline double gammp(double a, double x) {   // P(a,x)
    if (x <= 0) return 0.0;
    return (x < a + 1.0) ? gser(a, x) : 1.0 - gcf(a, x);
}
inline double gammq(double a, double x) {   // Q(a,x)
    if (x <= 0) return 1.0;
    return (x < a + 1.0) ? 1.0 - gser(a, x) : gcf(a, x);
}

// 标准正态 CDF
inline double normCdf(double z) { return 0.5 * (1.0 + std::erf(z / std::sqrt(2.0))); }
inline double normPdf(double z) { return std::exp(-0.5 * z * z) / std::sqrt(2.0 * 3.14159265358979323846); }

// 逆标准正态（Acklam 有理逼近，精度约 1e-9）
inline double normInv(double p) {
    if (p <= 0) return -1e300;
    if (p >= 1) return 1e300;
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                                1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                                6.680131188771972e+01, -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                                -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                               3.754408661907416e+00};
    const double pLow = 0.02425, pHigh = 1.0 - pLow;
    double q, r, x;
    if (p < pLow) {
        q = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    } else if (p <= pHigh) {
        q = p - 0.5; r = q * q;
        x = (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q / (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
    } else {
        q = std::sqrt(-2.0 * std::log(1.0 - p));
        x = -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    // 一步 Halley 修正
    double e = normCdf(x) - p;
    double u = e * std::sqrt(2.0 * 3.14159265358979323846) * std::exp(x * x / 2.0);
    return x - u / (1.0 + x * u / 2.0);
}

inline double tCdf(double t, double df) {
    double x = df / (df + t * t);
    double p = 0.5 * betai(df / 2.0, 0.5, x);
    return t > 0 ? 1.0 - p : p;
}
inline double tInv(double p, double df) {
    // 二分求逆
    if (p <= 0) return -1e300;
    if (p >= 1) return 1e300;
    double lo = -100.0, hi = 100.0;
    for (int i = 0; i < 200; i++) {
        double mid = (lo + hi) / 2.0;
        if (tCdf(mid, df) < p) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2.0;
}
inline double fCdfR(double f, double d1, double d2) {
    if (f <= 0) return 1.0;
    double x = d2 / (d2 + d1 * f);
    return betai(d2 / 2.0, d1 / 2.0, x);
}
inline double fInv(double p, double d1, double d2) {
    if (p <= 0) return 1e300;
    if (p >= 1) return 0;
    double lo = 0.0, hi = 1e6;
    for (int i = 0; i < 200; i++) {
        double mid = (lo + hi) / 2.0;
        // 求右尾概率等于 p 的点。原先写成 (1.0 - p)，求的其实是左尾分位数：
        //   F.INV.RT(0.05,5,5) 给出 0.198，而正确值是 5.05 —— 恰好互为倒数
        //   （F 分布性质：X~F(d1,d2) 则 1/X~F(d2,d1)），所以看起来像"算出来了"，
        //   实际是 1-α 那一侧。对照 chiInv 的写法即可看出这里多减了一次。
        if (fCdfR(mid, d1, d2) > p) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2.0;
}
inline double chiCdfR(double x, double df) {
    if (x <= 0) return 1.0;
    return gammq(df / 2.0, x / 2.0);
}
inline double chiInv(double p, double df) {
    if (p <= 0) return 1e300;
    if (p >= 1) return 0;
    double lo = 0.0, hi = 1e6;
    for (int i = 0; i < 200; i++) {
        double mid = (lo + hi) / 2.0;
        if (chiCdfR(mid, df) > p) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2.0;
}
inline double factLn(double n) { return std::lgamma(n + 1.0); }
inline double binomPdf(double k, double n, double p) {
    if (k < 0 || k > n) return 0.0;
    return std::exp(factLn(n) - factLn(k) - factLn(n - k) + k * std::log(p) + (n - k) * std::log(1.0 - p));
}


} // namespace dist
} // namespace xl
