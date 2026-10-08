// 样式与合并单元格测试
//
// 这里最容易错、也最值得测的点：
//   - xf 必须由"格式码 + 样式"组合编号。分别编号的话，
//     一个格子设了格式又设了样式，其中一个会被静默覆盖。
//   - fills 的前两个被 OOXML 占死（none / gray125），自定义填充从索引 2 起。
//     从 1 起会导致背景色整体错位一格。
//   - 合并区域里只有左上角存数据；被吞掉的格子不该写内容。
#include "style.hpp"
#include "xlsx.hpp"
#include "sheet.hpp"
#include <iostream>

using namespace xl;

static int P = 0, N = 0;
static void EQS(const std::string& g, const std::string& e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望='" << e << "' 实际='" << g << "'\n"; }
}
static void EQI(long long g, long long e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望=" << e << " 实际=" << g << "\n"; }
}
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}

int main() {
    // ------------------------------------------------------------------
    std::cout << "== 颜色与枚举 ==\n";
    {
        EQS(normalizeColor("#FF0000"), "FF0000", "带 # 的十六进制");
        EQS(normalizeColor("ff0000"), "FF0000", "小写转大写");
        EQS(normalizeColor("F00"), "FF0000", "三位简写");
        EQS(normalizeColor("FFFF0000"), "FF0000", "ARGB 去掉 alpha");
        EQS(normalizeColor("red"), "FF0000", "颜色名");
        EQS(normalizeColor("Red"), "FF0000", "颜色名大小写不敏感");
        EQS(normalizeColor("不是颜色"), "", "无法识别返回空");

        EQS(std::string(hAlignToOoxml(HAlign::Center)), "center", "水平居中");
        EQS(std::string(hAlignToOoxml(HAlign::General)), "general", "常规对齐");
        EQS(std::string(vAlignToOoxml(VAlign::Top)), "top", "垂直靠上");
        EQS(std::string(borderToOoxml(BorderStyle::Thin)), "thin", "细边框");

        HAlign ha; VAlign va; BorderStyle bs;
        OK(ooxmlToHAlign("center", ha) && ha == HAlign::Center, "解析 center");
        OK(ooxmlToVAlign("top", va) && va == VAlign::Top, "解析 top");
        OK(ooxmlToBorder("medium", bs) && bs == BorderStyle::Medium, "解析 medium");
        OK(!ooxmlToHAlign("瞎写", ha), "非法对齐返回 false");
    }

    // ------------------------------------------------------------------
    std::cout << "== CellStyle 组合键 ==\n";
    {
        CellStyle a, b;
        OK(a.empty(), "默认样式为空");
        OK(a.key() == b.key(), "两个默认样式键相同");

        a.bold = true;
        OK(a.key() != b.key(), "加粗后键不同");
        OK(!a.empty(), "加粗后非空");

        CellStyle c; c.bold = true;
        OK(a.key() == c.key(), "同样加粗的键相同");

        CellStyle d; d.bold = true; d.fillColor = "FF0000";
        OK(a.key() != d.key(), "加背景色后键不同");

        CellStyle e; e.fontSize = 14;
        CellStyle f; f.fontSize = 12;
        OK(e.key() != f.key(), "字号不同键不同");
    }

    // ------------------------------------------------------------------
    std::cout << "== 样式往返 xlsx ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("样式");

        CellStyle boldRed;
        boldRed.bold = true;
        boldRed.fontColor = "FF0000";
        boldRed.hAlign = HAlign::Center;
        sh.setValue(0, 0, Value::str("标题"));
        sh.setStyle(0, 0, boldRed);

        CellStyle filled;
        filled.fillColor = "FFFF00";
        filled.border = BorderStyle::Thin;
        sh.setValue(1, 0, Value::num(42));
        sh.setStyle(1, 0, filled);

        // 同时有格式码和样式 —— 这是组合编号的关键用例
        CellStyle rightAligned;
        rightAligned.hAlign = HAlign::Right;
        sh.setValue(2, 0, Value::num(1234.5678));
        sh.setNumFmt(2, 0, "#,##0.00");
        sh.setStyle(2, 0, rightAligned);

        sh.recalc();
        EQS(sh.display(2, 0), "1,234.57", "格式与样式并存时显示仍正确");

        std::string err;
        OK(wb.save("/tmp/xl_style.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_style.xlsx", err), "加载: " + err);
        Sheet& rs = rb.sheet(0);

        CellStyle g0 = rs.styleAt(0, 0);
        OK(g0.bold, "粗体往返");
        EQS(g0.fontColor, "FF0000", "字体颜色往返");
        OK(g0.hAlign == HAlign::Center, "水平居中往返");

        CellStyle g1 = rs.styleAt(1, 0);
        EQS(g1.fillColor, "FFFF00", "填充色往返");
        OK(g1.border == BorderStyle::Thin, "边框往返");

        // 关键：格式码和样式都要在，不能互相覆盖
        CellStyle g2 = rs.styleAt(2, 0);
        OK(g2.hAlign == HAlign::Right, "右对齐往返（样式没被格式覆盖）");
        EQS(rs.numFmtAt(2, 0), "#,##0.00", "格式码往返（格式没被样式覆盖）");
        EQS(rs.display(2, 0), "1,234.57", "重载后显示正确");
    }

    // ------------------------------------------------------------------
    std::cout << "== 样式去重 ==\n";
    {
        // 相同样式的多个格子应共用一个 xf，而不是每个格子一个
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        CellStyle st; st.bold = true;
        for (int i = 0; i < 50; i++) {
            sh.setValue(0, i, Value::num(i));
            sh.setStyle(0, i, st);
        }
        sh.recalc();
        std::string err;
        OK(wb.save("/tmp/xl_dedup.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_dedup.xlsx", err), "加载: " + err);
        OK(rb.sheet(0).styleAt(0, 49).bold, "第 50 个格子仍有粗体");
    }

    // ------------------------------------------------------------------
    std::cout << "== 合并单元格 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("合并");
        sh.setValue(0, 0, Value::str("季度汇总"));
        sh.setValue(0, 2, Value::num(1));
        sh.recalc();

        wb.addMerge(0, 0, 0, 3, 0);      // A1:D1
        OK(wb.isMergedAway(0, 1, 0), "B1 被吞掉");
        OK(wb.isMergedAway(0, 3, 0), "D1 被吞掉");
        OK(!wb.isMergedAway(0, 0, 0), "A1 是左上角，不被吞");
        OK(!wb.isMergedAway(0, 0, 2), "第 3 行不在合并区内");

        auto span = wb.mergeSpan(0, 1, 0);
        EQI(span.first, 4, "合并宽度 4");
        EQI(span.second, 1, "合并高度 1");
        auto span2 = wb.mergeSpan(0, 9, 9);
        EQI(span2.first, 1, "非合并区宽度 1");

        std::string err;
        OK(wb.save("/tmp/xl_merge.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_merge.xlsx", err), "加载: " + err);
        OK(rb.isMergedAway(0, 1, 0), "重载后 B1 仍被吞");
        OK(!rb.isMergedAway(0, 0, 0), "重载后 A1 仍是左上角");
        auto rs = rb.mergeSpan(0, 2, 0);
        EQI(rs.first, 4, "重载后合并宽度 4");
        EQI(rb.allMerges().size(), 1, "一张表有合并");
        EQI(rb.allMerges()[0].size(), 1, "该表有 1 个合并区域");
    }

    // ------------------------------------------------------------------
    std::cout << "== 合并的边界情形 ==\n";
    {
        Workbook wb;
        wb.addMerge(0, 3, 0, 0, 0);       // 反向拖动：A1:D1
        OK(wb.isMergedAway(0, 1, 0), "反向选区被规范化");
        EQI(wb.mergeSpan(0, 0, 0).first, 4, "反向选区宽度仍为 4");

        Workbook wb2;
        wb2.addMerge(0, 0, 0, 0, 0);      // 1x1 不算合并
        EQI(wb2.allMerges()[0].size(), 0, "1x1 不产生合并");

        // 重叠区域：后加的应替换掉相交的旧区域
        Workbook wb3;
        wb3.addMerge(0, 0, 0, 2, 0);      // A1:C1
        wb3.addMerge(0, 1, 0, 4, 0);      // B1:E1，与前者相交
        EQI(wb3.allMerges()[0].size(), 1, "重叠的旧区域被清掉");
        EQI(wb3.mergeSpan(0, 0, 0).first, 1, "A1 已不在合并区内");
        EQI(wb3.mergeSpan(0, 2, 0).first, 4, "C1 属于新的 B1:E1");

        Workbook wb4;
        wb4.addMerge(0, 0, 0, 1, 1);
        EQI(wb4.allMerges()[0].size(), 1, "2x2 产生合并");
        wb4.clearMerges(0);
        EQI(wb4.allMerges()[0].size(), 0, "clearMerges 生效");

        // 只加过 1x1 的表也要能安全查询（不能越界）
        Workbook wb5;
        wb5.addMerge(0, 0, 0, 0, 0);
        EQI(wb5.allMerges()[0].size(), 0, "只加过 1x1 时列表为空且可安全访问");
        OK(!wb5.isMergedAway(0, 0, 0), "1x1 不吞掉任何格子");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
