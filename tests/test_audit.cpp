// 审计回归测试
//
// 这一组全部来自一次完整的功能审计，每个用例都对应一个**已经修掉的真 bug**。
// 它们的共同点是：都能通过"全函数冒烟测试"（不崩溃、不返回未实现），
// 只有比对数值才抓得到。所以必须固化下来防止回归。
#include "sheet.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <string>
#include <vector>
using namespace xl;

static int P = 0, N = 0;
static void OK(bool c, const std::string& what){
    if (c) P++; else { N++; std::cout << "  FAIL: " << what << "\n"; }
}
static void num(const std::string& f, double want, double tol, const std::string& why){
    Sheet s; s.setName("A");
    std::string e = s.setFormula(5,5,f);
    if (!e.empty()) { N++; std::cout << "  FAIL(解析) " << f << ": " << e << "\n"; return; }
    s.recalc();
    Value v = s.valueAt(5,5);
    bool ok = v.isNum() && std::fabs(v.n - want) <= tol;
    if (ok) P++; else {
        N++;
        std::cout << "  FAIL: " << f << " = " << s.display(5,5)
                  << "，期望 " << want << "   [" << why << "]\n";
    }
}
static void arrImpl(const std::string& f, const std::string& want, const std::string& why);
static void arr(const std::string& f, const std::string& want, const std::string& why){ arrImpl(f,want,why); }
static void txt(const std::string& f, const std::string& want, const std::string& why){
    Sheet s; s.setName("A");
    std::string e = s.setFormula(5,5,f);
    if (!e.empty()) { N++; std::cout << "  FAIL(解析) " << f << ": " << e << "\n"; return; }
    s.recalc();
    Value v = s.valueAt(5,5);
    std::string got = v.isStr() ? v.s : s.display(5,5);
    bool ok = (got == want);
    if (ok) P++; else {
        N++;
        std::cout << "  FAIL: " << f << " = \"" << got << "\"，期望 \"" << want
                  << "\"   [" << why << "]\n";
    }
}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);

    std::cout << "== 重复实现导致的静默错误 ==\n";
    // 同一个函数名在两个 .cpp 各注册一次时，谁生效取决于未规定的静态初始化顺序。
    // 曾因此恒返回 0 —— 而返回 0 不崩溃、不报错，冒烟测试完全抓不到。
    num("PERMUTATIONA(3,2)",   9.0,   1e-9, "回归：曾被返回硬编码 0 的残桩覆盖");
    num("PERMUTATIONA(2,10)",  1024.0,1e-9, "同上");
    num("F.DIST(2,5,5,FALSE)", 0.1583,1e-3, "回归：cumulative=FALSE 应返回概率密度，曾返回 0");
    num("F.DIST(2,5,5,TRUE)",  0.7674,1e-3, "cumulative=TRUE 应返回累积概率");

    std::cout << "== 标准差用错 ==\n";
    num("KURT({1,2,3,4,5})", -1.2, 1e-9, "回归：Excel 用样本标准差，曾误用总体标准差得 2.625");
    num("STDEV({1,2,3,4})",   1.290994449, 1e-8, "样本标准差对照");
    num("STDEVP({1,2,3,4})",  1.118033989, 1e-8, "总体标准差对照");

    std::cout << "== 分布反函数 ==\n";
    num("CHIINV(0.5,5)",       4.351460191, 1e-6, "回归：求解方向取反，曾收敛到上界 100");
    num("CHIINV(0.1,5)",       9.236356900, 1e-6, "同上");
    num("CHISQ.INV.RT(0.5,5)", 4.351460191, 1e-6, "CHIINV 应与之相等");
    num("TINV(0.5,10)",        0.699812061, 1e-6, "回归：曾返回负值（左尾/右尾搞反）");
    num("TINV(0.05,10)",       2.228138852, 1e-6, "同上");
    num("T.INV.2T(0.5,10)",    0.699812061, 1e-6, "TINV 的现代别名");

    std::cout << "== 日期：周数与年分数 ==\n";
    num("ISOWEEKNUM(DATE(2024,1,1))", 1.0, 1e-9, "回归：曾整体差一周（2024-01-01 是周一）");
    num("ISOWEEKNUM(DATE(2024,1,8))", 2.0, 1e-9, "第 2 个周一起");
    num("ISOWEEKNUM(DATE(2023,1,1))",52.0, 1e-9, "2023-01-01 属 2022 年第 52 周");
    num("ISOWEEKNUM(DATE(2023,1,2))", 1.0, 1e-9, "2023 ISO 第 1 周");
    num("ISOWEEKNUM(DATE(2021,1,1))",53.0, 1e-9, "2021-01-01 属 2020 年第 53 周");
    num("ISOWEEKNUM(DATE(2021,1,4))", 1.0, 1e-9, "2021 ISO 第 1 周");
    // 30/360 曾在闰年多算一天：序列号被当成天数送进 civilFromDays
    num("YEARFRAC(DATE(2020,1,1),DATE(2021,1,1))",   1.0, 1e-9, "回归：闰年跨度曾得 1.00278");
    num("YEARFRAC(DATE(2020,1,1),DATE(2021,1,1),0)", 1.0, 1e-9, "basis=0 30/360");
    num("YEARFRAC(DATE(2020,1,1),DATE(2021,1,1),4)", 1.0, 1e-9, "basis=4 30/360 EUR");
    num("YEARFRAC(DATE(2021,1,1),DATE(2022,1,1))",   1.0, 1e-9, "平年跨度（本来就是对的）");
    num("DAYS360(DATE(2020,1,1),DATE(2021,1,1))",  360.0, 1e-9, "回归：同一根因");

    std::cout << "== 财务：付息期数 ==\n";
    num("COUPNUM(DATE(2024,1,1),DATE(2025,1,1),2)", 2.0, 1e-9, "回归：按平均月长 ceil，闰年曾多算一期得 3");
    num("COUPNUM(DATE(2024,1,1),DATE(2025,1,1),1)", 1.0, 1e-9, "年付");
    num("COUPNUM(DATE(2024,1,1),DATE(2026,1,1),2)", 4.0, 1e-9, "两年半年付");

    std::cout << "== 参数错位 ==\n";
    num("BETADIST(0.5,1,1)", 0.5, 1e-9, "回归：旧式签名无 cumulative 参数，补位错误导致 #VALUE!");

    std::cout << "== 引用：语法层才能判断的语义 ==\n";
    // ISREF / OFFSET 必须在 AST 层特判：单格引用求完值就是标量，
    // 传到函数手里已无从区分"A1 里的 42"和"数字 42"。
    {
        Sheet s; s.setName("A");
        s.setValue(0,0,Value::num(42));   // A1
        s.setValue(0,1,Value::num(7));    // A2
        s.setValue(1,0,Value::num(9));    // B1
        struct C { const char* f; const char* want; const char* why; };
        std::vector<C> cs = {
            {"ISREF(A1)",       "TRUE",  "回归：曾恒 FALSE"},
            {"ISREF(A1:A2)",    "TRUE",  "区域"},
            {"ISREF(42)",       "FALSE", "字面量"},
            {"OFFSET(A1,1,0)",  "7",     "回归：曾用“当前格当区域中心”反推锚点，全错"},
            {"OFFSET(A1,0,1)",  "9",     "同上"},
            {"OFFSET(A1,0,0)",  "42",    "同上"},
            {"INDIRECT(\"A1\")","42",    "字符串地址解析"},
        };
        for (auto& c : cs) {
            std::string e = s.setFormula(5,5,c.f);
            if (!e.empty()) { N++; std::cout << "  FAIL(解析) " << c.f << ": " << e << "\n"; continue; }
            s.recalc();
            std::string got = s.display(5,5);
            bool ok = (got == c.want);
            if (ok) P++; else {
                N++;
                std::cout << "  FAIL: " << c.f << " = " << got << "，期望 " << c.want
                          << "   [" << c.why << "]\n";
            }
        }
    }

    std::cout << "== 格式化：前导零与浮点噪声 ==\n";
    txt("DEC2OCT(100)",   "144", "回归：未给 places 时不应补零（曾得 0000000144）");
    txt("DEC2OCT(100,6)", "000144", "给定 places 才补零");
    txt("BIN2OCT(1010)",  "12",  "同上");
    txt("HEX2OCT(\"A\")", "12",  "同上");
    txt("IMPOWER(\"1+i\",2)", "2i",  "回归：曾留下 1.2e-16 实部噪声");
    txt("IMDIV(\"1+i\",\"1-i\")","i","同上；且虚部系数 1 应省略");
    txt("IMSQRT(\"3+4i\")",   "2+i","同上");
    txt("IMSUM(\"1+2i\",\"3+4i\")","4+6i","虚部系数非 1 时正常写出");

    std::cout << "== 数组形状与标量新 x（第二轮审计）==\n";
    // TOROW 曾直接复用 oneCol()，产出的是一列 —— 与 TOCOL 完全相同，
    // 转置方向反了。用形状断言，不能只看数值。
    {
        struct C { const char* f; const char* want; const char* why; };
        std::vector<C> cs = {
            {"TOROW({1;2;3})",              "1x3 {1,2,3}",  "回归：曾返回 3行1列"},
            {"TOCOL({1,2,3})",              "3x1 {1}{2}{3}","对照"},
            {"TOROW({1,2,3})",              "1x3 {1,2,3}",  "行输入应保持 1 行"},
            {"TREND({2,4,6},{1,2,3},4)",    "1x1 {8}",      "回归：标量 new_x 曾被忽略"},
            {"GROWTH({2,4,8},{1,2,3},4)",   "1x1 {16}",     "同上"},
            {"TREND({2,4,6},{1,2,3},{5})",  "1x1 {10}",     "数组新 x（本来正确）"},
            {"TREND({2,4,6},{1,2,3})",      "1x3 {2,4,6}",  "缺省沿用 known_x"},
        };
        for (auto& c : cs) arr(c.f, c.want, c.why);
    }

    std::cout << "== 空参数占位（Excel 常见写法）==\n";
    // 解析器原先要求每个参数都是合法表达式，遇到 `,,` 直接整式失败。
    // 影响 SORT(A,,-1)、VLOOKUP(x,A:B,2,)、OFFSET(A1,1,,3,2) 等一批写法。
    {
        struct C { const char* f; const char* want; const char* why; };
        std::vector<C> cs = {
            {"SORT({3,1,2},,-1)",    "1x3 {3,2,1}", "回归：,, 曾导致解析失败"},
            {"SORT({3,1,2},,1)",     "1x3 {1,2,3}", "升序"},
            {"TOROW({1,2,3},,TRUE)", "1x3 {1,2,3}", "同上"},
        };
        for (auto& c : cs) arr(c.f, c.want, c.why);
    }

    std::cout << "== 数据库函数（DSUM 家族）==\n";
    // 注意：本引擎 setValue(列, 行)，不是 (row, col)。
    // 审计时我两次把坐标写反，得到"DSUM 全错"的假结论 ——
    // 这两个用例同时也把坐标语义钉住，防止以后再搞错。
    {
        Sheet s; s.setName("A");
        s.setValue(0,0,Value::str("品种"));   // A1
        s.setValue(1,0,Value::str("数量"));   // B1
        s.setValue(2,0,Value::str("单价"));   // C1
        const char* k[3] = {"苹果","香蕉","苹果"};
        for (int i=0;i<3;i++){
            s.setValue(0,1+i, Value::str(k[i]));        // A2..A4
            s.setValue(1,1+i, Value::num(10+i*10));     // B2..B4 = 10,20,30
            s.setValue(2,1+i, Value::num(2+i));         // C2..C4 = 2,3,4
        }
        s.setValue(4,0,Value::str("品种"));   // E1
        s.setValue(4,1,Value::str("苹果"));   // E2
        struct C { const char* f; const char* want; const char* why; };
        std::vector<C> cs = {
            {"DSUM(A1:C4,2,E1:E2)",      "40",  "苹果两行 10+30"},
            {"DAVERAGE(A1:C4,2,E1:E2)",  "20",  "(10+30)/2"},
            {"DMAX(A1:C4,2,E1:E2)",      "30",  "最大"},
            {"DMIN(A1:C4,2,E1:E2)",      "10",  "最小"},
            {"DPRODUCT(A1:C4,2,E1:E2)",  "300", "10*30"},
            {"DCOUNT(A1:C4,2,E1:E2)",    "2",   "计数"},
            {"DSUM(A1:C4,\"数量\",E1:E2)", "40", "字段名写法"},
        };
        for (auto& c : cs) {
            std::string e = s.setFormula(8,8,c.f);
            if (!e.empty()) { N++; std::cout << "  FAIL(解析) " << c.f << ": " << e << "\n"; continue; }
            s.recalc();
            std::string got = s.display(8,8);
            bool ok = (got == c.want);
            if (ok) P++; else {
                N++;
                std::cout << "  FAIL: " << c.f << " = " << got << "，期望 " << c.want
                          << "   [" << c.why << "]（注意 setValue 是 (列,行)）\n";
            }
        }
    }

    std::cout << "== 右尾反函数：尾概率方向（第三轮审计）==\n";
    // F.INV.RT 曾把 p 当成左尾概率（多减了一次 1-p），于是
    // F.INV.RT(0.05,5,5) 给出 0.198 而非 5.05 —— 两者恰为倒数，
    // 看起来"算出来了"，实际是另一侧。用统计表标准值 + 往返双重钉死。
    num("F.INV.RT(0.05,5,5)",   5.050329, 1e-5, "回归：曾返回 0.198（左尾分位数）");
    num("F.INV.RT(0.05,1,10)",  4.964603, 1e-5, "F(1,10) 5% 临界值");
    num("F.INV.RT(0.01,3,20)",  4.938193, 1e-5, "F(3,20) 1% 临界值");
    num("FINV(0.05,5,5)",       5.050329, 1e-5, "旧式别名应一致");
    // 往返：代回右尾分布必须还原
    num("F.DIST.RT(F.INV.RT(0.05,5,5),5,5)", 0.05, 1e-9, "往返自洽");
    num("F.DIST.RT(F.INV.RT(0.3,4,7),4,7)",  0.3,  1e-9, "往返自洽");

    std::cout << "== PERCENTRANK.EXC 与 PERCENTILE.EXC 必须互逆 ==\n";
    // EXC 版把区间分成 n+1 段：第 k 小的值对应 k/(n+1)。
    // 原实现分子少加 1，整体偏小 1/(n+1)，且与 PERCENTILE.EXC 不再互逆。
    num("PERCENTRANK.EXC({1,2,3,4},1)", 0.2, 1e-9, "回归：曾为 0；应等于 1/5");
    num("PERCENTRANK.EXC({1,2,3,4},2)", 0.4, 1e-9, "2/5");
    num("PERCENTRANK.EXC({1,2,3,4},3)", 0.6, 1e-9, "3/5");
    num("PERCENTRANK.EXC({1,2,3,4},4)", 0.8, 1e-9, "4/5");
    // 互逆：把 PERCENTRANK.EXC 的结果喂回 PERCENTILE.EXC，应还原原值
    num("PERCENTILE.EXC({1,2,3,4},PERCENTRANK.EXC({1,2,3,4},3))", 3.0, 1e-9, "往返自洽");
    num("PERCENTILE.EXC({1,2,3,4},PERCENTRANK.EXC({1,2,3,4},1))", 1.0, 1e-9, "往返自洽");
    // INC 版分母是 n-1，作为对照
    num("PERCENTRANK.INC({1,2,3,4},1)", 0.0, 1e-9, "对照：0/3");
    num("PERCENTRANK.INC({1,2,3,4},4)", 1.0, 1e-9, "对照：3/3");

    std::cout << "== 复数输出格式一致性 ==\n";
    // IMTAN 曾内联了一套自己的格式化，虚部为 0 时输出 "0+0i"，
    // 而其他复数函数是 "0"。改为复用共享的 cxStr 后一致。
    txt("IMTAN(\"0\")", "0", "回归：曾输出 0+0i");
    txt("IMTAN(\"1\")", "1.5574077246549", "虚部为 0 时不带 +0i");
    txt("IMSIN(\"0\")", "0", "对照");
    txt("IMCOS(\"0\")", "1", "对照");

    std::cout << "== 补充覆盖（这些审计时我写错过期望，引擎是对的）==\n";
    num("DATEVALUE(\"2024-03-15\")", 45366.0, 1e-9, "我曾误算为 45367");
    num("EXP(1)",  2.71828182845905, 1e-9, "我曾按截断值写期望");
    num("RANK.AVG(3,{1,2,3,4})", 2.0, 1e-9, "默认降序，我曾以为升序");
    num("VAR.P({1,2,3})",      0.666667, 1e-5, "总体方差 2/3");
    num("COVARIANCE.P({1,2,3},{2,4,6})", 1.333333, 1e-5, "4/3，不是样本协方差");
    num("PEARSON({1,2,3},{2,4,6})",      1.0, 1e-9, "完全线性相关");
    num("DDB(10000,1000,5,1)", 4000.0, 1e-9, "10000×2/5");
    num("BINOMDIST(3,10,0.5,FALSE)", 0.117188, 1e-6, "120/1024");
    num("BINOMDIST(3,10,0.5,TRUE)",  0.171875, 1e-6, "176/1024");
    num("EXPONDIST(1,1,TRUE)",  0.632121, 1e-6, "1-e^-1");
    num("EXPONDIST(1,1,FALSE)", 0.367879, 1e-6, "e^-1");
    num("ERF(1)",  0.842701, 1e-6, "erf(1) 标准值");
    num("ERFC(0)", 1.0, 1e-9, "erfc(0)");
    num("BESSELI(0,0)", 1.0,      1e-9, "I₀(0)=1");
    num("BESSELK(1,0)", 0.421024, 1e-6, "K₀(1) 标准值");
    num("BESSELY(1,0)", 0.088257, 1e-6, "Y₀(1) 标准值");
    num("IMARGUMENT(\"1+i\")", 0.785398, 1e-6, "arg(1+i)=π/4");
    num("DOLLARFR(1.25,8)", 1.2,      1e-9, "1+2/8");
    num("PDURATION(0.05,100,200)", 14.206699, 1e-5, "ln2/ln1.05");
    num("DATEDIF(DATE(2020,1,1),DATE(2024,1,1),\"Y\")", 4.0, 1e-9, "整年数");
    num("EOMONTH(DATE(2024,2,1),0)", 45351.0, 1e-9, "2024-02-29");
    txt("OCT2BIN(\"7\")",  "111",  "7→111₂");
    txt("OCT2HEX(\"17\")", "F",    "15→F₁₆");
    txt("HEX2BIN(\"F\")",  "1111", "15→1111₂");

    std::cout << "== 日期与时间（覆盖补充）==\n";
    num("YEAR(DATE(2024,3,15))",  2024.0, 1e-9, "年");
    num("MONTH(DATE(2024,3,15))",    3.0, 1e-9, "月");
    num("DAY(DATE(2024,3,15))",     15.0, 1e-9, "日");
    num("HOUR(TIME(14,30,25))",     14.0, 1e-9, "时");
    num("MINUTE(TIME(14,30,25))",   30.0, 1e-9, "分");
    num("SECOND(TIME(14,30,25))",   25.0, 1e-9, "秒");
    num("DAYS(DATE(2024,1,10),DATE(2024,1,1))", 9.0, 1e-9, "相差天数");
    num("WEEKNUM(DATE(2024,1,1))",   1.0, 1e-9, "周日起始");
    num("DATEDIF(DATE(2024,1,1),DATE(2024,3,1),\"D\")", 60.0, 1e-9, "2024 闰年 31+29");

    std::cout << "== 数学与双曲（覆盖补充）==\n";
    num("SUMSQ(3,4)",          25.0, 1e-9, "9+16");
    num("SIGN(-5)",            -1.0, 1e-9, "负数");
    num("SIGN(0)",              0.0, 1e-9, "零");
    num("POWER(2,10)",       1024.0, 1e-9, "2^10");
    num("COMBINA(3,2)",         6.0, 1e-9, "C(4,2) 可重复组合");
    num("CEILING.MATH(2.5)",    3.0, 1e-9, "向上");
    num("CEILING.MATH(-2.5)",  -2.0, 1e-9, "负号默认向零收拢");
    num("FLOOR.MATH(2.5)",      2.0, 1e-9, "向下");
    num("FLOOR.MATH(-2.5)",    -3.0, 1e-9, "负号默认背离零");
    num("SUMX2MY2({1,2},{3,4})", -20.0, 1e-9, "(1-9)+(4-16)");
    num("SINH(0)", 0.0, 1e-9, "双曲正弦");
    num("COSH(0)", 1.0, 1e-9, "双曲余弦");
    num("TANH(0)", 0.0, 1e-9, "双曲正切");
    num("ASIN(0)", 0.0, 1e-9, "反正弦");
    num("ACOS(1)", 0.0, 1e-9, "反余弦");
    num("ASINH(0)",0.0, 1e-9, "反双曲正弦");
    num("ACOSH(1)",0.0, 1e-9, "反双曲余弦");
    num("ATANH(0)",0.0, 1e-9, "反双曲正切");

    std::cout << "== 分布：单侧与双侧 ==\n";
    num("T.DIST.RT(2,10)",  0.036694, 1e-6, "右尾");
    num("T.DIST.2T(2,10)",  0.073388, 1e-6, "双尾应恰为单尾 2 倍");
    num("TDIST(2,10,1)",    0.036694, 1e-6, "旧式单尾");
    num("TDIST(2,10,2)",    0.073388, 1e-6, "旧式双尾");
    num("FDIST(1,5,5)",     0.5,      1e-9, "旧式应等于 F.DIST.RT");
    num("NEGBINOM.DIST(3,2,0.5,FALSE)", 0.125, 1e-9, "C(4,3)×0.5⁵=4/32");
    num("CONFIDENCE.T(0.05,1,20)", 0.468014, 1e-6, "t 半宽");
    num("CHITEST({1,2},{1,2})",    1.0, 1e-9, "完全相同应为 1");

    std::cout << "== 复数与进制（覆盖补充）==\n";
    txt("IMEXP(\"0\")",   "1", "e^0");
    txt("IMLN(\"1\")",    "0", "ln 1");
    txt("IMLOG10(\"1\")", "0", "log10 1");
    txt("IMLOG2(\"1\")",  "0", "log2 1");
    txt("IMSIN(\"0\")",   "0", "sin 0");
    txt("IMCOS(\"0\")",   "1", "cos 0");
    txt("BIN2OCT(OCT2BIN(\"7\"))",  "7",  "往返自洽");
    txt("HEX2OCT(OCT2HEX(\"17\"))", "17", "往返自洽");
    num("STDEVA({1,2,3})", 1.0, 1e-9, "纯数值时与 STDEV 一致");
    num("VARA({1,2,3})",   1.0, 1e-9, "纯数值时与 VAR 一致");

    std::cout << "== 财务：累计本金+利息应互补 ==\n";
    // 100000 贷款，月利率 0.5%，360 期。月供 ≈599.55。
    // 第 1 年 12 期：利息+本金 = 12×月供 ≈7194.6
    num("CUMIPMT(0.06/12,360,100000,1,12,0) + CUMPRINC(0.06/12,360,100000,1,12,0)",
        -7194.6, 1.0, "两者之和应等于 12 期还款总额");

    std::cout << "== 右尾反函数：尾概率方向（第三轮）==\n";
    num("F.INV.RT(0.05,5,5)",   5.050329, 1e-5, "回归：曾返回 0.198（左尾分位数）");
    num("F.INV.RT(0.05,1,10)",  4.964603, 1e-5, "F(1,10) 5% 临界值");
    num("F.INV.RT(0.01,3,20)",  4.938193, 1e-5, "F(3,20) 1% 临界值");
    num("FINV(0.05,5,5)",       5.050329, 1e-5, "旧式别名应一致");
    num("F.DIST.RT(F.INV.RT(0.05,5,5),5,5)", 0.05, 1e-9, "往返自洽");
    num("F.DIST.RT(F.INV.RT(0.3,4,7),4,7)",  0.3,  1e-9, "往返自洽");

    std::cout << "== PERCENTRANK.EXC 与 PERCENTILE.EXC 必须互逆 ==\n";
    num("PERCENTRANK.EXC({1,2,3,4},1)", 0.2, 1e-9, "回归：曾为 0；应 1/5");
    num("PERCENTRANK.EXC({1,2,3,4},2)", 0.4, 1e-9, "2/5");
    num("PERCENTRANK.EXC({1,2,3,4},3)", 0.6, 1e-9, "3/5");
    num("PERCENTRANK.EXC({1,2,3,4},4)", 0.8, 1e-9, "4/5");
    num("PERCENTILE.EXC({1,2,3,4},PERCENTRANK.EXC({1,2,3,4},3))", 3.0, 1e-9, "往返自洽");
    num("PERCENTILE.EXC({1,2,3,4},PERCENTRANK.EXC({1,2,3,4},1))", 1.0, 1e-9, "往返自洽");
    num("PERCENTRANK.INC({1,2,3,4},1)", 0.0, 1e-9, "对照：0/3");
    num("PERCENTRANK.INC({1,2,3,4},4)", 1.0, 1e-9, "对照：3/3");

    std::cout << "== 复数输出格式一致性 ==\n";
    txt("IMTAN(\"0\")", "0", "回归：曾输出 0+0i");
    txt("IMTAN(\"1\")", "1.5574077246549", "虚部为 0 时不带 +0i");
    txt("IMTANH(\"1\")", "0.761594155955765", "tanh(1) 标准值");
    txt("IMCSCH(\"1\")", "0.850918128239322", "csch(1)=1/sinh(1)");

    std::cout << "== COUP 家族：付息期必须以到期日为锚点（第四轮）==\n";
    // 三条独立路径算同一个付息期长度必须一致。
    // 原先所有函数按"结算日所在年份的固定月份档"锚定（freq=2 → 1月/7月），
    // 互相自洽但整体错误；更早期 COUPDAYS 甚至完全忽略 st/mt，返回 365/freq。
    num("COUPDAYS(DATE(2023,5,15),DATE(2030,11,15),2,1)", 184.0, 1e-9, "回归：曾为 181");
    num("COUPDAYS(DATE(2024,3,1),DATE(2025,1,1),1,1)", 366.0, 1e-9, "年付，闰年");
    num("COUPDAYS(DATE(2024,3,1),DATE(2025,1,1),4,1)",  91.0, 1e-9, "季付 Q1");
    num("COUPDAYS(DATE(2024,3,1),DATE(2025,1,1),2,1)", 182.0, 1e-9, "半年付");
    num("COUPDAYS(DATE(2024,9,1),DATE(2025,1,1),2,1)", 184.0, 1e-9, "下半年 7/1~1/1");
    num("COUPDAYS(DATE(2024,3,1),DATE(2025,1,1),2,0)", 180.0, 1e-9, "30/360: 360/2");
    num("COUPDAYS(DATE(2024,3,1),DATE(2025,1,1),4,0)",  90.0, 1e-9, "30/360: 360/4");
    num("COUPDAYBS(DATE(2024,1,1),DATE(2025,1,1),2,1)", 0.0, 1e-9, "结算日恰在付息日");
    num("COUPDAYBS(DATE(2024,3,1),DATE(2025,1,1),2,1)+"
        "COUPDAYSNC(DATE(2024,3,1),DATE(2025,1,1),2,1)", 182.0, 1e-9, "路径B");
    num("COUPNCD(DATE(2024,3,1),DATE(2025,1,1),2,1)-"
        "COUPPCD(DATE(2024,3,1),DATE(2025,1,1),2,1)", 182.0, 1e-9, "路径C");

    std::cout << "== DISC 曾完全忽略 basis（第五轮）==\n";
    num("DISC(DATE(2024,1,1),DATE(2024,7,1),97,100,1)", 0.0603297, 1e-6, "回归：曾为 0.06");
    num("PRICEDISC(DATE(2024,1,1),DATE(2024,7,1),"
        "DISC(DATE(2024,1,1),DATE(2024,7,1),97,100,1),100,1)", 97.0, 1e-9,
        "往返必须还原成 97（0.06 会还原成 97.0164）");
    num("DISC(DATE(2024,1,1),DATE(2024,7,1),97,100,0)", 0.06, 1e-9, "basis=0 → 30/360");
    num("INTRATE(DATE(2024,1,1),DATE(2024,7,1),97,100,1)", 0.0621955, 1e-6, "同族对照");
    num("RECEIVED(DATE(2024,1,1),DATE(2024,7,1),97,0.06,1)", 99.983102, 1e-6, "同族对照");

    std::cout << "== FREQUENCY 必须是列向量 ==\n";
    arr("FREQUENCY({1,2,3,4,5},{2,4})", "3x1 {2}{2}{1}", "回归：曾为行向量 1x3");

    std::cout << "== 我又写错过的期望（引擎是对的，已按 Excel 修正）==\n";
    num("DATEVALUE(\"2024-03-15\")", 45366.0, 1e-9, "我曾算成 45367");
    num("EXP(1)",  2.71828182845905, 1e-9, "我曾按截断值写期望");
    num("RANK.AVG(3,{1,2,3,4})", 2.0, 1e-9, "Excel 默认降序，我以为升序");
    num("QUARTILE.EXC({1,2,3,4,5,6,7,8},1)", 2.25, 1e-9, "我曾算成 2.75");
    num("QUARTILE.EXC({1,2,3,4,5,6,7,8},3)", 6.75, 1e-9, "我曾算成 6.25");
    txt("QUARTILE.EXC({1,2,3,4,5,6,7,8},0)", "#NUM!", "EXC 版 quart 只接受 1..3");
    txt("QUARTILE.EXC({1,2,3,4,5,6,7,8},4)", "#NUM!", "同上");
    num("TIMEVALUE(\"14:30:25\")", 0.604456018518518, 1e-12, "52225/86400 手算");
    num("TBILLEQ(DATE(2024,1,1),DATE(2024,7,1),0.05)", 0.052009, 1e-6,
        "365×0.05/(360-0.05×182)；我曾传价格 98 得负数");
    num("TBILLYIELD(DATE(2024,1,1),DATE(2024,7,1),98)", 0.040368, 1e-6, "(100-98)/98×360/182");
    num("DB(10000,1000,5,1)", 3690.0, 1.0, "10000×(1-0.1^0.2)");
    arr("WRAPCOLS({1,2,3,4,5,6},3)", "3x2 {1,4}{2,5}{3,6}", "第2参是每列几行，我曾写反");
    arr("WRAPROWS({1,2,3,4,5,6},3)", "2x3 {1,2,3}{4,5,6}", "对照");

    std::cout << "== 补充覆盖 ==\n";
    txt("ROW()","6","本文件辅助写在索引(5,5) → 1起算第6行");
    txt("COLUMN()","6","同上 → F 列 = 6");
    txt("SHEET()","1","第1张表"); txt("SHEETS()","1","共1张表");
    txt("LOWER(\"ABCdef\")","abcdef","转小写");
    txt("CONCATENATE(\"a\",\"b\",\"c\")","abc","拼接");
    txt("ISLOGICAL(TRUE)","TRUE","逻辑值"); txt("ISLOGICAL(1)","FALSE","数字不是");
    txt("HYPERLINK(\"http://x.com\",\"点我\")","点我","显示文本");
    txt("PHONETIC(\"abc\")","abc","日文专用");
    num("VARP({1,2,3})",   0.666667, 1e-5, "总体方差 2/3");
    num("RSQ({1,2,3},{2,4,6})",   1.0, 1e-9, "完全线性");
    num("COVAR({1,2,3},{2,4,6})", 1.333333, 1e-5, "4/3");
    num("ZTEST({1,2,3,4,5},3,1)", 0.5, 1e-9, "样本均值=假设均值 → 0.5");
    num("Z.TEST({1,2,3,4,5},3,1)", 0.5, 1e-9, "新式别名一致");
    // LOGEST 返回数组，num() 只认标量 → 改用 txt 取首元素
    txt("LOGEST({2,4,8},{1,2,3})", "2", "指数拟合底数（数组取首元素）");
    num("GAMMA.INV(GAMMADIST(2,2,1,TRUE),2,1)",  2.0, 1e-9, "往返");
    num("BETA.INV(BETA.DIST(0.3,2,3,TRUE),2,3)", 0.3, 1e-9, "往返");
    num("LOGNORMDIST(1,0,1)", 0.5, 1e-9, "ln1=0 → Φ(0)");
    num("T.DIST.RT(2,10)",  0.036694, 1e-6, "右尾");
    num("T.DIST.2T(2,10)",  0.073388, 1e-6, "双尾恰为单尾 2 倍");
    num("TDIST(2,10,1)",    0.036694, 1e-6, "旧式单尾");
    num("TDIST(2,10,2)",    0.073388, 1e-6, "旧式双尾");
    num("CHITEST({1,2},{1,2})", 1.0, 1e-9, "完全相同应为 1");
    num("ERF.PRECISE(0)",  0.0, 1e-9, "等于 ERF(0)");
    num("ERF.PRECISE(1)",  0.842701, 1e-6, "等于 ERF(1)");
    num("ERFC.PRECISE(0)", 1.0, 1e-9, "等于 ERFC(0)");
    // XIRR 的定义就是让 XNPV=0 的利率 —— 代回去必须≈0
    num("XNPV(XIRR({-1000,500,600},{40000,40100,40500}),"
        "{-1000,500,600},{40000,40100,40500})", 0.0, 1e-6, "XIRR↔XNPV 往返");
    // BAHTTEXT 是占位实现，只锁定"不崩且返回文本"，不假装它正确
    txt("BAHTTEXT(100)", "100.00 Baht", "占位实现，非泰文（已知边界）");
    // 数据库函数：必须给 (数据库, 字段, 条件) 三参
    // !! 坐标陷阱 !!  setValue 的签名是 setValue(**列**, **行**)，与直觉相反。
    {
        Sheet s; s.setName("A");
        s.setValue(0,0,Value::str("v"));                                  // A1 字段名
        for (int r = 1; r <= 4; r++) s.setValue(0,r,Value::num((double)r)); // A2:A5 = 1..4
        s.setValue(4,0,Value::str("v"));                                   // E1 条件字段名
        // E2 留空 = 无条件，全部记录入选
        std::string e1 = s.setFormula(6,0,"DSTDEV(A1:A5,1,E1:E2)");
        std::string e2 = s.setFormula(7,0,"DVAR(A1:A5,1,E1:E2)");
        if (!e1.empty() || !e2.empty()) { N++; std::cout << "  FAIL(解析) DSTDEV/DVAR\n"; }
        else { s.recalc();
               double sd = s.valueAt(6,0).n, vr = s.valueAt(7,0).n;
               if (std::fabs(sd-1.290994)<1e-5 && std::fabs(vr-1.666667)<1e-5) P++;
               else { N++; std::cout << "  FAIL: DSTDEV=" << sd << " DVAR=" << vr << "\n"; } }
    }
    // 随机与时间：只验范围，不验具体值
    {
        Sheet s; s.setName("A");
        bool ok = true;
        for (int i = 0; i < 200; i++) {
            s.setFormula(8,8,"RAND()"); s.recalc();
            double v = s.valueAt(8,8).n;
            if (!(v >= 0.0 && v < 1.0)) { ok = false; break; }
        }
        if (ok) P++; else { N++; std::cout << "  FAIL: RAND() 越界\n"; }
        s.setFormula(8,8,"TODAY()"); s.recalc();
        double t = s.valueAt(8,8).n;
        s.setFormula(8,8,"NOW()");  s.recalc();
        double nw = s.valueAt(8,8).n;
        if (t >= 45000.0 && t < 60000.0 && nw >= t && nw < t + 1.0) P++;
        else { N++; std::cout << "  FAIL: TODAY/NOW = " << t << "," << nw << "\n"; }
    }

    std::cout << "== 收尾：剩余未提及函数全部纳入 ==\n";
    // COUNTBLANK：公式返回 "" 的单元格也计为空白
    {
        Sheet s; s.setName("A");
        s.setValue(0,0,Value::num(5));    // A1 有值
        s.setValue(2,0,Value::str(""));   // C1 空串 → 也算空白（B1 真空）
        std::string e = s.setFormula(6,0,"COUNTBLANK(A1:C1)");
        if (!e.empty()) { N++; std::cout << "  FAIL(解析) COUNTBLANK\n"; }
        else { s.recalc(); std::string g = s.display(6,0);
               if (g == "2") P++; else { N++; std::cout << "  FAIL: COUNTBLANK=" << g << "\n"; } }
    }
    num("LARGE({3,1,5,2},1)", 5.0, 1e-9, "最大");
    num("LARGE({3,1,5,2},4)", 1.0, 1e-9, "第4大=最小");
    num("SMALL({3,1,5,2},1)", 1.0, 1e-9, "最小");
    num("SMALL({3,1,5,2},4)", 5.0, 1e-9, "第4小=最大");
    num("AVERAGEIF({1,2,3,4},\">2\")", 3.5, 1e-9, "(3+4)/2");
    num("COUNTIFS({1,2,3,4},\">2\")",  2.0, 1e-9, "3和4");
    num("SUMIFS({1,2,3,4},{1,2,3,4},\">2\")",    7.0, 1e-9, "3+4");
    num("AVERAGEIFS({1,2,3,4},{1,2,3,4},\">2\")", 3.5, 1e-9, "(3+4)/2");
    num("COUNTIFS({1,2,3,4},\">1\",{1,2,3,4},\"<4\")", 2.0, 1e-9, "多条件");
    num("NORMSDIST(1.96)", 0.975002, 1e-6, "Φ(1.96)");
    num("NORMSINV(0.975)", 1.959964, 1e-6, "往返");
    num("NORMDIST(0,0,1,TRUE)", 0.5, 1e-9, "与 NORMSDIST 一致");
    num("NORMINV(0.5,0,1)",     0.0, 1e-9, "与 NORMSINV 一致");
    arr("EXPAND({1,2},3,3)", "3x3 {1,2,#N/A}{#N/A,#N/A,#N/A}{#N/A,#N/A,#N/A}", "扩位填 #N/A");
    arr("SEQUENCE(2,2)", "2x2 {1,2}{3,4}", "对照");
    txt("IMSEC(\"0\")", "1", "sec(0)=1");
    // 假设检验
    {
        Sheet s; s.setName("A");
        s.setFormula(8,8,"TTEST({1,2,3},{10,11,12},2,3)"); s.recalc();
        double t = s.valueAt(8,8).n;
        if (t >= 0.0 && t < 0.05) P++; else { N++; std::cout << "  FAIL: TTEST=" << t << "\n"; }
        s.setFormula(8,8,"FTEST({1,2,3,4},{5,6,7,8})"); s.recalc();
        double f = s.valueAt(8,8).n;
        if (std::fabs(f - 1.0) < 0.05) P++; else { N++; std::cout << "  FAIL: FTEST=" << f << "\n"; }
    }
    // RANDARRAY / RANDBETWEEN：形状与范围
    {
        Sheet s; s.setName("A");
        s.setFormula(8,8,"RANDARRAY(3,4)"); s.recalc();
        Value v = s.valueAt(8,8);
        bool ok = v.isArray() && v.arr && (*v.arr).size() == 3 && (*v.arr)[0].size() == 4;
        if (ok) P++; else { N++; std::cout << "  FAIL: RANDARRAY(3,4) 形状\n"; }
        bool ok2 = true;
        for (int i = 0; i < 200; i++) {
            s.setFormula(8,8,"RANDBETWEEN(1,10)"); s.recalc();
            double x = s.valueAt(8,8).n;
            if (!(x >= 1.0 && x <= 10.0)) { ok2 = false; break; }
        }
        if (ok2) P++; else { N++; std::cout << "  FAIL: RANDBETWEEN 越界\n"; }
    }
    // 财务：只锁定可自洽或量级正确的部分，不假装已与 Excel 逐项核对
    num("YIELDDISC(DATE(2024,1,1),DATE(2024,7,1),97,100,1)", 0.0621955, 1e-6, "与 INTRATE 同口径");
    num("ACCRINTM(DATE(2020,1,1),DATE(2024,1,1),0.05,1000,1)", 200.0, 1.0,
        "量级正确；basis=1 年长口径未与 Excel 逐项核对");
    num("ACCRINT(DATE(2020,1,1),DATE(2020,7,1),DATE(2024,1,1),0.05,1000,2,1)", 200.0, 1.0, "同上");
    {
        Sheet s; s.setName("A");
        // YIELDMAT 与 PRICEMAT 互为反函数
        s.setFormula(8,8,"PRICEMAT(DATE(2024,1,1),DATE(2024,7,1),DATE(2024,1,1),0.05,"
                         "YIELDMAT(DATE(2024,1,1),DATE(2024,7,1),DATE(2024,1,1),0.05,98,1),1)");
        s.recalc();
        double p = s.valueAt(8,8).n;
        if (std::fabs(p - 98.0) < 0.5) P++; else { N++; std::cout << "  FAIL: PRICEMAT(YIELDMAT(98))=" << p << "\n"; }
        // 法式折旧：只锁定正数、不崩
        s.setFormula(8,8,"AMORLINC(1000,DATE(2020,1,1),DATE(2024,1,1),300,1,0.2)"); s.recalc();
        double a = s.valueAt(8,8).n;
        s.setFormula(8,8,"AMORDEGRC(1000,DATE(2020,1,1),DATE(2024,1,1),300,1,0.2)"); s.recalc();
        double b = s.valueAt(8,8).n;
        if (a > 0 && b > 0) P++; else { N++; std::cout << "  FAIL: AMORLINC/AMORDEGRC=" << a << "," << b << "\n"; }
    }

    std::cout << "== LINEST 多变量系数顺序（SciPy 交叉验证抓到）==\n";
    // Excel 的系数是**逆序**：最后一個自变量的系数排最前，截距永远在最后。
    // 原实现用正序，单变量时看不出差别，多变量时前后颠倒。
    // 用例：y = 1·x1 + 2·x2 + 3，known_x's 两列分别为 x1、x2
    num("INDEX(LINEST({8,7,14,13,18},{1,2;2,1;3,4;4,3;5,5}),1,1)", 2.0, 1e-6,
        "回归：曾为 1（顺序颠倒）→ 应为 x2 的系数");
    num("INDEX(LINEST({8,7,14,13,18},{1,2;2,1;3,4;4,3;5,5}),1,2)", 1.0, 1e-6,
        "x1 的系数");
    num("INDEX(LINEST({8,7,14,13,18},{1,2;2,1;3,4;4,3;5,5}),1,3)", 3.0, 1e-6,
        "截距永远在最后");
    // 单变量不受影响，作为对照
    num("INDEX(LINEST({3,5,7},{1,2,3}),1,1)", 2.0, 1e-9, "单变量斜率（对照）");
    num("INDEX(LINEST({3,5,7},{1,2,3}),1,2)", 1.0, 1e-9, "单变量截距（对照）");

    std::cout << "== 中文文本函数：必须按字符而不是字节（LibreOffice 对照抓到）==\n";
    // LEN/LEFT/RIGHT/MID/REPLACE 原先直接对 std::string 按字节操作：
    //   LEN("中文abc")            = 9         ← 3+3+3 字节
    //   LEFT("中文abc",2)         = "\\xe4\\xb8"  ← 汉字被切成半个，乱码
    //   REPLACE("中文abc",1,2,"X") = "X\\xb8文abc"
    // Excel 一律按**字符**计数。已抽到 src/utf8.hpp 统一处理。
    num("LEN(\"中文abc\")",  5.0, 1e-9, "回归：曾为 9（字节数）");
    num("LEN(\"你好世界\")",  4.0, 1e-9, "纯中文");
    num("LEN(\"abc\")",      3.0, 1e-9, "纯 ASCII 不变");
    num("LEN(\"\")",         0.0, 1e-9, "空串");
    txt("LEFT(\"中文abc\",2)",    "中文", "回归：曾输出半个汉字（乱码）");
    txt("LEFT(\"中文abc\",3)",    "中文a", "跨中英边界");
    txt("RIGHT(\"中文abc\",2)",   "bc",   "从尾部取");
    txt("RIGHT(\"中文abc\",3)",   "abc",  "");
    txt("MID(\"中文abc\",2,2)",   "文a",  "回归：曾输出孤立字节");
    txt("MID(\"中文abc\",1,2)",   "中文", "从头取两个汉字");
    txt("REPLACE(\"中文abc\",1,2,\"X\")", "Xabc", "回归：曾为 X\\xb8文abc");
    txt("LEFT(\"abc\",2)",       "ab",   "ASCII 对照");

    std::cout << "== ^ 是左结合，但一元负号优先于 ^（LibreOffice 对照抓到）==\n";
    // 两条规则必须同时成立，这是 Excel 唯一与常规数学约定相反的结合性。
    // 原先按右结合递归 parsePow()，11 条实测里错 4 条。
    num("2^3^2",    64.0,     1e-9, "回归：曾为 512（右结合）→ 应为 (2^3)^2");
    num("2^2^3",    64.0,     1e-9, "回归：曾为 256 → (2^2)^3");
    num("-2^2",      4.0,     1e-9, "一元负号优先于 ^ → (-2)^2");
    num("2^-3",      0.125,   1e-9, "右侧允许一元");
    num("2^-3^2",    0.015625,1e-9, "回归：曾为 512 → (2^(-3))^2");
    num("-2^3^2",   64.0,     1e-9, "回归：曾为 -512 → ((-2)^3)^2");
    num("2^3^-2",    0.015625,1e-9, "回归：曾为 1.08 → (2^3)^(-2)");
    num("-(2^2)",   -4.0,     1e-9, "括号改变优先级");
    num("2^(3^2)", 512.0,     1e-9, "显式括号保留右结合");
    num("-2^-2",     0.25,    1e-9, "");
    num("4^0.5",     2.0,     1e-9, "");

    std::cout << "-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}

