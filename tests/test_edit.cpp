// ---------------------------------------------------------------------------
// 结构性编辑测试：插入 / 删除行与列
// ---------------------------------------------------------------------------
// 重点验证两件最容易错的事：
//   1. 公式引用按"引用跟着数据走"重写，而不是位置固定
//   2. 合并区域、条件格式、数据验证、行高列宽、批注都要跟着动
#include <iostream>
#include <string>
#include "sheet.hpp"
#include "xlsx.hpp"
#include "refshift.hpp"

static int gPass = 0, gFail = 0;

static void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { gPass++; std::cout << "  [OK]   " << name << "\n"; }
    else    { gFail++; std::cout << "  [FAIL] " << name << "   " << detail << "\n"; }
}

static std::string fml(xl::Sheet& sh, int c, int r) {
    std::string f;
    sh.cellFormula(c, r, f);
    return f;
}

int main() {
    std::cout << "== 引用重写：插入行 ==\n";
    {
        xl::Workbook wb;
        xl::Sheet& sh = wb.sheet(0);
        sh.setValue(0, 0, xl::Value::num(1));
        sh.setValue(0, 1, xl::Value::num(2));
        sh.setValue(0, 2, xl::Value::num(3));
        sh.setFormula(1, 0, "A1+A2");
        sh.setFormula(1, 1, "A2+A3");
        wb.recalcDirty();
        check("插入前 B1=3", sh.display(1, 0) == "3", sh.display(1, 0));

        wb.insertRows(0, 1, 1);           // 在第 2 行（index 1）处插入
        wb.recalcDirty();

        check("A1 不动", sh.display(0, 0) == "1", sh.display(0, 0));
        check("原第2行数据到了第3行", sh.display(0, 2) == "2", sh.display(0, 2));
        check("原第3行数据到了第4行", sh.display(0, 3) == "3", sh.display(0, 3));
        // A1 在插入点前不动；A2 指向"原第2行"，数据走了，引用要跟着走
        check("A1不动 A2->A3", fml(sh, 1, 0) == "A1+A3", fml(sh, 1, 0));
        check("B2的公式整体后移", fml(sh, 1, 2) == "A3+A4", fml(sh, 1, 2));
    }

    std::cout << "== 引用重写：绝对引用不动 ==\n";
    {
        xl::Workbook wb;
        xl::Sheet& sh = wb.sheet(0);
        sh.setValue(0, 0, xl::Value::num(5));
        sh.setFormula(1, 0, "$A$1*2");
        sh.setFormula(2, 0, "A$1*2");     // 行绝对，列相对
        sh.setFormula(3, 0, "$A1*2");     // 列绝对，行相对
        wb.recalcDirty();
        wb.insertRows(0, 0, 1);           // 在最前面插入一行
        wb.recalcDirty();
        check("$A$1 完全不动", fml(sh, 1, 1) == "$A$1*2", fml(sh, 1, 1));
        check("A$1 行绝对不动", fml(sh, 2, 1) == "A$1*2", fml(sh, 2, 1));
        check("$A1 行相对变 $A2", fml(sh, 3, 1) == "$A2*2", fml(sh, 3, 1));
    }

    std::cout << "== 插入列 ==\n";
    {
        xl::Workbook wb;
        xl::Sheet& sh = wb.sheet(0);
        sh.setValue(0, 0, xl::Value::num(10));
        sh.setValue(1, 0, xl::Value::num(20));
        wb.insertCols(0, 1, 1);
        wb.recalcDirty();
        check("原 B1 到 C1", sh.display(2, 0) == "20", sh.display(2, 0));
        check("B1 空出来了", !sh.hasCell(1, 0));
    }

    std::cout << "== 删除行 ==\n";
    {
        xl::Workbook wb;
        xl::Sheet& sh = wb.sheet(0);
        for (int i = 0; i < 4; i++) sh.setValue(0, i, xl::Value::num(i + 1));
        wb.deleteRows(0, 1, 1);           // 删掉第 2 行（值是 2）
        wb.recalcDirty();
        check("原第3行上移到第2行", sh.display(0, 1) == "3", sh.display(0, 1));
        check("行数减少", !sh.hasCell(0, 3) || sh.display(0, 3).empty());
    }

    std::cout << "== 合并区域跟随 ==\n";
    {
        xl::Workbook wb;
        wb.addMerge(0, 0, 0, 1, 1);
        check("合并前是锚点", wb.mergeSpan(0, 0, 0).first == 2);
        wb.insertRows(0, 0, 1);           // 在合并区之前插入一行
        int ac, ar;
        check("合并区跟着下移", wb.mergeAnchorOf(0, 0, 1, ac, ar) && ar == 1,
              "锚点行=" + std::to_string(ar));
        check("原位置不再是锚点", !wb.mergeAnchorOf(0, 0, 0, ac, ar));
    }

    std::cout << "== 取消合并 ==\n";
    {
        xl::Workbook wb;
        wb.addMerge(0, 0, 0, 1, 1);
        check("取消成功", wb.unmerge(0, 0, 0, 1, 1));
        int ac, ar;
        check("取消后无锚点", !wb.mergeAnchorOf(0, 0, 0, ac, ar));
        check("再取消返回 false", !wb.unmerge(0, 0, 0, 1, 1));
    }

    std::cout << "== 行高跟随 ==\n";
    {
        xl::Workbook wb;
        wb.layout(0).rowHeights[3] = 40;
        wb.insertRows(0, 1, 2);           // 在第 2 行处插入 2 行
        bool has = wb.layout(0).rowHeights.count(5) > 0;
        check("行高跟着下移 3->5", has);
        check("原位置不再有自定义行高", wb.layout(0).rowHeights.count(3) == 0);
    }

    std::cout << "== 数值不变性（插入后公式结果应保持一致）==\n";
    {
        // 这条最有意义：插入空行不该改变任何计算结果
        xl::Workbook wb;
        xl::Sheet& sh = wb.sheet(0);
        for (int i = 0; i < 5; i++) sh.setValue(0, i, xl::Value::num(i + 1));
        sh.setFormula(1, 0, "SUM(A1:A5)");
        wb.recalcDirty();
        std::string before = sh.display(1, 0);
        wb.insertRows(0, 0, 3);           // 在最前面插入 3 行
        wb.recalcDirty();
        std::string after = sh.display(1, 3);
        check("插入行后 SUM 结果不变", before == after,
              "插入前=" + before + " 插入后=" + after);
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << gPass << " 项，失败 " << gFail << " 项\n";
    return gFail ? 1 : 0;
}
