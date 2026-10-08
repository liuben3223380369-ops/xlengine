// 数据验证测试
//
// 数据验证与条件格式结构相似（规则挂在区域上），但语义相反：
// 条件格式只影响显示，数据验证要**拦住写入**。所以这一组除了验证判定，
// 还要验证"确实拦住了"。
//
// 最容易错的地方：
//   - <dataValidations> 必须排在 <conditionalFormatting> 之后、<drawing> 之前
//   - list / custom 不写 operator 属性
//   - 文本长度按码点数算，中文按字节算会变 3 倍
//   - 整数判定用 trunc 比较，不能用 fmod(d,1)==0
#include "dv.hpp"
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

static DataValidation mkDv(int c0, int r0, int c1, int r1) {
    DataValidation dv;
    dv.rects.push_back({{c0, r0, c1, r1}});
    return dv;
}

int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    std::cout << "== 枚举互转 ==\n";
    {
        EQS(std::string(dvTypeToOoxml(DvType::Whole)), "whole", "整数");
        EQS(std::string(dvTypeToOoxml(DvType::TextLength)), "textLength", "文本长度");
        EQS(std::string(dvOperatorToOoxml(DvOperator::NotBetween)), "notBetween", "不在之间");
        EQS(std::string(dvErrorStyleToOoxml(DvErrorStyle::Warning)), "warning", "警告");
        DvType t; DvOperator o; DvErrorStyle e;
        OK(ooxmlToDvType("decimal", t) && t == DvType::Decimal, "解析 decimal");
        OK(ooxmlToDvOperator("lessThanOrEqual", o) && o == DvOperator::LessThanOrEqual, "解析 <= ");
        OK(ooxmlToDvErrorStyle("information", e) && e == DvErrorStyle::Information, "解析 information");
        OK(!ooxmlToDvType("瞎写", t), "未知类型返回 false");
    }

    std::cout << "== 数值范围（整数） ==\n";
    {
        Sheet sh; sh.setValue(0, 0, Value::num(1)); sh.recalc();
        DataValidation dv = mkDv(0, 0, 0, 4);
        dv.type = DvType::Whole;
        dv.op = DvOperator::Between;
        dv.formula1 = "1"; dv.formula2 = "100";

        OK(checkDataValidation(sh, dv, 0, 0, Value::num(50)).ok, "50 通过");
        OK(checkDataValidation(sh, dv, 0, 0, Value::num(1)).ok, "下界 1 通过");
        OK(checkDataValidation(sh, dv, 0, 0, Value::num(100)).ok, "上界 100 通过");
        OK(!checkDataValidation(sh, dv, 0, 0, Value::num(0)).ok, "0 被拒");
        OK(!checkDataValidation(sh, dv, 0, 0, Value::num(101)).ok, "101 被拒");
        // 关键：非整数必须被拒
        OK(!checkDataValidation(sh, dv, 0, 0, Value::num(50.5)).ok, "50.5 被拒（不是整数）");
    }

    std::cout << "== 数值范围（小数） ==\n";
    {
        Sheet sh; sh.setValue(0, 0, Value::num(1)); sh.recalc();
        DataValidation dv = mkDv(0, 0, 0, 4);
        dv.type = DvType::Decimal;
        dv.op = DvOperator::GreaterThanOrEqual;
        dv.formula1 = "0";
        OK(checkDataValidation(sh, dv, 0, 0, Value::num(0.5)).ok, "0.5 通过");
        OK(checkDataValidation(sh, dv, 0, 0, Value::num(0)).ok, "0 通过");
        OK(!checkDataValidation(sh, dv, 0, 0, Value::num(-0.1)).ok, "-0.1 被拒");
    }

    std::cout << "== 序列（list） ==\n";
    {
        Sheet sh; sh.setValue(0, 0, Value::num(1)); sh.recalc();
        DataValidation dv = mkDv(0, 0, 0, 4);
        dv.type = DvType::List;
        dv.formula1 = "\"是,否,待定\"";
        dv.listItems = splitListFormula(dv.formula1);
        EQI(dv.listItems.size(), 3, "拆出 3 个候选项");
        OK(checkDataValidation(sh, dv, 0, 0, Value::str("是")).ok, "“是”通过");
        OK(checkDataValidation(sh, dv, 0, 0, Value::str("否")).ok, "“否”通过");
        OK(!checkDataValidation(sh, dv, 0, 0, Value::str("也许")).ok, "“也许”被拒");

        // 拆分与拼装
        auto items = splitListFormula("\"a,b,c\"");
        EQI(items.size(), 3, "拆分 3 项");
        EQS(items[0], "a", "第 1 项");
        EQS(joinListFormula({"a","b"}), "\"a,b\"", "拼装带引号");
    }

    std::cout << "== 文本长度 ==\n";
    {
        Sheet sh; sh.setValue(0, 0, Value::num(1)); sh.recalc();
        DataValidation dv = mkDv(0, 0, 0, 4);
        dv.type = DvType::TextLength;
        dv.op = DvOperator::LessThanOrEqual;
        dv.formula1 = "5";
        OK(checkDataValidation(sh, dv, 0, 0, Value::str("abc")).ok, "3 字符通过");
        OK(!checkDataValidation(sh, dv, 0, 0, Value::str("abcdef")).ok, "6 字符被拒");
        // 关键：中文按码点算，3 个汉字 = 3，不是 9 字节
        OK(checkDataValidation(sh, dv, 0, 0, Value::str("一二三")).ok, "3 个汉字按 3 算（非 9）");
        OK(!checkDataValidation(sh, dv, 0, 0, Value::str("一二三四五六")).ok, "6 个汉字被拒");
    }

    std::cout << "== 自定义公式 ==\n";
    {
        // A1:A5 = {1,2,3,4,5}，规则：必须大于 A1 的值（相对左上角，对第 k 格等价于 Ak）
        Sheet sh;
        for (int i = 0; i < 5; i++) sh.setValue(0, i, Value::num((i + 1) * 10));
        for (int i = 0; i < 5; i++) sh.setValue(1, i, Value::num(i + 1));
        sh.recalc();

        DataValidation dv = mkDv(1, 0, 1, 4);
        dv.type = DvType::Custom;
        dv.formula1 = "B1>2";
        OK(checkDataValidation(sh, dv, 1, 3, Value::num(4)).ok, "B4=4 满足 B1>2 平移后的 B4>2");
        OK(!checkDataValidation(sh, dv, 1, 0, Value::num(1)).ok, "B1=1 不满足");
    }

    std::cout << "== 空值与错误值 ==\n";
    {
        Sheet sh; sh.setValue(0, 0, Value::num(1)); sh.recalc();
        DataValidation dv = mkDv(0, 0, 0, 4);
        dv.type = DvType::Whole;
        dv.op = DvOperator::Between;
        dv.formula1 = "1"; dv.formula2 = "100";

        dv.allowBlank = true;
        OK(checkDataValidation(sh, dv, 0, 0, Value::empty()).ok, "allowBlank 时空格放行");
        dv.allowBlank = false;
        OK(!checkDataValidation(sh, dv, 0, 0, Value::empty()).ok, "禁止空时空格被拒");

        OK(!checkDataValidation(sh, dv, 0, 0, Value::error(Err::Div0)).ok, "错误值一律被拒");
    }

    std::cout << "== 错误强度与提示 ==\n";
    {
        Sheet sh; sh.setValue(0, 0, Value::num(1)); sh.recalc();
        DataValidation dv = mkDv(0, 0, 0, 4);
        dv.type = DvType::Whole;
        dv.op = DvOperator::Between;
        dv.formula1 = "1"; dv.formula2 = "10";
        dv.error = "请输入 1 到 10";

        DvVerdict v = checkDataValidation(sh, dv, 0, 0, Value::num(99));
        OK(!v.ok, "越界不通过");
        EQS(v.message, "请输入 1 到 10", "用自定义提示");
        OK(v.stop, "默认 Stop 强度");

        dv.errorStyle = DvErrorStyle::Warning;
        v = checkDataValidation(sh, dv, 0, 0, Value::num(99));
        OK(!v.stop, "Warning 不强制拒绝");
    }

    std::cout << "== 区域命中 ==\n";
    {
        DataValidation dv = mkDv(2, 3, 4, 6);
        OK(dvForCell({dv}, 3, 4) != nullptr, "区域内命中");
        OK(dvForCell({dv}, 1, 4) == nullptr, "区域外不命中");
        OK(dvForCell({dv}, 2, 2) == nullptr, "上方不命中");
    }

    std::cout << "== xlsx 往返 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("录入");
        for (int i = 0; i < 5; i++) sh.setValue(0, i, Value::num(i + 1));

        DataValidation dv = mkDv(0, 0, 0, 4);
        dv.type = DvType::Whole;
        dv.op = DvOperator::Between;
        dv.formula1 = "1"; dv.formula2 = "100";
        dv.errorTitle = "范围错误";
        dv.error = "请输入 1 到 100";
        wb.addDv(0, dv);

        DataValidation lv = mkDv(1, 0, 1, 4);
        lv.type = DvType::List;
        lv.formula1 = "\"是,否\"";
        lv.listItems = splitListFormula(lv.formula1);
        wb.addDv(0, lv);

        std::string err;
        OK(wb.save("/tmp/xl_dv.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_dv.xlsx", err), "加载: " + err);
        EQI(rb.allDvs()[0].size(), 2, "读回 2 条规则");
        const DataValidation& r0 = rb.allDvs()[0][0];
        OK(r0.type == DvType::Whole, "类型 whole");
        OK(r0.op == DvOperator::Between, "运算符 between");
        EQS(r0.formula1, "1", "下界");
        EQS(r0.formula2, "100", "上界");
        EQS(r0.error, "请输入 1 到 100", "错误提示");
        EQS(r0.errorTitle, "范围错误", "错误标题");
        OK(rb.allDvs()[0][1].type == DvType::List, "第二条是 list");
        EQI(rb.allDvs()[0][1].listItems.size(), 2, "list 候选项 2 个");

        // 读回后判定仍生效
        OK(!checkDataValidation(rb.sheet(0), r0, 0, 0, Value::num(200)).ok, "读回后仍拦截 200");
        OK(checkDataValidation(rb.sheet(0), r0, 0, 0, Value::num(50)).ok, "读回后仍放行 50");
    }

    std::cout << "== 边界情形 ==\n";
    {
        Sheet sh; sh.setValue(0, 0, Value::num(1)); sh.recalc();
        // 无规则的格子
        OK(!dvForCell({}, 0, 0), "空列表不命中");
        // type=None 一律放行
        DataValidation dv = mkDv(0, 0, 0, 4);
        dv.type = DvType::None;
        OK(checkDataValidation(sh, dv, 0, 0, Value::str("随便")).ok, "type=none 放行一切");
        // 缺 formula 时不崩
        DataValidation dv2 = mkDv(0, 0, 0, 4);
        dv2.type = DvType::Whole;
        dv2.op = DvOperator::GreaterThan;
        dv2.formula1.clear();
        OK(!checkDataValidation(sh, dv2, 0, 0, Value::num(5)).ok, "缺下界视为不通过");
        // 文本对数值规则
        OK(!checkDataValidation(sh, dv2, 0, 0, Value::str("abc")).ok, "文本不满足数值规则");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
