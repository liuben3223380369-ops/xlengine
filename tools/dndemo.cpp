// 生成带定义名称的示例 xlsx，供交叉验证
#include "xlsx.hpp"
#include <iostream>
using namespace xl;

int main() {
    Workbook wb;
    Sheet& sh = wb.sheet(0);
    sh.setName("销售数据");
    const char* heads[] = {"季度", "销售额"};
    sh.setValue(0, 0, Value::str(heads[0]));
    sh.setValue(1, 0, Value::str(heads[1]));
    const char* qs[] = {"Q1", "Q2", "Q3", "Q4"};
    double vals[] = {120, 165, 98, 132};
    for (int i = 0; i < 4; i++) {
        sh.setValue(0, i + 1, Value::str(qs[i]));
        sh.setValue(1, i + 1, Value::num(vals[i]));
    }
    // 用定义名称算合计，验证命名区域真的接进了求值链
    // 汇总必须放在数据区之外。
    // 命名区域把 B2:B5 隐藏成了"销售额"，写公式时看不出边界 ——
    // 若把 =MAX(销售额) 写在 B4，B4 自己就落在 B2:B5 里，
    // 会静默变成自引用（#CALC!）。这里放到第 7 行避开。
    sh.setFormula(0, 6, "SUM(销售额)");
    sh.setFormula(1, 6, "MAX(销售额)");
    sh.setFormula(2, 6, "AVERAGE(销售额)");
    sh.recalc();

    std::string why;
    wb.addDefinedName("销售额", "销售数据!$B$2:$B$5", why);
    wb.addDefinedName("季度", "销售数据!$A$2:$A$5", why);
    wb.addDefinedName("税率", "0.13", why);
    // 常量型名称参与计算
    sh.setFormula(3, 6, "SUM(销售额)*税率");
    sh.recalc();

    std::string err;
    if (!wb.save("out/定义名称.xlsx", err)) { std::cout << "保存失败: " << err << "\n"; return 1; }
    std::cout << "已生成 out/定义名称.xlsx（" << wb.definedNames().size() << " 个名称）\n";
    std::cout << "  A7 SUM=" << sh.display(0, 6)
              << "  B7 MAX=" << sh.display(1, 6)
              << "  C7 AVERAGE=" << sh.display(2, 6)
              << "  D7 SUM*税率=" << sh.display(3, 6) << "\n";
    return 0;
}
