// 生成带数据验证的示例 xlsx，供交叉验证
#include "xlsx.hpp"
#include "dv.hpp"
#include <iostream>
using namespace xl;

int main() {
    Workbook wb;
    Sheet& sh = wb.sheet(0);
    sh.setName("录入表");
    const char* heads[] = {"年龄", "评分", "是否通过"};
    for (int i = 0; i < 3; i++) sh.setValue(i, 0, Value::str(heads[i]));
    int ages[] = {25, 30, 28};
    double scores[] = {88.5, 92, 76.5};
    const char* yn[] = {"是", "否", "是"};
    for (int i = 0; i < 3; i++) {
        sh.setValue(0, i + 1, Value::num(ages[i]));
        sh.setValue(1, i + 1, Value::num(scores[i]));
        sh.setValue(2, i + 1, Value::str(yn[i]));
    }
    sh.recalc();

    // 年龄：18~65 的整数
    {
        DataValidation dv;
        dv.rects.push_back({{0, 1, 0, 3}});
        dv.type = DvType::Whole;
        dv.op = DvOperator::Between;
        dv.formula1 = "18"; dv.formula2 = "65";
        dv.errorTitle = "年龄超出范围";
        dv.error = "请输入 18 到 65 的整数";
        wb.addDv(0, dv);
    }
    // 评分：0~100 的小数
    {
        DataValidation dv;
        dv.rects.push_back({{1, 1, 1, 3}});
        dv.type = DvType::Decimal;
        dv.op = DvOperator::Between;
        dv.formula1 = "0"; dv.formula2 = "100";
        dv.error = "评分应为 0 到 100";
        dv.errorStyle = DvErrorStyle::Warning;
        wb.addDv(0, dv);
    }
    // 是否通过：下拉
    {
        DataValidation dv;
        dv.rects.push_back({{2, 1, 2, 3}});
        dv.type = DvType::List;
        dv.listItems = {"是", "否"};
        dv.formula1 = joinListFormula(dv.listItems);
        dv.error = "只能填“是”或“否”";
        wb.addDv(0, dv);
    }

    std::string err;
    if (!wb.save("out/数据验证.xlsx", err)) { std::cout << "保存失败: " << err << "\n"; return 1; }
    std::cout << "已生成 out/数据验证.xlsx（" << wb.allDvs()[0].size() << " 条规则）\n";

    // 打印校验结果
    struct { const char* label; int c; Value v; } probes[] = {
        {"年龄 25（合法）",   0, Value::num(25)},
        {"年龄 200（越界）",  0, Value::num(200)},
        {"年龄 30.5（非整数）", 0, Value::num(30.5)},
        {"评分 88.5（合法）", 1, Value::num(88.5)},
        {"评分 150（越界）",  1, Value::num(150)},
        {"通过=是（合法）",   2, Value::str("是")},
        {"通过=也许（非法）", 2, Value::str("也许")},
    };
    for (auto& p : probes) {
        const DataValidation* dv = dvForCell(wb.allDvs()[0], p.c, 1);
        bool ok = dv ? checkDataValidation(sh, *dv, p.c, 1, p.v).ok : true;
        std::cout << "  " << p.label << " -> " << (ok ? "通过" : "拒绝") << "\n";
    }
    return 0;
}
