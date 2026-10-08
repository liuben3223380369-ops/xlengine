// 生成带列宽/行高/冻结/筛选的示例 xlsx，供交叉验证
#include "xlsx.hpp"
#include "view.hpp"
#include <iostream>
using namespace xl;

int main() {
    Workbook wb;
    Sheet& sh = wb.sheet(0);
    sh.setName("销售明细");
    const char* heads[] = {"地区", "一季度", "二季度", "合计"};
    for (int i = 0; i < 4; i++) sh.setValue(i, 0, Value::str(heads[i]));
    const char* rows[][4] = {
        {"华北", "120", "165", "285"}, {"华东", "98", "132", "230"},
        {"华南", "87", "110", "197"}, {"西部", "65", "88", "153"},
    };
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (c == 0) sh.setValue(c, r + 1, Value::str(rows[r][c]));
            else        sh.setValue(c, r + 1, Value::num(std::stod(rows[r][c])));
        }
    sh.recalc();

    wb.setColWidth(0, 0, 16);      // 地区列宽一些（中文）
    wb.setColWidth(0, 1, 12);
    wb.setColWidth(0, 2, 12);
    wb.setColWidth(0, 3, 12);
    wb.setRowHeight(0, 0, 30);     // 表头行高
    wb.setFreeze(0, 1, 1);         // 冻结首行首列
    wb.setAutoFilter(0, 0, 0, 3, 4);

    std::string err;
    if (!wb.save("out/视图属性.xlsx", err)) { std::cout << "保存失败: " << err << "\n"; return 1; }
    std::cout << "已生成 out/视图属性.xlsx\n";
    std::cout << "  列宽 A=" << wb.layout(0).colWidths.at(0)
              << "  行高1=" << wb.layout(0).rowHeights.at(0)
              << "  冻结=" << wb.layout(0).freeze.frozenCols << "列/"
              << wb.layout(0).freeze.frozenRows << "行"
              << "  筛选=" << (wb.layout(0).filter.enabled ? "有" : "无") << "\n";
    return 0;
}
