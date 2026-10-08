// 定义名称（命名区域）测试
//
// =SUM(销售额) 而不是 =SUM(B2:B5)。存在 workbook.xml 的 <definedNames> 里。
//
// 这一组的重点是两处容易做错的地方：
//   1. 查找顺序：LET 作用域 > 定义名称。反了会让 LET(x,...) 被外部持久名遮蔽。
//   2. 名字合法性：像 "A1" 这种名字词法器永远产出 Ref，根本不会被查到 ——
//      必须在写入时拒绝，否则用户定义了一个"看起来成功但永远无效"的名称。
#include "xlsx.hpp"
#include "sheet.hpp"
#include <iostream>

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

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    std::cout << "== 名字合法性 ==\n";
    {
        Workbook wb;
        std::string why;
        OK(wb.addDefinedName("销售额", "Sheet1!$B$2:$B$5", why), "合法名: " + why);
        OK(wb.addDefinedName("_temp", "Sheet1!$A$1", why), "下划线开头: " + why);
        OK(wb.addDefinedName("我的.名称", "Sheet1!$A$1", why), "中文与点: " + why);

        // 必须被拒的几种
        OK(!wb.addDefinedName("", "Sheet1!$A$1", why), "空名被拒: " + why);
        OK(!wb.addDefinedName("1abc", "Sheet1!$A$1", why), "数字开头被拒: " + why);
        OK(!wb.addDefinedName("A1", "Sheet1!$A$1", why), "像单元格地址被拒: " + why);
        OK(!wb.addDefinedName("AB12", "Sheet1!$A$1", why), "AB12 被拒: " + why);
        OK(!wb.addDefinedName("my name", "Sheet1!$A$1", why), "含空格被拒: " + why);
        OK(!wb.addDefinedName("a+b", "Sheet1!$A$1", why), "含运算符被拒: " + why);
        OK(!wb.addDefinedName("TRUE", "Sheet1!$A$1", why), "TRUE 被拒: " + why);
        OK(!wb.addDefinedName("R", "Sheet1!$A$1", why), "单字母 R 被拒: " + why);
        // 空引用目标被拒
        OK(!wb.addDefinedName("空目标", "", why), "空引用被拒: " + why);
    }

    std::cout << "== 同名覆盖 ==\n";
    {
        Workbook wb;
        std::string why;
        wb.addDefinedName("X", "Sheet1!$A$1", why);
        wb.addDefinedName("X", "Sheet1!$A$2", why);       // Excel 语义：重新定义
        EQI(wb.definedNames().size(), 1, "仍只有 1 条");
        const std::string* d = wb.findDefinedName("X");
        OK(d && *d == "Sheet1!$A$2", "值已被覆盖");
        OK(wb.removeDefinedName("X"), "删除成功");
        EQI(wb.definedNames().size(), 0, "删空");
        OK(!wb.removeDefinedName("X"), "重复删除返回 false");
        // 查不到返回 nullptr
        OK(wb.findDefinedName("不存在") == nullptr, "未定义返回 nullptr");
    }

    std::cout << "== 公式里使用命名区域 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("数据");
        // B2:B5 = 10,20,30,40
        for (int i = 0; i < 4; i++) sh.setValue(1, i + 1, Value::num((i + 1) * 10));
        sh.recalc();

        std::string why;
        OK(wb.addDefinedName("销售额", "数据!$B$2:$B$5", why), "定义: " + why);

        // SUM(销售额) = 100
        std::string e = sh.setFormula(3, 0, "SUM(销售额)");
        OK(e.empty(), "公式解析: " + e);
        sh.recalc();
        EQS(sh.display(3, 0), "100", "SUM(销售额)=100");

        // 也支持不带表名（当前表）
        wb.addDefinedName("单价", "$B$2:$B$5", why);
        std::string e2 = sh.setFormula(4, 0, "COUNT(单价)");
        OK(e2.empty(), "COUNT(单价) 解析: " + e2);
        sh.recalc();
        EQS(sh.display(4, 0), "4", "COUNT(单价)=4");

        // 未定义的名字 -> #NAME?
        std::string e3 = sh.setFormula(5, 0, "SUM(不存在的名字)");
        OK(e3.empty(), "未定义名也能解析（求值才报错）");
        sh.recalc();
        EQS(sh.display(5, 0), "#NAME?", "未定义名得 #NAME?");
    }

    std::cout << "== LET 优先级高于定义名称 ==\n";
    {
        // 定义一个名字 x，再在公式里 LET(x, 99, x)。
        // 结果必须是 99（LET 优先），不是定义名称的值。
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("表1");
        sh.setValue(0, 0, Value::num(7));
        sh.recalc();
        std::string why;
        wb.addDefinedName("x", "表1!$A$1", why);          // x -> 7

        std::string e = sh.setFormula(2, 0, "LET(x, 99, x)");
        OK(e.empty(), "LET 解析: " + e);
        sh.recalc();
        EQS(sh.display(2, 0), "99", "LET 遮蔽定义名称（得 99，不是 7）");

        // 单独引用时仍用定义名称
        std::string e2 = sh.setFormula(3, 0, "x");
        OK(e2.empty(), "裸引用解析: " + e2);
        sh.recalc();
        EQS(sh.display(3, 0), "7", "无 LET 时取定义名称 = 7");
    }

    std::cout << "== 常量与公式型定义 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("参数");
        std::string why;
        wb.addDefinedName("税率", "0.13", why);           // 常量
        wb.addDefinedName("基数", "200", why);
        std::string e = sh.setFormula(0, 0, "基数*税率");
        OK(e.empty(), "解析: " + e);
        sh.recalc();
        EQS(sh.display(0, 0), "26", "200*0.13 = 26");

        // 定义文本带前导 '='（Excel 保存时常见）
        wb.addDefinedName("加倍", "=A1*2", why);
        std::string e2 = sh.setFormula(1, 0, "加倍");
        sh.recalc();
        // A1 = 26，所以 加倍 = 52
        EQS(sh.display(1, 0), "52", "带前导 = 的定义也能求值");
    }

    std::cout << "== 循环定义不崩 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("循环");
        std::string why;
        wb.addDefinedName("a", "b", why);
        wb.addDefinedName("b", "a", why);
        std::string e = sh.setFormula(0, 0, "a");
        OK(e.empty(), "循环定义仍能解析");
        sh.recalc();
        // 不崩即可；结果应该是 #NAME?（深度超限）
        OK(true, "循环定义不崩溃，结果=" + sh.display(0, 0));
        OK(wb.warnings().empty(), "无额外告警");
    }

    std::cout << "== xlsx 往返 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("销售数据");
        for (int i = 0; i < 4; i++) sh.setValue(1, i + 1, Value::num((i + 1) * 10));
        sh.recalc();
        std::string why;
        wb.addDefinedName("销售额", "销售数据!$B$2:$B$5", why);
        wb.addDefinedName("税率", "0.13", why);

        std::string err;
        OK(wb.save("/tmp/xl_dn.xlsx", err), "保存: " + err);
        Workbook rb;
        OK(rb.load("/tmp/xl_dn.xlsx", err), "加载: " + err);
        EQI(rb.definedNames().size(), 2, "读回 2 条");
        const std::string* d = rb.findDefinedName("销售额");
        OK(d != nullptr, "销售额 存在");
        if (d) EQS(*d, "销售数据!$B$2:$B$5", "引用文本一致（中文表名）");
        const std::string* t = rb.findDefinedName("税率");
        OK(t && *t == "0.13", "税率 值一致");

        // 读回后公式仍能算：验证定义名称真的接上了求值链
        Sheet& rs = rb.sheet(0);
        std::string e = rs.setFormula(3, 0, "SUM(销售额)");
        OK(e.empty(), "读回后公式解析: " + e);
        rs.recalc();
        EQS(rs.display(3, 0), "100", "读回后 SUM(销售额)=100");
    }

    std::cout << "== 边界 ==\n";
    {
        Workbook wb;
        // 清空
        std::string why;
        wb.addDefinedName("a", "Sheet1!$A$1", why);
        wb.clearDefinedNames();
        EQI(wb.definedNames().size(), 0, "清空生效");

        // 无工作簿的单表：不崩，返回 #NAME?
        Sheet solo;
        solo.setValue(0, 0, Value::num(1));
        std::string e = solo.setFormula(1, 0, "某名字");
        OK(e.empty(), "单表也能解析");
        solo.recalc();
        EQS(solo.display(1, 0), "#NAME?", "无工作簿时得 #NAME?");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
