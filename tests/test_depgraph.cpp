// 依赖图与增量重算测试
//
// 增量重算的失败模式是"跑得飞快但数是旧的" —— 比慢危险得多，
// 因为它不报错。所以这一组的核心是**差分测试**：
// 同一串随机编辑，分别用 recalcDirty() 与 recalc() 跑，结果必须逐格相同。
//
// 另外单独验证几条容易漏的失效路径：
//   定义名称变更、跨表引用、整列引用、删除格子、改公式（旧依赖摘除）
#include "sheet.hpp"
#include "xlsx.hpp"
#include <iostream>
#include <random>
#include <sstream>

using namespace xl;

static int P = 0, N = 0;
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}
static void EQS(const std::string& g, const std::string& e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望='" << e << "' 实际='" << g << "'\n"; }
}
static void EQI(long long g, long long e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望=" << e << " 实际=" << g << "\n"; }
}

// 把整张表拍成字符串，用于比对两种重算方式的结果
static std::string snapshot(Sheet& s, int cols, int rows) {
    std::ostringstream o;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) o << c << "," << r << "=" << s.display(c, r) << ";";
    return o.str();
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    std::cout << "== 基本：改一格，依赖它的格子跟着变 ==\n";
    {
        Sheet s; s.setName("T");
        s.setValue(0, 0, Value::num(1));
        s.setFormula(1, 0, "A1*10");
        s.recalcDirty();
        EQS(s.display(1, 0), "10", "B1 = A1*10");
        s.setValue(0, 0, Value::num(5));
        s.recalcDirty();
        EQS(s.display(1, 0), "50", "改 A1=5 后 B1=50");
        // 再改一次，确认不是"只生效一次"
        s.setValue(0, 0, Value::num(7));
        s.recalcDirty();
        EQS(s.display(1, 0), "70", "再改 A1=7 后 B1=70");
    }

    std::cout << "== 链式依赖：A->B->C->D 全部更新 ==\n";
    {
        Sheet s; s.setName("C");
        s.setValue(0, 0, Value::num(2));
        s.setFormula(1, 0, "A1+1");
        s.setFormula(2, 0, "B1*2");
        s.setFormula(3, 0, "C1*3");
        s.recalcDirty();
        // A1=2 -> B1=3 -> C1=6 -> D1=18
        EQS(s.display(1, 0), "3", "B1=3");
        EQS(s.display(2, 0), "6", "C1=6");
        EQS(s.display(3, 0), "18", "D1=18");
        s.setValue(0, 0, Value::num(10));
        s.recalcDirty();
        EQS(s.display(3, 0), "66", "改 A1=10 后 D1=(10+1)*2*3=66");
    }

    std::cout << "== 改公式后旧依赖要摘除 ==\n";
    {
        Sheet s; s.setName("R");
        s.setValue(0, 0, Value::num(1));
        s.setValue(2, 0, Value::num(100));
        s.setFormula(1, 0, "A1*2");
        s.recalcDirty();
        EQS(s.display(1, 0), "2", "初值依赖 A1");
        // 改成依赖 C1
        s.setFormula(1, 0, "C1*2");
        s.recalcDirty();
        EQS(s.display(1, 0), "200", "改公式后依赖 C1");
        // 现在改 A1 不该影响 B1
        s.setValue(0, 0, Value::num(999));
        s.recalcDirty();
        EQS(s.display(1, 0), "200", "改已被摘除的旧依赖 A1，B1 不变");
        // 改 C1 仍应影响
        s.setValue(2, 0, Value::num(5));
        s.recalcDirty();
        EQS(s.display(1, 0), "10", "改新依赖 C1，B1 跟着变");
    }

    std::cout << "== 循环引用仍然正确 ==\n";
    {
        Sheet s; s.setName("X");
        s.setFormula(0, 0, "B1+1");
        s.setFormula(1, 0, "A1+1");
        s.recalcDirty();
        EQS(s.display(0, 0), "#CALC!", "A1 = #CALC!");
        OK(s.circularCells().size() >= 1, "循环集合非空");
        // 再改一次仍不崩
        s.recalcDirty();
        EQS(s.display(0, 0), "#CALC!", "重复重算仍为 #CALC!");
    }

    std::cout << "== 整列引用 ==\n";
    {
        Sheet s; s.setName("W");
        for (int i = 0; i < 5; i++) s.setValue(0, i, Value::num(i + 1));
        s.setFormula(1, 0, "SUM(A:A)");
        s.recalcDirty();
        EQS(s.display(1, 0), "15", "SUM(A:A)=15");
        s.setValue(0, 2, Value::num(100));
        s.recalcDirty();
        EQS(s.display(1, 0), "112", "改 A3=100 后 =112");
    }

    std::cout << "== 删除格子 ==\n";
    {
        Sheet s; s.setName("E");
        s.setValue(0, 0, Value::num(4));
        s.setFormula(1, 0, "A1*3");
        s.recalcDirty();
        EQS(s.display(1, 0), "12", "B1=12");
        s.eraseCell(0, 0);
        s.recalcDirty();
        // A1 没了 -> 空值参与算术 = 0
        EQS(s.display(1, 0), "0", "删 A1 后 B1=0");
        OK(!s.hasCell(0, 0), "A1 确实被删");
    }

    std::cout << "== 定义名称变更要让引用它的公式重算 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("数据");
        sh.setValue(0, 0, Value::num(10));      // A1
        sh.setValue(0, 1, Value::num(20));      // A2
        sh.setValue(1, 0, Value::num(7));       // B1（坐标是 col,row）
        std::string why;
        wb.addDefinedName("基数", "数据!$A$1:$A$2", why);
        sh.setFormula(2, 0, "SUM(基数)");
        wb.recalcDirty();
        EQS(sh.display(2, 0), "30", "初次 SUM(基数)=30");
        // 改定义：指向别处
        wb.addDefinedName("基数", "数据!$B$1:$B$2", why);
        wb.recalcDirty();
        EQS(sh.display(2, 0), "7", "改定义后重算为 7（B1=7，B2 空）");
    }

    std::cout << "== 跨表引用 ==\n";
    {
        Workbook wb;
        Sheet& a = wb.sheet(0);
        a.setName("源");
        a.setValue(0, 0, Value::num(5));
        wb.addSheet("目标");
        Sheet& b = wb.sheet(1);
        b.setFormula(0, 0, "源!A1*2");
        wb.recalcDirty();
        EQS(b.display(0, 0), "10", "目标!A1 = 源!A1*2 = 10");
        a.setValue(0, 0, Value::num(9));
        wb.recalcDirty();
        EQS(b.display(0, 0), "18", "改源表后跨表公式跟着变 = 18");
    }


    std::cout << "== 表名重命名后跨表引用仍可用（既有 bug 的回归） ==\n";
    {
        // 这个 bug 是写本组测试时才发现的：
        // Workbook::sheetByName 读的是 SheetRec::name，而 Sheet::setName()
        // 只改 Sheet 自己那一份。"建表后改名"的表在查找里永远查不到，
        // 于是跨表引用一律 #REF! 且不报错。
        Workbook wb;
        Sheet& a = wb.sheet(0);
        a.setName("一月");                  // 建表后改名（关键）
        a.setValue(0, 0, Value::num(42));
        wb.addSheet("二月");
        Sheet& b = wb.sheet(1);
        b.setName("二月");
        OK(wb.sheetByName("一月") == &a, "改名后仍能按名字找到表");
        // 用 renameSheet 也应保持同步
        OK(wb.renameSheet(0, "正月"), "renameSheet 成功");
        OK(wb.sheetByName("正月") == &a, "renameSheet 后两边同步");
        OK(wb.sheetNames()[0] == "正月", "sheetNames 也同步");

        b.setFormula(0, 0, "正月!A1+1");
        wb.recalcDirty();
        EQS(b.display(0, 0), "43", "跨表引用 = 43");
        a.setValue(0, 0, Value::num(100));
        wb.recalcDirty();
        EQS(b.display(0, 0), "101", "改源表后跨表增量更新 = 101");
    }

    // ------------------------------------------------------------------
    // 差分测试：随机编辑序列下，增量与全量必须完全一致
    // ------------------------------------------------------------------
    std::cout << "== 差分测试：recalcDirty 与 recalc 结果必须一致 ==\n";
    {
        std::mt19937 rng(20260927);          // 固定种子，失败可复现
        int mismatches = 0;
        int steps = 0;

        for (int trial = 0; trial < 30; trial++) {
            // 建两张结构完全相同的表
            auto build = [&](Sheet& s) {
                s.setName("D");
                for (int i = 0; i < 12; i++) s.setValue(0, i, Value::num(i + 1));
                s.setFormula(1, 0, "SUM(A1:A4)");
                s.setFormula(1, 1, "A1*A2");
                s.setFormula(1, 2, "B1+B2");
                s.setFormula(1, 3, "MAX(A1:A12)");
                s.setFormula(1, 4, "IF(B1>10,1,0)");
                s.setFormula(1, 5, "B3*2+1");
                s.setFormula(1, 6, "SUM(A:A)");
                s.setFormula(1, 7, "A5+A6+A7");
                s.setFormula(1, 8, "B1/B2");
                s.setFormula(1, 9, "LEN(TEXTJOIN(\"\",TRUE,A1:A3))");
                s.setFormula(1, 10, "AVERAGE(A1:A6)");
                s.setFormula(1, 11, "B10*3");
            };
            Sheet inc, full;
            build(inc); build(full);
            inc.recalc(); full.recalc();

            // 一串随机编辑：增量表用 recalcDirty，全量表用 recalc
            for (int step = 0; step < 25; step++) {
                int kind = (int)(rng() % 3);
                int c = (int)(rng() % 2);
                int r = (int)(rng() % 12);
                double v = (double)(rng() % 100);
                if (kind == 0) {
                    inc.setValue(c, r, Value::num(v));
                    full.setValue(c, r, Value::num(v));
                } else if (kind == 1) {
                    inc.setValue(0, r, Value::num(v));       // 只动数据列
                    full.setValue(0, r, Value::num(v));
                } else {
                    std::string f = "A" + std::to_string((int)(rng() % 12) + 1) + "+1";
                    inc.setFormula(1, r, f);
                    full.setFormula(1, r, f);
                }
                inc.recalcDirty();
                full.recalc();
                steps++;
                if (snapshot(inc, 2, 12) != snapshot(full, 2, 12)) {
                    if (mismatches == 0)
                        std::cout << "    首次不一致: trial=" << trial << " step=" << step
                                  << " kind=" << kind << "\n";
                    mismatches++;
                }
            }
        }
        OK(mismatches == 0, "差分不一致次数=" + std::to_string(mismatches)
                          + " / 共 " + std::to_string(steps) + " 步");
    }


    std::cout << "== 跨表链：A 引用 B 引用 C ==\n";
    {
        // 三层链会暴露"固定趟数传播"的问题：
        // 只跑两趟的话第三层拿不到更新，且这种失败是间歇性的、很难复现。
        Workbook wb;
        wb.sheet(0).setName("C表");
        wb.sheet(0).setValue(0, 0, Value::num(1));
        wb.addSheet("B表"); wb.sheet(1).setName("B表");
        wb.sheet(1).setFormula(0, 0, "C表!A1+1");
        wb.addSheet("A表"); wb.sheet(2).setName("A表");
        wb.sheet(2).setFormula(0, 0, "B表!A1*10");
        wb.recalcDirty();
        // C=1 -> B=2 -> A=20
        EQS(wb.sheet(1).display(0, 0), "2", "B表 = C+1 = 2");
        EQS(wb.sheet(2).display(0, 0), "20", "A表 = B*10 = 20");
        wb.sheet(0).setValue(0, 0, Value::num(5));
        wb.recalcDirty();
        EQS(wb.sheet(1).display(0, 0), "6", "改源后 B表 = 6");
        EQS(wb.sheet(2).display(0, 0), "60", "三层链末端 A表 = 60");
    }

    std::cout << "== 依赖图规模 ==\n";
    {
        Sheet s; s.setName("G");
        for (int i = 0; i < 100; i++) s.setValue(0, i, Value::num(i));
        for (int i = 0; i < 100; i++) s.setFormula(1, i, "SUM(A1:A10)");
        EQI(s.depCellCount(), 100, "100 条公式登记");
        EQI(s.depEdgeCount(), 1000, "依赖边 100×10");
        s.recalcDirty();
        EQI(s.dirtyCount(), 0, "重算后脏集合清空");
        // 改一格 -> 只标脏依赖它的 100 条（本例中全都依赖 A1:A10）
        s.setValue(0, 0, Value::num(1));
        EQI(s.dirtyCount() >= 100 ? 1 : 0, 1, "改 A1 后有脏格子");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
