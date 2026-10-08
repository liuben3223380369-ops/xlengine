// 生成带各种数字格式的示例 xlsx，供交叉验证与人工查看
#include "xlsx.hpp"
#include "numfmt.hpp"
#include "date.hpp"
#include <iostream>
using namespace xl;

int main() {
    Workbook wb;
    Sheet& sh = wb.sheet(0);
    sh.setName("格式");

    // 表头
    sh.setValue(0, 0, Value::str("格式码"));
    sh.setValue(1, 0, Value::str("原始值"));
    sh.setValue(2, 0, Value::str("显示结果"));

    struct Row { const char* code; double v; };
    Row rows[] = {
        {"#,##0.00",      1234.5678},
        {"0.00%",           0.1234},
        {"yyyy-mm-dd", ymdToSerial(2026, 9, 27)},
        {"h:mm:ss",           0.625},
        {"#,##0,",        1234567.0},
        {"0",              1234.5678},
    };
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        int r = (int)i + 1;
        sh.setValue(0, r, Value::str(rows[i].code));
        sh.setValue(1, r, Value::num(rows[i].v));
        sh.setNumFmt(1, r, rows[i].code);
        sh.setValue(2, r, Value::num(rows[i].v));
        sh.setNumFmt(2, r, rows[i].code);
    }
    // 一个不带格式的格子，用来确认默认是 General
    sh.setValue(3, 1, Value::num(1234.5678));

    std::string err;
    if (!wb.save("out/数字格式.xlsx", err)) { std::cout << "保存失败: " << err << "\n"; return 1; }
    std::cout << "已生成 out/数字格式.xlsx\n";
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        int r = (int)i + 1;
        std::cout << "  " << rows[i].code << "  ->  " << sh.display(2, r) << "\n";
    }
    return 0;
}
