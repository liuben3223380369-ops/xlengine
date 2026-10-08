// 生成带批注的示例 xlsx，供交叉验证
#include "xlsx.hpp"
#include "note.hpp"
#include <iostream>
using namespace xl;

int main() {
    Workbook wb;
    Sheet& sh = wb.sheet(0);
    sh.setName("核算表");
    const char* heads[] = {"项目", "金额", "备注"};
    for (int i = 0; i < 3; i++) sh.setValue(i, 0, Value::str(heads[i]));
    const char* items[] = {"差旅", "办公", "招待"};
    for (int i = 0; i < 3; i++) sh.setValue(0, i + 1, Value::str(items[i]));
    sh.setValue(1, 1, Value::num(3200));
    sh.setValue(1, 2, Value::num(860));
    sh.setValue(1, 3, Value::num(1500));
    sh.recalc();

    CellNote a; a.col = 1; a.row = 1; a.author = "张三";
    a.text = "这笔含机票，需附行程单";
    wb.addNote(0, a);
    CellNote b; b.col = 1; b.row = 3; b.author = "李四";
    b.text = "招待费超标，需说明原因\n（附审批单）";
    wb.addNote(0, b);
    CellNote c; c.col = 2; c.row = 2; c.author = "张三";
    c.text = "已核对发票";
    wb.addNote(0, c);

    std::string err;
    if (!wb.save("out/批注.xlsx", err)) { std::cout << "保存失败: " << err << "\n"; return 1; }
    std::cout << "已生成 out/批注.xlsx（" << wb.allNotes()[0].size() << " 条批注）\n";
    for (const CellNote& n : wb.allNotes()[0])
        std::cout << "  " << cellAddrText(n.col, n.row) << " " << n.author << ": " << n.text << "\n";
    return 0;
}
