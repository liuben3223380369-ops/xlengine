// 性能基准（常驻）。
//
// 优化前必须先量，否则容易优化掉根本不在热点上的东西 ——
// 这一份就是当初定位"改一格触发全表重算"时用的工具，留下来防止回退。
//
// 关注点：
//   1~3  建表与全量重算（一次性成本）
//   4    增量重算 vs 全量重算 —— 交互场景的关键指标
//   5~7  基础操作的单次开销（函数查找 / 单元格定位 / 显示格式化）
#include "xlsx.hpp"
#include "sheet.hpp"
#include "parser.hpp"
#include "functions.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>

using namespace xl;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    const int N = 20000;      // 公式条数

    std::cout << "剖析规模： " << N << " 条公式\n\n";

    // ---- 阶段 1：解析 ----
    Sheet s1; s1.setName("P");
    for (int i = 0; i < N; i++) s1.setValue(0, i, Value::num(i));
    auto t0 = Clock::now();
    for (int i = 0; i < N; i++) s1.setFormula(1, i, "SUM(A1:A5)*2+LEN(\"abc\")");
    auto t1 = Clock::now();
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  1. setFormula（解析 + 建 AST）        " << std::setw(8) << ms(t0, t1) << " ms\n";

    // ---- 阶段 2：首次求值 ----
    auto t2 = Clock::now();
    s1.recalc();
    auto t3 = Clock::now();
    std::cout << "  2. recalc 首次（全量求值）            " << std::setw(8) << ms(t2, t3) << " ms\n";

    // ---- 阶段 3：二次全量重算（AST 已建好，纯求值）----
    auto t4 = Clock::now();
    s1.recalc();
    auto t5 = Clock::now();
    std::cout << "  3. recalc 二次（纯求值，无解析）      " << std::setw(8) << ms(t4, t5) << " ms\n";

    // ---- 阶段 4：改一格后全量重算（模拟终端每次编辑）----
    auto t6 = Clock::now();
    for (int k = 0; k < 20; k++) { s1.setValue(0, 0, Value::num(k)); s1.recalc(); }
    auto t7 = Clock::now();
    std::cout << "  4. 改 1 格 + 全量重算 × 20            " << std::setw(8) << ms(t6, t7)
              << " ms   （单次 " << ms(t6, t7) / 20 << " ms）\n";

    // ---- 阶段 5：纯函数调用开销（不含 AST 遍历）----
    {
        std::vector<Value> args{Value::num(1), Value::num(2)};
        auto a0 = Clock::now();
        const FnInfo* fn = nullptr;
        for (int i = 0; i < 200000; i++) fn = findFunction("SUM");
        auto a1 = Clock::now();
        std::cout << "  5. findFunction × 200,000             " << std::setw(8) << ms(a0, a1)
                  << " ms   （命中=" << (fn ? "是" : "否") << "）\n";
    }

    // ---- 阶段 6：单元格查找 ----
    {
        Sheet s2; s2.setName("L");
        for (int i = 0; i < 50000; i++) s2.setValue(i % 200, i / 200, Value::num(i));
        auto b0 = Clock::now();
        long long acc = 0;
        for (int k = 0; k < 20; k++)
            for (int i = 0; i < 50000; i++) {
                const Sheet::CellRec* c = s2.find(i % 200, i / 200);
                if (c && c->value.isNum()) acc += 1;
            }
        auto b1 = Clock::now();
        std::cout << "  6. find() × 1,000,000                 " << std::setw(8) << ms(b0, b1)
                  << " ms   （命中 " << acc << "）\n";
    }

    // ---- 阶段 7：display（数字格式化）----
    {
        Sheet s3; s3.setName("D");
        for (int i = 0; i < 50000; i++) s3.setValue(0, i, Value::num(i * 1.5));
        auto c0 = Clock::now();
        size_t total = 0;
        for (int k = 0; k < 5; k++)
            for (int i = 0; i < 50000; i++) total += s3.display(0, i).size();
        auto c1 = Clock::now();
        std::cout << "  7. display() × 250,000                " << std::setw(8) << ms(c0, c1)
                  << " ms   （字节 " << total << "）\n";
    }

    // ---- 阶段 8：增量 vs 全量（真实形态：局部依赖）----
    {
        Sheet sL; sL.setName("Bench");
        for (int i = 0; i < N; i++) sL.setValue(0, i, Value::num(i));
        for (int i = 0; i < N; i++)
            sL.setFormula(1, i, "A" + std::to_string(i + 1) + "*2");
        sL.recalc();
        auto d0 = Clock::now();
        for (int k = 0; k < 100; k++) { sL.setValue(0, 5, Value::num(k)); sL.recalcDirty(); }
        auto d1 = Clock::now();
        auto d2 = Clock::now();
        for (int k = 0; k < 100; k++) { sL.setValue(0, 5, Value::num(k)); sL.recalc(); }
        auto d3 = Clock::now();
        double inc = ms(d0, d1) / 100.0, full = ms(d2, d3) / 100.0;
        std::cout << "  8. 改 1 格（局部依赖，×100 取单次）\n";
        std::cout << "       recalcDirty                     " << std::setw(8) << inc << " ms\n";
        std::cout << "       recalc（全量）                  " << std::setw(8) << full << " ms\n";
        std::cout << "       提速                            " << std::setw(8)
                  << (inc > 0 ? full / inc : 0) << " x\n";
        std::cout << "       依赖边 " << sL.depEdgeCount() << " 条\n";
    }

    std::cout << "\n";
    return 0;
}