// 数组形状断言：只比数值看不出 TOROW 方向反了，必须连形状一起比。
static void arrImpl(const std::string& f, const std::string& want, const std::string& why){
    Sheet s; s.setName("A");
    std::string e = s.setFormula(8,8,f);
    if (!e.empty()) { N++; std::cout << "  FAIL(解析) " << f << ": " << e << "\n"; return; }
    s.recalc();
    Value v = s.valueAt(8,8);
    std::string got;
    if (v.isArray() && v.arr) {
        got = std::to_string((*v.arr).size()) + "x"
            + std::to_string((*v.arr).empty() ? 0 : (*v.arr)[0].size()) + " ";
        for (auto& row : *v.arr) {
            got += "{";
            for (size_t i=0;i<row.size();i++){
                if (i) got += ",";
                if (row[i].isNum()) got += std::to_string((long long)(row[i].n + 1e-9));
                else if (row[i].isEmpty()) got += "空";
                else if (row[i].isError()) got += errText(row[i].e);
                else got += "?";
            }
            got += "}";
        }
    } else if (v.isNum()) {
        char b[64]; std::snprintf(b, sizeof b, "%.6g", v.n); got = b;
    } else got = s.display(8,8);
    if (got == want) P++; else {
        N++;
        std::cout << "  FAIL: " << f << " = " << got << "，期望 " << want << "   [" << why << "]\n";
    }
}
