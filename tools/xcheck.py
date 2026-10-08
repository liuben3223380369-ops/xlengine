#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
用 SciPy / NumPy 独立计算，与 xlengine 的求值结果逐项对照。

目的：引擎"算得对不对"这件事，靠自测说不清 —— 自测只能证明自洽。
SciPy 是完全独立的实现，两边对得上才有说服力。

用法：
    make tools/evalbatch
    python3 tools/xcheck.py <公式列表文件>
"""
import subprocess, sys, os, math

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "evalbatch")

def run_engine(formulas, path="/tmp/xcheck_list.txt"):
    with open(path, "w", encoding="utf-8") as f:
        for x in formulas:
            f.write(x + "\n")
    r = subprocess.run([EXE, path], capture_output=True, text=True)
    out = {}
    for line in r.stdout.splitlines():
        if "\t" not in line:
            continue
        k, v = line.split("\t", 1)
        out[k.strip()] = v.strip()
    return out

def f(x):
    try:
        return float(x)
    except Exception:
        return None

def main():
    import scipy.stats as st
    import numpy as np

    cases = []   # (公式, 期望值, 容差, 说明)

    def add(formula, expect, tol=1e-9, note=""):
        cases.append((formula, expect, tol, note))

    # ---- 正态分布 ----
    for z in [-3, -2, -1.5, -1, -0.5, 0, 0.5, 1, 1.5, 2, 3]:
        add(f"NORMSDIST({z})", st.norm.cdf(z), 1e-9, "Φ(z)")
    for p in [0.001, 0.01, 0.1, 0.25, 0.5, 0.75, 0.9, 0.975, 0.99, 0.999]:
        add(f"NORMSINV({p})", st.norm.ppf(p), 1e-7, "Φ⁻¹(p)")
    for mu, sd in [(0, 1), (10, 2), (-5, 0.5)]:
        for x in [mu - 2 * sd, mu, mu + 2 * sd]:
            add(f"NORMDIST({x},{mu},{sd},TRUE)", st.norm.cdf(x, mu, sd), 1e-9, "cdf")
            add(f"NORMDIST({x},{mu},{sd},FALSE)", st.norm.pdf(x, mu, sd), 1e-9, "pdf")
    for p in [0.05, 0.5, 0.95]:
        add(f"NORMINV({p},10,2)", st.norm.ppf(p, 10, 2), 1e-7, "ppf")

    # ---- t 分布 ----
    for df in [1, 2, 5, 10, 30, 100]:
        for x in [0, 1, 2, 3]:
            add(f"T.DIST({x},{df},TRUE)", st.t.cdf(x, df), 1e-9, "cdf")
            add(f"T.DIST.RT({x},{df})", st.t.sf(x, df), 1e-9, "右尾 sf")
            add(f"T.DIST.2T({x},{df})", 2 * st.t.sf(abs(x), df), 1e-9, "双尾")
    for df in [1, 5, 10, 30]:
        for p in [0.9, 0.95, 0.975, 0.99]:
            add(f"T.INV({p},{df})", st.t.ppf(p, df), 1e-7, "ppf")
            add(f"T.INV.2T({1 - p},{df})", st.t.ppf(1 - (1 - p) / 2, df), 1e-7, "双尾 ppf")

    # ---- 卡方 ----
    for df in [1, 2, 5, 10, 20]:
        for x in [0.5, 2, 5, 10, 20]:
            add(f"CHISQ.DIST({x},{df},TRUE)", st.chi2.cdf(x, df), 1e-9, "cdf")
            add(f"CHISQ.DIST.RT({x},{df})", st.chi2.sf(x, df), 1e-9, "右尾")
    for df in [2, 5, 10]:
        for p in [0.05, 0.5, 0.95]:
            add(f"CHISQ.INV({p},{df})", st.chi2.ppf(p, df), 1e-7, "ppf")
            add(f"CHISQ.INV.RT({p},{df})", st.chi2.isf(p, df), 1e-7, "右尾反函数")

    # ---- F 分布 ----
    for d1, d2 in [(1, 5), (2, 10), (5, 5), (3, 20), (10, 1)]:
        for x in [0.1, 0.5, 1, 2, 5]:
            add(f"F.DIST({x},{d1},{d2},TRUE)", st.f.cdf(x, d1, d2), 1e-9, "cdf")
            add(f"F.DIST.RT({x},{d1},{d2})", st.f.sf(x, d1, d2), 1e-9, "右尾")
    for d1, d2 in [(5, 5), (1, 10), (3, 20)]:
        for p in [0.01, 0.05, 0.5]:
            add(f"F.INV({p},{d1},{d2})", st.f.ppf(p, d1, d2), 1e-7, "左尾 ppf")
            # 右尾反函数：尾概率 = p 的分位点
            add(f"F.INV.RT({p},{d1},{d2})", st.f.isf(p, d1, d2), 1e-7, "右尾 isf")

    # ---- 二项 ----
    for n in [5, 10, 20]:
        for p in [0.3, 0.5]:
            for k in [0, 1, n // 2, n - 1, n]:
                add(f"BINOM.DIST({k},{n},{p},FALSE)", st.binom.pmf(k, n, p), 1e-9, "pmf")
                add(f"BINOM.DIST({k},{n},{p},TRUE)", st.binom.cdf(k, n, p), 1e-9, "cdf")
    for n, p in [(10, 0.5), (20, 0.3)]:
        for q in [0.2, 0.5, 0.8]:
            add(f"BINOM.INV({n},{p},{q})", st.binom.ppf(q, n, p), 1e-9, "临界值")

    # ---- 泊松 ----
    for m in [1, 3, 10]:
        for k in [0, 1, 3, 10]:
            add(f"POISSON.DIST({k},{m},FALSE)", st.poisson.pmf(k, m), 1e-9, "pmf")
            add(f"POISSON.DIST({k},{m},TRUE)", st.poisson.cdf(k, m), 1e-9, "cdf")

    # ---- 指数 ----
    for lam in [0.5, 1, 2]:
        for x in [0, 0.5, 1, 3]:
            add(f"EXPON.DIST({x},{lam},FALSE)", st.expon.pdf(x, scale=1 / lam), 1e-9, "pdf")
            add(f"EXPON.DIST({x},{lam},TRUE)", st.expon.cdf(x, scale=1 / lam), 1e-9, "cdf")

    # ---- Gamma ----
    for a in [1, 2, 5]:
        for b in [1, 2]:
            for x in [0.5, 2, 5]:
                add(f"GAMMA.DIST({x},{a},{b},TRUE)", st.gamma.cdf(x, a, scale=b), 1e-8, "cdf")
                add(f"GAMMA.DIST({x},{a},{b},FALSE)", st.gamma.pdf(x, a, scale=b), 1e-8, "pdf")
    for a, b in [(2, 1), (5, 2)]:
        for p in [0.1, 0.5, 0.9]:
            add(f"GAMMA.INV({p},{a},{b})", st.gamma.ppf(p, a, scale=b), 1e-6, "ppf")

    # ---- Beta ----
    for a, b in [(2, 3), (0.5, 0.5), (5, 1)]:
        for x in [0.1, 0.3, 0.5, 0.9]:
            add(f"BETA.DIST({x},{a},{b},TRUE)", st.beta.cdf(x, a, b), 1e-9, "cdf")
    for a, b in [(2, 3), (5, 1)]:
        for p in [0.1, 0.5, 0.9]:
            add(f"BETA.INV({p},{a},{b})", st.beta.ppf(p, a, b), 1e-7, "ppf")

    # ---- 对数正态 ----
    for mu, sd in [(0, 1), (1, 0.5)]:
        for x in [0.5, 1, 2, 5]:
            add(f"LOGNORM.DIST({x},{mu},{sd},TRUE)", st.lognorm.cdf(x, sd, scale=math.exp(mu)), 1e-9, "cdf")

    # ---- 负二项 ----
    for r, p in [(2, 0.5), (5, 0.3)]:
        for k in [0, 1, 3, 8]:
            # Excel NEGBINOM.DIST(k, r, p) 是"失败 k 次前成功 r 次"
            add(f"NEGBINOM.DIST({k},{r},{p},FALSE)", st.nbinom.pmf(k, r, p), 1e-9, "pmf")

    # ---- 超几何 ----
    for N in [20, 50]:
        for K in [5, 10]:
            for n in [3, 8]:
                for k in [0, 2, 3]:
                    if K <= N and n <= N:
                        add(f"HYPGEOM.DIST({k},{n},{K},{N},FALSE)",
                            st.hypergeom.pmf(k, N, K, n), 1e-9, "pmf")

    # ---- 误差函数 ----
    for x in [0, 0.5, 1, 1.5, 2]:
        add(f"ERF({x})", math.erf(x), 1e-12, "erf")
        add(f"ERFC({x})", math.erfc(x), 1e-12, "erfc")

    # ---- 贝塞尔 ----
    from scipy.special import iv, kv, yn, jv
    for n in [0, 1, 2]:
        # 0.1 与 12 是刻意加的：级数在大 x 处项会先增后减，容易因抵消而崩
        for x in [0.1, 0.5, 1, 2, 5, 12]:
            add(f"BESSELI({x},{n})", float(iv(n, x)), 1e-8, "修正贝塞尔 I")
            add(f"BESSELK({x},{n})", float(kv(n, x)), 1e-8, "修正贝塞尔 K")
            add(f"BESSELY({x},{n})", float(yn(n, x)), 1e-8, "第二类贝塞尔 Y")
            add(f"BESSELJ({x},{n})", float(jv(n, x)), 1e-8, "第一类贝塞尔 J")

    # ---- 统计量 ----
    data = [2, 4, 4, 4, 5, 5, 7, 9]
    ds = ",".join(str(x) for x in data)
    add(f"STDEV.S({{{ds}}})", np.std(data, ddof=1), 1e-9, "样本标准差")
    add(f"STDEV.P({{{ds}}})", np.std(data, ddof=0), 1e-9, "总体标准差")
    add(f"VAR.S({{{ds}}})", np.var(data, ddof=1), 1e-9, "样本方差")
    add(f"VAR.P({{{ds}}})", np.var(data, ddof=0), 1e-9, "总体方差")
    add(f"AVERAGE({{{ds}}})", float(np.mean(data)), 1e-12, "均值")
    add(f"MEDIAN({{{ds}}})", float(np.median(data)), 1e-12, "中位数")
    add(f"SKEW({{{ds}}})", float(st.skew(data, bias=False)), 1e-7, "偏度")
    add(f"KURT({{{ds}}})", float(st.kurtosis(data, bias=False)), 1e-7, "峰度")
    add(f"GEOMEAN({{{ds}}})", float(st.gmean(data)), 1e-9, "几何平均")
    add(f"HARMEAN({{{ds}}})", float(st.hmean(data)), 1e-9, "调和平均")
    # 分位数：Excel QUARTILE.INC 用 (n-1)*p+1 线性插值，与 numpy 的 linear 一致
    for k in [0, 1, 2, 3, 4]:
        add(f"QUARTILE.INC({{{ds}}},{k})", float(np.percentile(data, 25 * k)), 1e-9, f"Q{k}")
    add(f"PERCENTILE.INC({{{ds}}},0.25)", float(np.percentile(data, 25)), 1e-9, "P25")
    add(f"PERCENTILE.INC({{{ds}}},0.9)", float(np.percentile(data, 90)), 1e-9, "P90")

    # ---- 相关系数 / 协方差 ----
    xs = [1, 2, 3, 4, 5]; ys = [2, 4, 5, 4, 5]
    xss = ",".join(map(str, xs)); yss = ",".join(map(str, ys))
    add(f"CORREL({{{xss}}},{{{yss}}})", float(np.corrcoef(xs, ys)[0, 1]), 1e-9, "皮尔逊")
    add(f"COVARIANCE.S({{{xss}}},{{{yss}}})", float(np.cov(xs, ys, ddof=1)[0, 1]), 1e-9, "样本协方差")
    add(f"COVARIANCE.P({{{xss}}},{{{yss}}})", float(np.cov(xs, ys, ddof=0)[0, 1]), 1e-9, "总体协方差")

    # ---- 威布尔 ----
    for a, b in [(1, 1), (2, 3), (0.5, 2)]:
        for x in [0.5, 1, 3]:
            add(f"WEIBULL.DIST({x},{a},{b},TRUE)", st.weibull_min.cdf(x, a, scale=b), 1e-9, "cdf")

    # ---- 矩阵运算：用 numpy.linalg 对照 ----
    A = [[4, 7], [2, 6]]
    Ainv = np.linalg.inv(np.array(A, dtype=float))
    add("MINVERSE({4,7;2,6})", None, 1e-9, "占位:下面用数组比对")
    cases.pop()   # 数组型单独处理，这里不放

    # ---- 线性回归 LINEST：用 numpy lstsq 对照 ----
    ys2 = [1, 2, 3, 4, 5]
    xs2 = [2, 4, 6, 8, 10]
    Xd = np.vstack([np.array(xs2, dtype=float), np.ones(len(xs2))]).T
    slope, intercept = np.linalg.lstsq(Xd, np.array(ys2, dtype=float), rcond=None)[0]
    xss2 = ",".join(map(str, xs2)); yss2 = ",".join(map(str, ys2))
    add(f"SLOPE({{{yss2}}},{{{xss2}}})", float(slope), 1e-9, "最小二乘斜率")
    add(f"INTERCEPT({{{yss2}}},{{{xss2}}})", float(intercept), 1e-9, "最小二乘截距")

    # ---- 组合数 / 阶乘 ----
    from math import comb as _comb, factorial as _fact, lgamma
    for n, k in [(10, 3), (20, 5), (49, 6)]:
        add(f"COMBIN({n},{k})", float(_comb(n, k)), 1e-9, "组合数")
        add(f"COMBINA({n},{k})", float(_comb(n + k - 1, k)), 1e-9, "可重复组合")
    for n in [0, 1, 5, 10, 20]:
        add(f"FACT({n})", float(_fact(n)), 1e-6, "阶乘")
    def _factdouble(n):
        # n!! = n×(n-2)×… 一直乘到 1（奇）或 2（偶）。
        # 我第一版写成了一堆错乱的阶乘组合，得 160；正确 5!! = 5×3×1 = 15。
        r = 1
        while n > 1:
            r *= n
            n -= 2
        return float(r)
    for n in [5, 10, 20]:
        add(f"FACTDOUBLE({n})", _factdouble(n), 1e-6, "双阶乘")

    # ---- Gamma / Beta 函数（特殊函数，非分布）----
    from scipy.special import gamma as G, beta as B
    for x in [0.5, 1, 2, 5, 10]:
        add(f"GAMMA({x})", float(G(x)), 1e-8, "Γ(x)")
        add(f"GAMMALN({x})", float(lgamma(x)), 1e-9, "lnΓ(x)")
    # 注意：Excel 没有独立的 BETA() 函数（只有 BETA.DIST / BETA.INV），
    # 所以引擎返回错误是对的 —— 这里不放 BETA()，只保留 BETA.DIST 的对照。

    # ---- 双曲 / 反双曲 ----
    for x in [0, 0.5, 1, 2]:
        add(f"SINH({x})", math.sinh(x), 1e-12, "sinh")
        add(f"COSH({x})", math.cosh(x), 1e-12, "cosh")
        add(f"TANH({x})", math.tanh(x), 1e-12, "tanh")
        add(f"ASINH({x})", math.asinh(x), 1e-12, "asinh")
        add(f"ACOSH({x + 1})", math.acosh(x + 1), 1e-12, "acosh")
        if x < 1:
            add(f"ATANH({x})", math.atanh(x), 1e-12, "atanh")

    # ---- 对数 / 指数 ----
    for x in [1, 2, 10, 100, 1000]:
        add(f"LN({x})", math.log(x), 1e-12, "自然对数")
        add(f"LOG10({x})", math.log10(x), 1e-12, "常用对数")
        if x <= 700:   # e^1000 在双精度下溢出，Excel 同样返回 #NUM!
            add(f"EXP({x})", math.exp(x), 1e-9, "e^x")
        add(f"SQRT({x})", math.sqrt(x), 1e-12, "平方根")
    for x in [8, 27, 100]:
        add(f"LOG({x},2)", math.log(x, 2), 1e-12, "任意底对数")

    # ---- 取整语义（Excel 的暗坑，numpy 可对照）----
    for x in [-2.5, -0.5, 0.5, 2.5]:
        add(f"INT({x})", float(math.floor(x)), 1e-12, "INT 向下取整")
        # Excel 的 ROUND 是"四舍五入、远离零"，而 Python 的 round() 是
        # banker's rounding（2.5 → 2、-2.5 → -2）。用错参照会得到假失败。
        add(f"ROUND({x},0)", float(math.floor(x + 0.5) if x >= 0 else math.ceil(x - 0.5)), 1e-12, "四舍五入（远离零）")
        add(f"ROUNDDOWN({x},0)", float(math.trunc(x)), 1e-12, "向零")
        add(f"ROUNDUP({x},0)", float(math.trunc(x) if abs(x - int(x)) == 0 else (math.floor(x) if x < 0 else math.ceil(x))), 1e-12, "远离零")

    # ---- 财务：NPV / IRR 用 numpy 求根对照 ----
    flows = [-1000, 300, 400, 500]
    fs = ",".join(str(x) for x in flows)
    for r in [0.05, 0.1, 0.15]:
        # Excel 的 NPV 从**第 1 期**开始折现（values[0] 也要折一期），
        # 而 IRR 的第 0 期不折现 —— 两者约定不同，这是最容易写错的一处。
        expect_npv = sum(c / (1 + r) ** (i + 1) for i, c in enumerate(flows))
        add(f"NPV({r},{{{fs}}})", expect_npv, 1e-9, "NPV（首期在第1期折现）")
    # IRR：解 NPV=0 的 r，用 numpy.roots
    # IRR 定义：values[0] 在**第 0 期**（不折现），其余在第 i 期。
    # 用二分法直接解 NPV_IRR(r)=0，比多项式求根再变换更不容易写错。
    def npv_irr(r, fl):
        return sum(c / (1 + r) ** i for i, c in enumerate(fl))
    lo, hi = -0.999, 10.0
    if npv_irr(lo, flows) * npv_irr(hi, flows) < 0:
        for _ in range(300):
            mid = (lo + hi) / 2
            if npv_irr(lo, flows) * npv_irr(mid, flows) <= 0: hi = mid
            else: lo = mid
        add(f"IRR({{{fs}}})", (lo + hi) / 2, 1e-7, "IRR（二分求根；第0期不折现）")

    # ---- 矩阵：MINVERSE / MDETERM / MMULT 用 numpy.linalg 对照 ----
    # 引擎接受 "{a,b;c,d}" 形式的二维数组字面量
    def m2s(M):
        return "{" + ";".join(",".join(repr(float(v)) for v in row) for row in M) + "}"
    for M in [[[4, 7], [2, 6]], [[1, 2], [3, 4]], [[2, 0], [0, 3]]]:
        A = np.array(M, dtype=float)
        add(f"MDETERM({m2s(M)})", float(np.linalg.det(A)), 1e-9, "行列式")
        # 逆矩阵左上角元素作为代表标量（数组型在下面单独比对）
        add(f"INDEX(MINVERSE({m2s(M)}),1,1)", float(np.linalg.inv(A)[0, 0]), 1e-9, "逆矩阵[0,0]")
        add(f"INDEX(MINVERSE({m2s(M)}),2,2)", float(np.linalg.inv(A)[1, 1]), 1e-9, "逆矩阵[1,1]")
    # 转置
    for M in [[[1, 2, 3]], [[1, 2], [3, 4]]]:
        A = np.array(M, dtype=float)
        add(f"INDEX(TRANSPOSE({m2s(M)}),1,1)", float(A.T[0, 0]), 1e-9, "转置[0,0]")

    # ---- LINEST：多元回归系数，用 numpy lstsq 对照 ----
    # y = 1*x1 + 2*x2 + 3
    x1 = [1, 2, 3, 4, 5]; x2 = [2, 1, 4, 3, 5]
    yv = [1 * a + 2 * b + 3 for a, b in zip(x1, x2)]
    Xd = np.column_stack([np.array(x1, float), np.array(x2, float), np.ones(5)])
    beta = np.linalg.lstsq(Xd, np.array(yv, float), rcond=None)[0]
    # Excel LINEST 返回系数逆序（最后一个自变量在前，最后是截距）
    add(f"INDEX(LINEST({{{','.join(map(str, yv))}}},"
        f"{{{';'.join(','.join(map(str, r)) for r in zip(x1, x2))}}}),1,1)",
        float(beta[1]), 1e-6, "LINEST 系数 x2")
    add(f"INDEX(LINEST({{{','.join(map(str, yv))}}},"
        f"{{{';'.join(','.join(map(str, r)) for r in zip(x1, x2))}}}),1,2)",
        float(beta[0]), 1e-6, "LINEST 系数 x1")
    add(f"INDEX(LINEST({{{','.join(map(str, yv))}}},"
        f"{{{';'.join(','.join(map(str, r)) for r in zip(x1, x2))}}}),1,3)",
        float(beta[2]), 1e-6, "LINEST 截距")

    # ---- 执行 ----
    formulas = [c[0] for c in cases]
    print(f"共 {len(formulas)} 条公式，交给引擎求值…")
    res = run_engine(formulas)

    P = N = 0
    fails = []
    for formula, expect, tol, note in cases:
        got_s = res.get(formula)
        if got_s is None:
            N += 1; fails.append((formula, "无输出", expect, note)); continue
        if got_s.startswith("<ERR>") or got_s.startswith("<PARSE") or got_s.startswith("<STR"):
            N += 1; fails.append((formula, got_s, expect, note)); continue
        got = f(got_s)
        if got is None:
            N += 1; fails.append((formula, got_s, expect, note)); continue
        # 期望值极小时用绝对容差兜底
        ok = abs(got - expect) <= max(tol, abs(expect) * tol)
        if ok:
            P += 1
        else:
            N += 1
            fails.append((formula, got, expect, note))

    print(f"\n对照 SciPy：通过 {P} 项，不符 {N} 项")
    if fails:
        print("\n不符明细（前 40 条）：")
        for fm, g, e, n in fails[:40]:
            print(f"  {fm:<44} 引擎={g!s:<24} SciPy={e!s:<24} [{n}]")
        if len(fails) > 40:
            print(f"  …还有 {len(fails)-40} 条")
    return 1 if N else 0

if __name__ == "__main__":
    sys.exit(main())
