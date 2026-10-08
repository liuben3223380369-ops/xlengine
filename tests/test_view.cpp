// 视图属性测试：列宽、行高、冻结窗格、自动筛选
//
// 这类属性的特点是"丢了不会报错"——文件照样能打开、自己的 reader 照样能读回来。
// 只有用户在 Excel 里打开时才会发现：列宽全没了、表头不再冻结、筛选箭头消失了。
// 所以这一组必须靠 openpyxl 验证真实写进了文件。
//
// 关键约束：CT_Worksheet 的序列
//   sheetViews → cols → sheetData → autoFilter → mergeCells → ...
// 顺序错了 Excel 直接报文件损坏。
#include "view.hpp"
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
static void EQD(double g, double e, const std::string& what) {
    if (std::fabs(g - e) < 1e-6) P++;
    else { N++; std::cout << "  FAIL " << what << " 期望=" << e << " 实际=" << g << "\n"; }
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    std::cout << "== 冻结窗格的派生属性 ==\n";
    {
        // 冻结 1 列 2 行 -> topLeftCell 是 B3
        EQS(freezeTopLeftCell(1, 2), "B3", "冻结 1列2行 的 topLeftCell");
        EQS(freezeTopLeftCell(0, 1), "A2", "只冻 1 行");
        EQS(freezeTopLeftCell(3, 0), "D1", "只冻 3 列");
        // activePane 取决于冻的是行还是列
        EQS(std::string(freezeActivePane(1, 2)), "bottomRight", "行列都冻");
        EQS(std::string(freezeActivePane(2, 0)), "topRight", "只冻列");
        EQS(std::string(freezeActivePane(0, 3)), "bottomLeft", "只冻行");
    }

    std::cout << "== 片段生成 ==\n";
    {
        FreezePane f; f.enabled = true; f.frozenCols = 1; f.frozenRows = 2;
        std::string xml = buildSheetViewsXml(f);
        OK(xml.find("state=\"frozen\"") != std::string::npos, "含 state=frozen");
        OK(xml.find("xSplit=\"1\"") != std::string::npos, "含 xSplit");
        OK(xml.find("ySplit=\"2\"") != std::string::npos, "含 ySplit");
        OK(xml.find("topLeftCell=\"B3\"") != std::string::npos, "含 topLeftCell");

        // 只冻行时不该出现 xSplit
        FreezePane r; r.enabled = true; r.frozenRows = 3;
        std::string x2 = buildSheetViewsXml(r);
        OK(x2.find("xSplit") == std::string::npos, "只冻行时不写 xSplit");
        OK(x2.find("ySplit=\"3\"") != std::string::npos, "只冻行时写 ySplit");

        // 未启用时返回空串
        FreezePane off;
        EQS(buildSheetViewsXml(off), "", "未启用返回空串");

        // cols：相邻同宽应合并成一个 <col min max>
        std::map<int, double> w;
        w[0] = 12.5; w[1] = 12.5; w[2] = 12.5; w[4] = 20;
        std::string cx = buildColsXml(w);
        OK(cx.find("min=\"1\" max=\"3\"") != std::string::npos, "相邻同宽合并为 1..3");
        OK(cx.find("min=\"5\" max=\"5\"") != std::string::npos, "第 5 列单独一段");
        OK(cx.find("customWidth=\"1\"") != std::string::npos, "带 customWidth");
        EQS(buildColsXml({}), "", "空宽度返回空串");

        AutoFilter af; af.enabled = true; af.c0 = 0; af.r0 = 0; af.c1 = 2; af.r1 = 9;
        EQS(buildAutoFilterXml(af), "<autoFilter ref=\"A1:C10\"/>\n", "筛选区域文本");
        EQS(buildAutoFilterXml(AutoFilter()), "", "未启用返回空串");
    }

    std::cout << "== xlsx 往返 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("视图");
        for (int i = 0; i < 3; i++) {
            sh.setValue(0, i, Value::str(i == 0 ? "姓名" : "数据"));
            sh.setValue(1, i, Value::num(i + 1));
        }
        sh.recalc();

        wb.setColWidth(0, 0, 18.5);
        wb.setColWidth(0, 1, 10);
        wb.setRowHeight(0, 0, 28);
        wb.setFreeze(0, 0, 1);              // 冻结首行
        wb.setAutoFilter(0, 0, 0, 2, 2);    // A1:C3 筛选

        std::string err;
        OK(wb.save("/tmp/xl_view.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_view.xlsx", err), "加载: " + err);
        const SheetLayout& L = rb.layout(0);
        EQD(L.colWidths.count(0) ? L.colWidths.at(0) : -1, 18.5, "第 1 列宽");
        EQD(L.colWidths.count(1) ? L.colWidths.at(1) : -1, 10, "第 2 列宽");
        EQD(L.rowHeights.count(0) ? L.rowHeights.at(0) : -1, 28, "首行行高");
        OK(L.freeze.enabled, "冻结已读回");
        EQI(L.freeze.frozenRows, 1, "冻结 1 行");
        EQI(L.freeze.frozenCols, 0, "未冻结列");
        OK(L.filter.enabled, "筛选已读回");
        EQI(L.filter.c1, 2, "筛选到 C 列");
        EQI(L.filter.r1, 2, "筛选到第 3 行");
    }

    std::cout << "== 冻结列与行同时存在 ==\n";
    {
        Workbook wb;
        wb.sheet(0).setValue(0, 0, Value::num(1));
        wb.setFreeze(0, 2, 3);
        std::string err;
        OK(wb.save("/tmp/xl_view2.xlsx", err), "保存: " + err);
        Workbook rb; OK(rb.load("/tmp/xl_view2.xlsx", err), "加载: " + err);
        EQI(rb.layout(0).freeze.frozenCols, 2, "冻结 2 列");
        EQI(rb.layout(0).freeze.frozenRows, 3, "冻结 3 行");
    }

    std::cout << "== 边界与非法输入 ==\n";
    {
        Workbook wb;
        // 负数宽度应被夹到正值（0 或负会让 Excel 把列压成不可见）
        wb.setColWidth(0, 0, -5);
        EQD(wb.layout(0).colWidths.at(0), 0.01, "负宽度被夹为正");
        // 越界列/行不崩
        wb.setColWidth(0, -1, 10);
        wb.setRowHeight(0, -3, 10);
        OK(true, "越界坐标不崩");
        // 关闭冻结：两个都设为 0 时 enabled 应为 false
        wb.setFreeze(0, 5, 5);
        OK(wb.layout(0).freeze.enabled, "设了就启用");
        wb.setFreeze(0, 0, 0);
        OK(!wb.layout(0).freeze.enabled, "全 0 时停用");
        // 反向拖动设定筛选区域应自动规整
        wb.setAutoFilter(0, 5, 8, 2, 3);
        EQI(wb.layout(0).filter.c0, 2, "筛选左列已规整");
        EQI(wb.layout(0).filter.c1, 5, "筛选右列已规整");
        wb.clearAutoFilter(0);
        OK(!wb.layout(0).filter.enabled, "清除筛选");

        // 空 layout 不产生任何片段（避免写出空的 <cols/>）
        SheetLayout empty;
        OK(empty.empty(), "空布局判定");
        EQS(buildColsXml(empty.colWidths), "", "空布局不产生 cols");
        EQS(buildSheetViewsXml(empty.freeze), "", "空布局不产生 sheetViews");
        EQS(buildAutoFilterXml(empty.filter), "", "空布局不产生 autoFilter");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
