// 压测：验证引擎在接近真实规模的数据上是否可用
#include "sheet.hpp"
#include "functions.hpp"
#include "parser.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
using namespace xl;

static double now() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() / 1000.0;
}

int main() {
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "函数总数: " << functionNames().size() << "\n\n";

    // --- 1. 写入 10 万单元格 ---
    {
        Sheet s;
        double t0 = now();
        for (int r = 0; r < 1000; r++)
            for (int c = 0; c < 100; c++)
                s.setValue(c, r, Value::num((double)(r * 100 + c)));
        double t1 = now();
        std::cout << "写入 100,000 个单元格      " << std::setw(8) << (t1 - t0) << " ms"
                  << "   (" << s.cellCount() << " 条记录)\n";
    }

    // --- 2. 10 万条 SUM 公式并首次求值 ---
    {
        Sheet s;
        for (int r = 0; r < 1000; r++)
            for (int c = 0; c < 100; c++)
                s.setValue(c, r, Value::num((double)(r * 100 + c)));
        double t0 = now();
        for (int r = 0; r < 1000; r++) {
            std::string f = "=SUM(A" + std::to_string(r + 1) + ":CV" + std::to_string(r + 1) + ")";
            std::string e = s.setFormula(100, r, f);
            if (!e.empty()) { std::cout << "公式失败: " << e << "\n"; return 1; }
        }
        double t1 = now();
        s.recalc();
        double t2 = now();
        std::cout << "1,000 条 SUM(100列) 解析    " << std::setw(8) << (t1 - t0) << " ms\n";
        std::cout << "  首次全量重算              " << std::setw(8) << (t2 - t1) << " ms\n";
        // 抽查：第 5 行应为 400..499 之和 = (400+499)*100/2 = 44950
        std::cout << "  校验 row5 = " << s.display(100, 4) << " (期望 44950)\n";
    }

    // --- 3. 依赖链：5000 层逐级引用 ---
    {
        Sheet s;
        s.setValue(0, 0, Value::num(1));
        double t0 = now();
        for (int r = 1; r < 5000; r++)
            s.setFormula(0, r, "=A" + std::to_string(r) + "+1");
        double t1 = now();
        s.recalc();
        double t2 = now();
        std::cout << "\n5,000 层依赖链构建          " << std::setw(8) << (t1 - t0) << " ms\n";
        std::cout << "  递归求值                  " << std::setw(8) << (t2 - t1) << " ms"
                  << "   末值=" << s.display(0, 4999) << " (期望 5000)\n";
    }

    // --- 4. 缓存命中后的重复读取 ---
    {
        Sheet s;
        for (int c = 0; c < 200; c++) s.setValue(c, 0, Value::num((double)c));
        s.setFormula(0, 1, "=SUM(A1:GR1)");
        s.recalc();
        double t0 = now();
        for (int i = 0; i < 100000; i++) s.valueAt(0, 1);
        double t1 = now();
        std::cout << "\n100,000 次缓存命中读取      " << std::setw(8) << (t1 - t0) << " ms\n";
    }

    // --- 5. 循环引用不被卡死 ---
    {
        Sheet s;
        s.setFormula(0, 0, "=B1");
        s.setFormula(1, 0, "=A1");
        double t0 = now();
        s.recalc();
        double t1 = now();
        std::cout << "\n循环引用检测                " << std::setw(8) << (t1 - t0) << " ms"
                  << "   结果=" << s.display(0, 0) << "\n";
    }

    // --- 6. 稀疏性：只存非空格 ---
    {
        Sheet s;
        s.setValue(0, 0, Value::num(1));
        s.setValue(16000, 1000000, Value::num(2));      // 接近 Excel 边界
        std::cout << "\n稀疏存储: 2 个极端位置 -> 实际记录 " << s.cellCount() << " 条\n";
        std::cout << "  XFD1048576 = " << s.display(16000, 1000000) << " (期望 2)\n";
    }

    return 0;
}
