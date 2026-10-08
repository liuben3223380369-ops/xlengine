// 生成带样式与合并单元格的示例 xlsx，供交叉验证与人工查看
#include "xlsx.hpp"
#include "style.hpp"
#include <iostream>
using namespace xl;

int main() {
    Workbook wb;
    Sheet& sh = wb.sheet(0);
    sh.setName("样式");

    // 标题：跨 A1:D1 合并，粗体红字居中
    CellStyle title;
    title.bold = true;
    title.fontColor = "FF0000";
    title.hAlign = HAlign::Center;
    title.fontSize = 14;
    sh.setValue(0, 0, Value::str("季度汇总"));
    sh.setStyle(0, 0, title);
    wb.addMerge(0, 0, 0, 3, 0);

    // 表头：黄底 + 细边框
    CellStyle head;
    head.fillColor = "FFFF00";
    head.border = BorderStyle::Thin;
    head.hAlign = HAlign::Center;
    const char* cols[] = {"地区", "一季度", "二季度", "合计"};
    for (int i = 0; i < 4; i++) {
        sh.setValue(i, 1, Value::str(cols[i]));
        sh.setStyle(i, 1, head);
    }

    // 数据：数字带千分位
    CellStyle num;
    num.hAlign = HAlign::Right;
    double vals[3][3] = {{120, 165, 285}, {98, 132, 230}, {87, 110, 197}};
    const char* areas[] = {"华北", "华东", "华南"};
    for (int r = 0; r < 3; r++) {
        sh.setValue(0, r + 2, Value::str(areas[r]));
        for (int c = 0; c < 3; c++) {
            sh.setValue(c + 1, r + 2, Value::num(vals[r][c]));
            sh.setNumFmt(c + 1, r + 2, "#,##0");
            sh.setStyle(c + 1, r + 2, num);
        }
    }
    sh.recalc();

    std::string err;
    if (!wb.save("out/样式示例.xlsx", err)) { std::cout << "保存失败: " << err << "\n"; return 1; }
    std::cout << "已生成 out/样式示例.xlsx\n";
    std::cout << "  合并区域数: " << wb.allMerges()[0].size() << "\n";
    return 0;
}
