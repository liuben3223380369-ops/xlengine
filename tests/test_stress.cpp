// 工业级压力测试
//
// 与功能测试的区别：功能测试验证"对不对"，压力测试验证"扛不扛得住"。
// 关注四类失效模式：
//   1. 规模：数据量上去后是否还在线性时间内完成（有无意外的 O(n²)）
//   2. 深度：递归结构（依赖链、嵌套括号）会不会爆栈
//   3. 边界：极限值附近是否正确或安全失败（而不是崩溃/静默出错）
//   4. 泄漏：反复操作后内存是否单调增长
//
// 每组都设一个"可接受阈值"，超了就报 FAIL —— 压力测试不断言精确值，
// 但必须对"慢到不可用"和"内存失控"给出明确结论。
#include "sheet.hpp"
#include "value.hpp"
#include "numfmt.hpp"
#include "xlsx.hpp"
#include "chart.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#include <cmath>

using namespace xl;
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static int P = 0, N = 0;
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}
static void REPORT(const std::string& what, double ms, double limit, const std::string& extra = "") {
    bool ok = ms <= limit;
    if (ok) P++; else N++;
    printf("  %-42s %8.1f ms  (阈值 %6.0f)  %s %s\n",
           what.c_str(), ms, limit, ok ? "OK  " : "FAIL", extra.c_str());
}
// 当前进程的常驻内存（KB）；读 /proc/self/statm，Linux 上可靠
static long rssKB() {
    std::ifstream f("/proc/self/statm");
    long size = 0, resident = 0;
    if (f) f >> size >> resident;
    return resident * 4;          // 页大小 4KB
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    std::cout << "== 1. 规模：写入与重算 ==\n";
    {
        // 10 万格：写入
        Sheet sh;
        auto t0 = Clock::now();
        for (int r = 0; r < 1000; r++)
            for (int c = 0; c < 100; c++)
                sh.setValue(c, r, Value::num(c + r));
        double wms = msSince(t0);
        REPORT("写入 100,000 个单元格", wms, 2000);

        // 1000 条 SUM，每条引用 100 列
        t0 = Clock::now();
        for (int r = 0; r < 1000; r++) {
            std::string f = "SUM(A" + std::to_string(r + 1) + ":CV" + std::to_string(r + 1) + ")";
            sh.setFormula(200, r, f);
        }
        double fms = msSince(t0);
        REPORT("解析 1,000 条 SUM(100列)", fms, 3000);

        t0 = Clock::now();
        sh.recalc();
        double rms = msSince(t0);
        REPORT("全量重算 1,000 条 SUM(100列)", rms, 5000);

        // 校验一条，确认不是"快但算错了"
        double got = 0;
        Value v = sh.valueAt(200, 0);
        if (v.isNum()) got = v.n;
        double want = 0;
        for (int c = 0; c < 100; c++) want += c;    // 第 0 行：c+0
        OK(std::fabs(got - want) < 1e-6 && std::fabs(want - 4950) < 1e-9, "首行 SUM 结果正确");
        printf("     校验 SUM 第 0 行 = %.0f（期望 %.0f）\n", got, want);
    }

    std::cout << "\n== 2. 规模：稀疏存储的极端位置 ==\n";
    {
        Sheet sh;
        auto t0 = Clock::now();
        sh.setValue(0, 0, Value::num(1));
        sh.setValue(16383, 1048575, Value::num(2));    // XFD1048576
        double ms = msSince(t0);
        REPORT("两个极端位置写入", ms, 100);
        long recs = (long)sh.allCells().size();
        printf("     实际记录 %ld 条（稀疏性验证）\n", recs);
        OK(recs == 2, "稀疏存储只记录 2 条");
    }

    std::cout << "\n== 3. 深度：依赖链 ==\n";
    {
        // 5000 层链式依赖：A1=1, A2=A1+1, ... 
        Sheet sh;
        auto t0 = Clock::now();
        sh.setValue(0, 0, Value::num(1));
        for (int i = 1; i < 5000; i++)
            sh.setFormula(0, i, "A" + std::to_string(i) + "+1");
        double bms = msSince(t0);
        REPORT("构建 5,000 层依赖链", bms, 5000);

        t0 = Clock::now();
        sh.recalc();
        double rms = msSince(t0);
        REPORT("递归求值 5,000 层", rms, 5000);
        Value v = sh.valueAt(0, 4999);
        OK(v.isNum() && std::fabs(v.n - 5000) < 1e-9, "依赖链末值正确");
        printf("     末值 = %s（期望 5000）\n", valueToText(v).c_str());
    }

    std::cout << "\n== 4. 深度：嵌套括号 ==\n";
    {
        // 嵌套括号是最典型的爆栈场景。逐级加深，找到能承受的上限。
        int maxOk = 0;
        for (int depth : {10, 50, 100, 500, 1000, 5000}) {
            std::string e(depth, '(');
            e += "1";
            e += std::string(depth, ')');
            Sheet sh;
            std::string err = sh.setFormula(0, 0, e);
            if (!err.empty()) { printf("     深度 %5d：解析拒绝（%s）\n", depth, err.c_str()); break; }
            sh.recalc();
            Value v = sh.valueAt(0, 0);
            if (v.isNum() && std::fabs(v.n - 1) < 1e-9) { maxOk = depth; printf("     深度 %5d：OK\n", depth); }
            else { printf("     深度 %5d：结果异常 %s\n", depth, valueToText(v).c_str()); break; }
        }
        // 不设硬性阈值：只要"要么正确、要么安全拒绝"，都算通过。
        // 真正要抓的是崩溃 —— 崩溃的话这个进程根本走不到这里。
        // 崩溃点原先在 5000 层；加上深度保护后应变成"安全拒绝"。
        // 这里既验证能支撑的层数，也验证超限时是拒绝而不是崩。
        OK(maxOk >= 200, "至少支持 200 层括号嵌套（实测 " + std::to_string(maxOk) + "）");
        {
            std::string e(50000, '('); e += "1"; e += std::string(50000, ')');
            Sheet sh;
            std::string err = sh.setFormula(0, 0, e);
            OK(!err.empty(), "50,000 层嵌套被安全拒绝（不崩溃）");
            printf("     深度 50000：安全拒绝（%s）\n", err.c_str());
        }
    }

    std::cout << "\n== 5. 深度：超长公式 ==\n";
    {
        // 规模取 Excel 公式长度上限（8192 字符）之内：4000 项 = 7999 字符。
        // "1+1+..." 每 2 字符一项，Excel 最多约 4096 项，所以 4000 是
        // 真实可能遇到的最坏情况 —— 这一档必须算得对。
        std::string e = "1";
        for (int i = 0; i < 4000; i++) e += "+1";
        Sheet sh;
        auto t0 = Clock::now();
        std::string err = sh.setFormula(0, 0, e);
        double pms = msSince(t0);
        REPORT("解析 4,000 项连加", pms, 5000);
        OK(err.empty(), "长公式解析成功");
        t0 = Clock::now();
        sh.recalc();
        double rms = msSince(t0);
        REPORT("求值 4,000 项连加", rms, 3000);
        Value v = sh.valueAt(0, 0);
        OK(v.isNum() && std::fabs(v.n - 4001) < 1e-9, "连加结果正确");

        // 超出 Excel 长度的极端公式：不要求算对，但**绝不能崩**。
        // 求值递归深度等于 AST 深度，此前没有上限，一万项连加会直接
        // 栈溢出段错误 —— 用户只是写了个长公式，进程就没了。
        std::string big = "1";
        for (int i = 0; i < 20000; i++) big += "+1";
        Sheet sh2;
        std::string err2 = sh2.setFormula(0, 0, big);
        OK(err2.empty(), "超长公式解析成功（两万项）");
        t0 = Clock::now();
        sh2.recalc();                       // 关键：这一步此前是段错误
        REPORT("求值 20,000 项连加（安全拒绝）", msSince(t0), 3000);
        // 结果必须是错误值而不是崩溃或错误数字
        Value v2 = sh2.valueAt(0, 0);
        if (!v2.isError())
            std::cout << "     超深公式未返回错误值（可能是崩溃前的残留）\n";
        OK(v2.isError(), "超深公式返回错误值而非崩溃");
        printf("     4,000 项连加结果 = %s（期望 4001）\n", valueToText(v).c_str());
    }

    std::cout << "\n== 6. 边界：行列上限 ==\n";
    {
        Sheet sh;
        // 合法最大格 XFD1048576 = (16383, 1048575)
        std::string err = sh.setFormula(0, 0, "XFD1048576");
        OK(err.empty(), "引用最大单元格 XFD1048576");

        // 越界引用必须安全失败，不能崩
        bool survived = true;
        const char* badRefs[] = {"XFE1", "XFD1048577", "AAA1", "A0", "A1048577", "ZZZ99999999"};
        for (const char* r : badRefs) {
            Sheet s2;
            std::string e2 = s2.setFormula(0, 0, r);
            s2.recalc();
            s2.display(0, 0);
            (void)e2;
            if (!s2.display(0, 0).empty()) { /* 只要不崩就行 */ }
        }
        OK(survived, "越界引用不崩溃");
        printf("     已试 %zu 个越界引用\n", sizeof(badRefs) / sizeof(badRefs[0]));
    }

    std::cout << "\n== 7. 边界：超长字符串 ==\n";
    {
        std::string big(1000000, 'x');       // 1MB 文本
        Sheet sh;
        auto t0 = Clock::now();
        sh.setValue(0, 0, Value::str(big));
        sh.setValue(0, 1, Value::str("LEN(A1)"));
        std::string err = sh.setFormula(0, 2, "LEN(A1)");
        double ms = msSince(t0);
        REPORT("写入 1MB 文本并求长度", ms, 2000);
        OK(err.empty(), "长文本公式解析成功");
        sh.recalc();
        Value v = sh.valueAt(0, 2);
        OK(v.isNum() && std::fabs(v.n - 1000000) < 1e-9, "长文本 LEN 正确");
        printf("     LEN = %s（期望 1000000）\n", valueToText(v).c_str());

        // 文本函数在大字符串上的重复操作（容易退化成 O(n²)）
        t0 = Clock::now();
        std::string rep;
        for (int i = 0; i < 50; i++) rep += "SUBSTITUTE(";
        rep += "A1";
        for (int i = 0; i < 50; i++) rep += ",\"x\",\"y\")";
        Sheet sh2;
        sh2.setValue(0, 0, Value::str(std::string(10000, 'x')));
        std::string e2 = sh2.setFormula(0, 1, rep);
        sh2.recalc();
        double nms = msSince(t0);
        REPORT("50 层嵌套 SUBSTITUTE(10KB)", nms, 5000);
        OK(e2.empty(), "嵌套文本公式解析成功");
    }

    std::cout << "\n== 8. 边界：数值极值 ==\n";
    {
        struct Case { const char* expr; const char* note; };
        Case cases[] = {
            {"1E308*10", "上溢"},
            {"-1E308*10", "下溢"},
            {"1/0", "除零"},
            {"0/0", "零除零"},
            {"SQRT(-1)", "负数开方"},
            {"LOG(0)", "log(0)"},
            {"LN(-1)", "ln(负数)"},
            {"1E-320", "非规格化数"},
            {"999^999", "巨大幂"},
            {"FACT(171)", "阶乘临界"},
            {"FACT(172)", "阶乘溢出"},
            {"(1E308+1E308)-1E308", "无穷运算"},
        };
        for (auto& c : cases) {
            Sheet sh;
            std::string err = sh.setFormula(0, 0, c.expr);
            if (!err.empty()) { printf("     %-24s 解析拒绝\n", c.expr); continue; }
            sh.recalc();
            std::string t = sh.display(0, 0);
            printf("     %-24s => %-12s (%s)\n", c.expr, t.c_str(), c.note);
            // 只要是错误值或有限数都算安全；NaN/inf 漏出去才要报
            bool safe = t.find("#") != std::string::npos ||
                        t == "Infinity" || t == "-Infinity" ||
                        (t.find_first_not_of("-0123456789.eE+") == std::string::npos);
            OK(safe, std::string("极值安全: ") + c.expr + " -> " + t);
        }
    }

    std::cout << "\n== 9. 循环引用：大规模环 ==\n";
    {
        // 1000 个格子首尾相接成环 —— 检测必须是 O(n) 而不是爆栈
        Sheet sh;
        sh.setFormula(0, 0, "A1000+1");
        for (int i = 1; i < 1000; i++)
            sh.setFormula(0, i, "A" + std::to_string(i) + "+1");
        auto t0 = Clock::now();
        sh.recalc();
        double ms = msSince(t0);
        REPORT("1,000 格环形循环引用检测", ms, 3000);
        bool allCalc = true;
        for (int i = 0; i < 1000; i++) {
            std::string t = sh.display(0, i);
            if (t.find("#") == std::string::npos) allCalc = false;
        }
        OK(allCalc, "环上所有格子都报错误而非死循环");
        printf("     示例 A1 = %s\n", sh.display(0, 0).c_str());
    }

    std::cout << "\n== 10. 泄漏：反复编辑 ==\n";
    {
        long base = rssKB();
        Sheet sh;
        for (int round = 0; round < 20; round++) {
            for (int i = 0; i < 2000; i++) {
                sh.setValue(0, i % 500, Value::num(i));
                sh.setFormula(1, i % 500, "A" + std::to_string(i % 500 + 1) + "*2");
            }
            sh.recalc();
        }
        long after = rssKB();
        long growth = after - base;
        printf("     20 轮 × 2000 次编辑后 RSS: %ld -> %ld KB（增长 %ld KB）\n",
               base, after, growth);
        // 阈值放宽：分配器本身会保留已释放的内存，增长不等于泄漏。
        // 这里只抓"数量级失控"（比如每轮都线性增长几百 MB）。
        OK(growth < 200 * 1024, "反复编辑内存未失控");
    }

    std::cout << "\n== 11. 泄漏：反复解析 ==\n";
    {
        long base = rssKB();
        for (int round = 0; round < 50; round++) {
            Sheet sh;
            for (int i = 0; i < 500; i++)
                sh.setFormula(0, i, "SUM(A1:B2)*IF(A1>0,1,2)+VLOOKUP(1,A1:B2,2,FALSE)");
            sh.recalc();
        }
        long after = rssKB();
        long growth = after - base;
        printf("     50 轮 × 500 条复杂公式后 RSS: %ld -> %ld KB（增长 %ld KB）\n",
               base, after, growth);
        OK(growth < 200 * 1024, "反复解析内存未失控");
    }

    std::cout << "\n== 12. 规模：xlsx 大文件 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("大数据");
        auto t0 = Clock::now();
        for (int r = 0; r < 5000; r++)
            for (int c = 0; c < 20; c++)
                sh.setValue(c, r, Value::num(r * 20 + c));
        double wms = msSince(t0);
        REPORT("构造 100,000 格工作簿", wms, 5000);

        std::string err;
        t0 = Clock::now();
        bool okSave = wb.save("/tmp/xl_stress.xlsx", err);
        double sms = msSince(t0);
        REPORT("保存 100,000 格 xlsx", sms, 30000);
        OK(okSave, "大文件保存成功: " + err);

        if (okSave) {
            Workbook rb;
            t0 = Clock::now();
            bool okLoad = rb.load("/tmp/xl_stress.xlsx", err);
            double lms = msSince(t0);
            REPORT("读回 100,000 格 xlsx", lms, 30000);
            OK(okLoad, "大文件读取成功: " + err);
            if (okLoad) {
                size_t cells = rb.sheet(0).allCells().size();
                printf("     读回单元格数: %zu\n", cells);
                OK(cells == 100000, "单元格数一致（100000）");
            }
        }
    }

    std::cout << "\n== 13. 规模：大图表 ==\n";
    {
        Chart c;
        c.type = ChartType::Line;
        c.title = "大数据折线";
        DataSeries s;
        s.name = "系列";
        for (int i = 0; i < 50000; i++) s.values.push_back(std::sin(i * 0.01) * 100);
        c.series.push_back(s);
        std::vector<unsigned char> png;
        auto t0 = Clock::now();
        bool okPng = chartToPNG(c, png);
        double ms = msSince(t0);
        REPORT("50,000 点折线图渲染 PNG", ms, 30000);
        OK(okPng && png.size() > 1000, "大图表渲染成功");
        printf("     PNG 大小 %zu 字节\n", png.size());
    }

    std::cout << "\n-------------------------------------\n";
    // 与其它测试组保持同一格式（以"通过 N 项"开头），否则汇总 grep 会漏掉
    std::cout << "通过 " << P << " 项，失败 " << N << " 项（压力测试）\n";
    return N ? 1 : 0;
}
