// 生成带条件格式的示例 xlsx，供交叉验证与人工查看
#include "xlsx.hpp"
#include "cf.hpp"
#include <iostream>
using namespace xl;

int main() {
    Workbook wb;
    Sheet& sh = wb.sheet(0);
    sh.setName("成绩");

    // 表头
    const char* heads[] = {"姓名", "分数"};
    for (int i = 0; i < 2; i++) sh.setValue(i, 0, Value::str(heads[i]));

    const char* names[] = {"张三", "李四", "王五", "赵六", "钱七"};
    double scores[] = {95, 42, 78, 61, 88};
    for (int i = 0; i < 5; i++) {
        sh.setValue(0, i + 1, Value::str(names[i]));
        sh.setValue(1, i + 1, Value::num(scores[i]));
    }
    sh.recalc();

    // 1) 不及格（<60）标红
    {
        ConditionalFormat cf;
        cf.rects.push_back({{1, 1, 1, 5}});
        CfRule r;
        r.type = CfType::CellIs;
        r.op = CfOperator::LessThan;
        r.formulas = {"60"};
        r.style.fillColor = "FF0000";
        r.style.bold = true;
        r.priority = 1;
        cf.rules.push_back(r);
        wb.addCf(0, cf);
    }
    // 2) 90 分以上标绿
    {
        ConditionalFormat cf;
        cf.rects.push_back({{1, 1, 1, 5}});
        CfRule r;
        r.type = CfType::CellIs;
        r.op = CfOperator::GreaterThanOrEqual;
        r.formulas = {"90"};
        r.style.fillColor = "00FF00";
        r.priority = 2;
        cf.rules.push_back(r);
        wb.addCf(0, cf);
    }
    // 3) 前 2 名加粗
    {
        ConditionalFormat cf;
        cf.rects.push_back({{1, 1, 1, 5}});
        CfRule r;
        r.type = CfType::Top10;
        r.rank = 2;
        r.style.bold = true;
        r.priority = 3;
        cf.rules.push_back(r);
        wb.addCf(0, cf);
    }

    std::string err;
    if (!wb.save("out/条件格式.xlsx", err)) { std::cout << "保存失败: " << err << "\n"; return 1; }
    std::cout << "已生成 out/条件格式.xlsx（" << wb.allCfs()[0].size() << " 条规则）\n";

    // 顺带打印判定结果，便于人工核对
    std::map<std::pair<int,int>, CfStyle> res;
    resolveCfStyles(sh, wb.allCfs()[0], res);
    for (int i = 1; i <= 5; i++) {
        auto it = res.find({1, i});
        std::cout << "  " << names[i-1] << " " << scores[i-1] << " -> ";
        if (it == res.end()) std::cout << "（无）\n";
        else std::cout << "填充=" << (it->second.fillColor.empty() ? "-" : it->second.fillColor)
                       << " 粗体=" << (it->second.bold ? "是" : "否") << "\n";
    }
    return 0;
}
