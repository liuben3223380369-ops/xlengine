// 条件格式测试
//
// 这一组最容易错的地方：
//   - dxfs 与 cellXfs 不能混：条件格式样式走 styles.xml 的 dxfs，
//     普通单元格样式走 cellXfs。写错位置 Excel 会忽略或报损坏。
//   - conditionalFormatting 在 sheetN.xml 里的位置有序列约束：
//     必须在 mergeCells 之后、drawing 之前。
//   - cfRule 只存 dxfId，样式要等 styles.xml 解析完才能回填 ——
//     读文件时若少这一步，规则会"存在但没有样式"。
//   - 公式相对区域左上角书写，判定第 k 格时要按偏移量平移相对引用。
#include "cf.hpp"
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

// 造一张 A1:A5 = {1,2,3,4,5} 的表
static void fillSheet(Sheet& sh) {
    for (int i = 0; i < 5; i++) sh.setValue(0, i, Value::num(i + 1));
    sh.recalc();
}
static ConditionalFormat mkCf(int c0, int r0, int c1, int r1) {
    ConditionalFormat cf;
    cf.rects.push_back({{c0, r0, c1, r1}});
    return cf;
}

int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    // ------------------------------------------------------------------
    std::cout << "== 枚举互转 ==\n";
    {
        EQS(std::string(cfTypeToOoxml(CfType::CellIs)), "cellIs", "cellIs");
        EQS(std::string(cfTypeToOoxml(CfType::Top10)), "top10", "top10");
        EQS(std::string(cfTypeToOoxml(CfType::Duplicate)), "duplicateValues", "重复值");
        EQS(std::string(cfOperatorToOoxml(CfOperator::GreaterThanOrEqual)),
            "greaterThanOrEqual", "大于等于");
        CfType t; CfOperator o;
        OK(ooxmlToCfType("aboveAverage", t) && t == CfType::AboveAverage, "解析 aboveAverage");
        OK(ooxmlToCfOperator("notBetween", o) && o == CfOperator::NotBetween, "解析 notBetween");
        OK(!ooxmlToCfType("dataBar", t), "未支持的 dataBar 返回 false");
    }

    // ------------------------------------------------------------------
    std::cout << "== CellIs：大于 ==\n";
    {
        Sheet sh; fillSheet(sh);
        ConditionalFormat cf = mkCf(0, 0, 0, 4);
        CfRule r;
        r.type = CfType::CellIs;
        r.op = CfOperator::GreaterThan;
        r.formulas = {"3"};
        r.style.fillColor = "FF0000";
        cf.rules.push_back(r);

        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(sh, {cf}, res);
        EQI(res.size(), 2, "只有 4 和 5 命中（共 2 格）");
        OK(res.count({0, 3}) && res[{0, 3}].fillColor == "FF0000", "A4 命中且为红色");
        OK(res.count({0, 4}), "A5 命中");
        OK(!res.count({0, 2}), "A3(3) 不命中（严格大于）");
    }

    // ------------------------------------------------------------------
    std::cout << "== CellIs：各运算符 ==\n";
    {
        struct Case { CfOperator op; std::vector<std::string> f; int want; const char* note; };
        Case cases[] = {
            {CfOperator::Equal,              {"3"}, 1, "等于 3"},
            {CfOperator::NotEqual,           {"3"}, 4, "不等于 3"},
            {CfOperator::GreaterThan,        {"3"}, 2, "大于 3"},
            {CfOperator::LessThan,           {"3"}, 2, "小于 3"},
            {CfOperator::GreaterThanOrEqual, {"3"}, 3, "大于等于 3"},
            {CfOperator::LessThanOrEqual,    {"3"}, 3, "小于等于 3"},
            {CfOperator::Between,            {"2","4"}, 3, "2 到 4 之间"},
            {CfOperator::NotBetween,         {"2","4"}, 2, "不在 2 到 4 之间"},
        };
        for (auto& c : cases) {
            Sheet sh; fillSheet(sh);
            ConditionalFormat cf = mkCf(0, 0, 0, 4);
            CfRule r;
            r.type = CfType::CellIs; r.op = c.op; r.formulas = c.f;
            r.style.bold = true;
            cf.rules.push_back(r);
            std::map<std::pair<int,int>, CfStyle> res;
            resolveCfStyles(sh, {cf}, res);
            EQI(res.size(), c.want, std::string(c.note));
        }
    }

    // ------------------------------------------------------------------
    std::cout << "== Expression：相对引用平移 ==\n";
    {
        // B1:B5 放 {1,2,3,4,5}，A1:A5 放 {10,20,30,40,50}
        // 规则 sqref=B1:B5，公式 "A1>25" —— 对 B4 求值时等价于 "A4>25"
        Sheet sh;
        for (int i = 0; i < 5; i++) {
            sh.setValue(0, i, Value::num((i + 1) * 10));   // A
            sh.setValue(1, i, Value::num(i + 1));          // B
        }
        sh.recalc();
        ConditionalFormat cf = mkCf(1, 0, 1, 4);
        CfRule r;
        r.type = CfType::Expression;
        r.formulas = {"A1>25"};
        r.style.fillColor = "FFFF00";
        cf.rules.push_back(r);

        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(sh, {cf}, res);
        // B3(对应 A3=30)、B4(A4=40)、B5(A5=50) 命中
        EQI(res.size(), 3, "相对引用平移后 3 格命中");
        OK(!res.count({1, 1}), "B2(A2=20) 不命中");
        OK(res.count({1, 2}), "B3(A3=30) 命中");
        OK(res.count({1, 4}), "B5(A5=50) 命中");
    }

    // ------------------------------------------------------------------
    std::cout << "== Top10 ==\n";
    {
        Sheet sh; fillSheet(sh);          // 1..5
        ConditionalFormat cf = mkCf(0, 0, 0, 4);
        CfRule r;
        r.type = CfType::Top10;
        r.rank = 2;                        // 前 2 名
        r.style.bold = true;
        cf.rules.push_back(r);
        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(sh, {cf}, res);
        EQI(res.size(), 2, "前 2 名命中");
        OK(res.count({0, 3}) && res.count({0, 4}), "命中的是 4 和 5");

        // 后 2 名
        ConditionalFormat cf2 = mkCf(0, 0, 0, 4);
        CfRule r2; r2.type = CfType::Top10; r2.rank = 2; r2.bottom = true;
        cf2.rules.push_back(r2);
        res.clear();
        resolveCfStyles(sh, {cf2}, res);
        OK(res.count({0, 0}) && res.count({0, 1}), "后 2 名是 1 和 2");
    }

    // ------------------------------------------------------------------
    std::cout << "== AboveAverage ==\n";
    {
        Sheet sh; fillSheet(sh);          // 1..5，均值 3
        ConditionalFormat cf = mkCf(0, 0, 0, 4);
        CfRule r;
        r.type = CfType::AboveAverage;
        r.style.italic = true;
        cf.rules.push_back(r);
        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(sh, {cf}, res);
        EQI(res.size(), 2, "高于均值(3)的有 2 格：4 和 5");
        OK(!res.count({0, 2}), "等于均值不算高于");

        CfRule r2 = r; r2.above = false;
        ConditionalFormat cf2 = mkCf(0, 0, 0, 4);
        cf2.rules.push_back(r2);
        res.clear();
        resolveCfStyles(sh, {cf2}, res);
        EQI(res.size(), 2, "低于均值的有 2 格：1 和 2");
    }

    // ------------------------------------------------------------------
    std::cout << "== Duplicate / Unique ==\n";
    {
        Sheet sh;
        sh.setValue(0, 0, Value::num(1));
        sh.setValue(0, 1, Value::num(2));
        sh.setValue(0, 2, Value::num(1));
        sh.setValue(0, 3, Value::num(3));
        sh.recalc();

        ConditionalFormat cf = mkCf(0, 0, 0, 3);
        CfRule r; r.type = CfType::Duplicate; r.style.fillColor = "FF0000";
        cf.rules.push_back(r);
        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(sh, {cf}, res);
        EQI(res.size(), 2, "重复值(1)出现 2 次");
        OK(res.count({0, 0}) && res.count({0, 2}), "命中的是第 1 和第 3 格");

        ConditionalFormat cf2 = mkCf(0, 0, 0, 3);
        CfRule r2; r2.type = CfType::Unique;
        cf2.rules.push_back(r2);
        res.clear();
        resolveCfStyles(sh, {cf2}, res);
        EQI(res.size(), 2, "唯一值(2 和 3)共 2 格");
    }

    // ------------------------------------------------------------------
    std::cout << "== 优先级与 stopIfTrue ==\n";
    {
        Sheet sh; fillSheet(sh);
        ConditionalFormat cf = mkCf(0, 0, 0, 4);
        CfRule a; a.type = CfType::CellIs; a.op = CfOperator::GreaterThan;
        a.formulas = {"1"}; a.priority = 1; a.style.fillColor = "0000FF";
        CfRule b; b.type = CfType::CellIs; b.op = CfOperator::GreaterThan;
        b.formulas = {"3"}; b.priority = 2; b.style.fillColor = "FF0000";
        cf.rules.push_back(b); cf.rules.push_back(a);   // 故意乱序入
        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(sh, {cf}, res);
        // 4、5 同时命中两条：priority 小的(1,蓝色)先应用，大的(2,红色)后覆盖
        EQS(res[{0, 4}].fillColor, "FF0000", "低优先级后应用，覆盖为红色");
        EQS(res[{0, 1}].fillColor, "0000FF", "只命中一条时为蓝色");

        // stopIfTrue：高优先级命中后不再看低优先级
        ConditionalFormat cf2 = mkCf(0, 0, 0, 4);
        CfRule c1; c1.type = CfType::CellIs; c1.op = CfOperator::GreaterThan;
        c1.formulas = {"1"}; c1.priority = 1; c1.style.fillColor = "0000FF"; c1.stopIfTrue = true;
        CfRule c2; c2.type = CfType::CellIs; c2.op = CfOperator::GreaterThan;
        c2.formulas = {"3"}; c2.priority = 2; c2.style.fillColor = "FF0000";
        cf2.rules.push_back(c2); cf2.rules.push_back(c1);
        res.clear();
        resolveCfStyles(sh, {cf2}, res);
        EQS(res[{0, 4}].fillColor, "0000FF", "stopIfTrue 后不再被红色覆盖");
    }

    // ------------------------------------------------------------------
    std::cout << "== xlsx 往返 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("条件");
        fillSheet(sh);
        for (int i = 0; i < 5; i++) sh.setValue(1, i, Value::num((i + 1) * 10));

        ConditionalFormat cf;
        cf.rects.push_back({{0, 0, 0, 4}});
        CfRule r;
        r.type = CfType::CellIs;
        r.op = CfOperator::GreaterThan;
        r.formulas = {"3"};
        r.style.fillColor = "FF0000";
        r.style.bold = true;
        cf.rules.push_back(r);
        wb.addCf(0, cf);

        std::string err;
        OK(wb.save("/tmp/xl_cf.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_cf.xlsx", err), "加载: " + err);
        EQI(rb.allCfs().size(), 1, "读回 1 张表的条件格式");
        if (rb.allCfs().empty()) { std::cout << "  FAIL 条件格式未读回\n"; return 1; }
        EQI(rb.allCfs()[0].size(), 1, "读回 1 个条件格式块");
        if (rb.allCfs()[0].empty()) { std::cout << "  FAIL 块为空\n"; return 1; }
        const auto& rcf = rb.allCfs()[0][0];
        EQI(rcf.rules.size(), 1, "读回 1 条规则");
        EQI(rcf.rects.size(), 1, "读回 1 个区域");
        if (!rcf.rules.empty() && !rcf.rects.empty()) {
            OK(rcf.rules[0].type == CfType::CellIs, "类型正确");
            OK(rcf.rules[0].op == CfOperator::GreaterThan, "运算符正确");
            EQS(rcf.rules[0].formulas.empty() ? "" : rcf.rules[0].formulas[0], "3", "公式正确");
            // 关键：样式是通过 dxfId 回填的，这一步漏了就只剩规则没有样式
            EQS(rcf.rules[0].style.fillColor, "FF0000", "dxf 样式已回填");
            OK(rcf.rules[0].style.bold, "粗体已回填");
        }

        // 读回后重新判定，结果应一致
        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(rb.sheet(0), rb.allCfs()[0], res);
        EQI(res.size(), 2, "读回后判定仍为 2 格");
    }

    // ------------------------------------------------------------------
    std::cout << "== 边界情形 ==\n";
    {
        // 空区域 / 空规则不产生结果
        Sheet sh; fillSheet(sh);
        ConditionalFormat empty;
        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(sh, {empty}, res);
        EQI(res.size(), 0, "空块不命中");

        // 区域超出有数据的范围：不崩，只是不命中
        ConditionalFormat cf = mkCf(0, 0, 5, 100);
        CfRule r; r.type = CfType::CellIs; r.op = CfOperator::Equal; r.formulas = {"1"};
        cf.rules.push_back(r);
        res.clear();
        resolveCfStyles(sh, {cf}, res);
        EQI(res.size(), 1, "大区域只命中实际有数据的格子");

        // 空格子不参与 Duplicate
        Sheet sh2;
        sh2.setValue(0, 0, Value::num(7));
        sh2.recalc();
        ConditionalFormat cf2 = mkCf(0, 0, 0, 3);
        CfRule r2; r2.type = CfType::Duplicate;
        cf2.rules.push_back(r2);
        res.clear();
        resolveCfStyles(sh2, {cf2}, res);
        EQI(res.size(), 0, "唯一值不算重复（空格不参与）");

        // 文本不参与数值比较类规则
        Sheet sh3;
        sh3.setValue(0, 0, Value::str("abc"));
        sh3.recalc();
        ConditionalFormat cf3 = mkCf(0, 0, 0, 0);
        CfRule r3; r3.type = CfType::Top10; r3.rank = 1;
        cf3.rules.push_back(r3);
        res.clear();
        resolveCfStyles(sh3, {cf3}, res);
        EQI(res.size(), 0, "文本不参与 Top10");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
