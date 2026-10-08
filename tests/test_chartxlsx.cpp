// 图表写入/读回 xlsx 的测试
// 验证重点：OOXML 结构合规（openpyxl 能读）、类型映射正确、引用与内联值往返一致
#include "xlsx.hpp"
#include "chartxml.hpp"
#include "chart.hpp"
#include "sheet.hpp"
#include "zip.hpp"
#include "xml.hpp"
#include <iostream>
#include <fstream>
#include <cmath>

using namespace xl;

static int P = 0, N = 0;
static void OK(bool cond, const std::string& what) {
    if (cond) P++;
    else { N++; std::cout << "  FAIL " << what << "\n"; }
}
static void EQ(const std::string& got, const std::string& exp, const std::string& what) {
    if (got == exp) P++;
    else { N++; std::cout << "  FAIL " << what << "  期望='" << exp << "' 实际='" << got << "'\n"; }
}
static void EQD(double got, double exp, const std::string& what, double tol = 1e-9) {
    if (std::fabs(got - exp) <= tol) P++;
    else { N++; std::cout << "  FAIL " << what << "  期望=" << exp << " 实际=" << got << "\n"; }
}

static const std::string TMP = "/tmp/xl_chart_test";

int main() {
    // ------------------------------------------------------------------
    std::cout << "== chart XML 类型映射 ==\n";
    {
        EQ(chartTypeToOoxml(ChartType::Column),        "barChart:col:clustered", "柱形");
        EQ(chartTypeToOoxml(ChartType::Bar),           "barChart:bar:clustered", "条形");
        EQ(chartTypeToOoxml(ChartType::StackedColumn), "barChart:col:stacked",   "堆积柱");
        EQ(chartTypeToOoxml(ChartType::Line),          "lineChart::standard",    "折线");
        EQ(chartTypeToOoxml(ChartType::Pie),           "pieChart::",             "饼图");
        EQ(chartTypeToOoxml(ChartType::Scatter),       "scatterChart::",         "散点");
        bool ok = false;
        OK(ooxmlToChartType("barChart", "bar", "clustered", ok) == ChartType::Bar && ok, "反解条形");
        OK(ooxmlToChartType("barChart", "col", "stacked", ok) == ChartType::StackedColumn && ok, "反解堆积柱");
        OK(ooxmlToChartType("areaChart", "", "stacked", ok) == ChartType::StackedArea && ok, "反解堆积面积");
        ok = true;
        ooxmlToChartType("surfaceChart", "", "", ok);
        OK(!ok, "不支持的类型应返回 false");
    }

    // ------------------------------------------------------------------
    std::cout << "== chart XML 生成 ==\n";
    {
        Chart c;
        c.type = ChartType::Column;
        c.title = "测试";
        c.categories = {"A", "B", "C"};
        DataSeries s;
        s.name = "系列1";
        s.values = {1, 2, 3};
        c.series.push_back(s);
        std::string err;
        std::string xml = buildChartXml(c, err);
        OK(err.empty() && !xml.empty(), "生成成功");
        OK(xml.find("<c:barChart>") != std::string::npos, "含 barChart");
        OK(xml.find("<c:barDir val=\"col\"/>") != std::string::npos, "含 barDir=col");
        OK(xml.find("<c:numLit>") != std::string::npos, "内联值用 numLit");
        // XML 必须能被标准解析器接受
        std::string perr;
        XmlNode root = xmlParse(xml, perr);
        OK(perr.empty(), "生成的 XML 可解析");
        OK(root.child("chart") != nullptr, "含 chart 节点");
    }
    {
        // 引用型系列：应输出 numRef 而非 numLit
        Chart c;
        c.type = ChartType::Line;
        DataSeries s;
        s.name = "引用系列";
        s.valRange = "Sheet1!$B$2:$B$5";
        s.catRange = "Sheet1!$A$2:$A$5";
        c.series.push_back(s);
        std::string err;
        std::string xml = buildChartXml(c, err);
        OK(xml.find("<c:numRef>") != std::string::npos, "引用型输出 numRef");
        OK(xml.find("<c:f>Sheet1!$B$2:$B$5</c:f>") != std::string::npos, "含数值引用");
        OK(xml.find("<c:f>Sheet1!$A$2:$A$5</c:f>") != std::string::npos, "含分类引用");
        OK(xml.find("<c:lineChart>") != std::string::npos, "折线用 lineChart");
    }
    {
        // 散点图：xVal / yVal 双数值轴
        Chart c;
        c.type = ChartType::Scatter;
        DataSeries s;
        s.name = "XY";
        s.xs = {1, 2, 3};
        s.values = {4, 5, 6};
        c.series.push_back(s);
        std::string err;
        std::string xml = buildChartXml(c, err);
        OK(xml.find("<c:scatterChart>") != std::string::npos, "散点用 scatterChart");
        OK(xml.find("<c:xVal>") != std::string::npos, "含 xVal");
        OK(xml.find("<c:yVal>") != std::string::npos, "含 yVal");
    }
    {
        // 空图表要报错，不能产出半个 XML
        Chart c;
        std::string err;
        std::string xml = buildChartXml(c, err);
        OK(!err.empty() && xml.empty(), "空图表报错");
    }

    // ------------------------------------------------------------------
    std::cout << "== 写入 xlsx 并读回 ==\n";
    {
        Workbook wb;
        wb.renameSheet(0, "数据");
        Sheet& sh = wb.sheet(0);
        for (int r = 0; r < 3; r++) sh.setValue(0, r, Value::num(r + 1));

        // 内联值图表
        Chart c1;
        c1.type = ChartType::Line;
        c1.title = "内联折线";
        c1.categories = {"一", "二", "三"};
        DataSeries s1; s1.name = "甲"; s1.values = {10, 20, 30};
        DataSeries s2; s2.name = "乙"; s2.values = {15, 25, 35};
        c1.series.push_back(s1); c1.series.push_back(s2);
        c1.anchor.fromCol = 3; c1.anchor.fromRow = 0;
        c1.anchor.toCol = 10; c1.anchor.toRow = 15;
        wb.addChart(0, c1, "折线图");

        // 引用型图表
        Chart c2;
        c2.type = ChartType::Pie;
        c2.title = "饼图";
        DataSeries s3; s3.name = "占比"; s3.valRange = "数据!$A$1:$A$3";
        c2.series.push_back(s3);
        c2.anchor.fromCol = 3; c2.anchor.fromRow = 17;
        c2.anchor.toCol = 10; c2.anchor.toRow = 32;
        wb.addChart(0, c2, "饼图");

        std::string err;
        std::string path = TMP + ".xlsx";
        OK(wb.save(path, err), "保存: " + err);
        OK(wb.chartCount(0) == 2, "两个图表");

        // 检查部件齐全（这是 Excel 能否打开的关键）
        ZipReader zr;
        OK(zr.open(path, err), "ZIP 打开");
        OK(zr.has("xl/charts/chart1.xml"), "有 chart1");
        OK(zr.has("xl/charts/chart2.xml"), "有 chart2");
        OK(zr.has("xl/drawings/drawing1.xml"), "有 drawing1");
        OK(zr.has("xl/drawings/_rels/drawing1.xml.rels"), "有 drawing rels");
        OK(zr.has("xl/worksheets/_rels/sheet1.xml.rels"), "有 sheet rels");
        {
            std::string ct;
            zr.readText("[Content_Types].xml", ct);
            OK(ct.find("drawingml.chart+xml") != std::string::npos, "ContentTypes 含 chart");
            OK(ct.find("drawing+xml") != std::string::npos, "ContentTypes 含 drawing");
        }
        {
            std::string sx;
            zr.readText("xl/worksheets/sheet1.xml", sx);
            OK(sx.find("<drawing r:id=\"rId1\"/>") != std::string::npos, "sheet 引用 drawing");
            OK(sx.find("xmlns:r=") != std::string::npos, "sheet 声明 r 命名空间");
        }

        // 读回
        Workbook wb2;
        OK(wb2.load(path, err), "加载: " + err);
        for (auto& w : wb2.warnings()) std::cout << "    warn: " << w << "\n";
        OK(wb2.chartCount(0) == 2, "读回两个图表");

        if (wb2.chartCount(0) >= 1) {
            Chart& r1 = wb2.chartsOf(0)[0];
            OK(r1.type == ChartType::Line, "类型：折线");
            EQ(r1.title, "内联折线", "标题往返");
            OK(r1.series.size() == 2, "两个系列");
            if (r1.series.size() == 2) {
                EQ(r1.series[0].name, "甲", "系列1名字");
                EQ(r1.series[1].name, "乙", "系列2名字");
                OK(r1.series[0].values.size() == 3, "系列1三个点");
                if (r1.series[0].values.size() == 3) {
                    EQD(r1.series[0].values[0], 10, "值 10");
                    EQD(r1.series[0].values[1], 20, "值 20");
                    EQD(r1.series[0].values[2], 30, "值 30");
                }
                EQD(r1.series[1].values[2], 35, "系列2 值 35");
            }
            OK(r1.categories.size() == 3, "三个分类");
            if (r1.categories.size() == 3) EQ(r1.categories[0], "一", "分类文本");
        }
        if (wb2.chartCount(0) >= 2) {
            Chart& r2 = wb2.chartsOf(0)[1];
            OK(r2.type == ChartType::Pie, "类型：饼图");
            EQ(r2.title, "饼图", "饼图标题");
            OK(r2.series.size() == 1, "一个系列");
            if (!r2.series.empty())
                EQ(r2.series[0].valRange, "数据!$A$1:$A$3", "引用型区间往返");
        }
    }

    // ------------------------------------------------------------------
    std::cout << "== 回归：无图表文件不受影响 ==\n";
    {
        Workbook wb;
        wb.sheet(0).setValue(0, 0, Value::num(42));
        std::string err, path = TMP + "_nochart.xlsx";
        OK(wb.save(path, err), "保存无图表文件");
        ZipReader zr;
        zr.open(path, err);
        OK(!zr.has("xl/charts/chart1.xml"), "不生成 chart 部件");
        OK(!zr.has("xl/drawings/drawing1.xml"), "不生成 drawing 部件");
        std::string ct;
        zr.readText("[Content_Types].xml", ct);
        OK(ct.find("chart") == std::string::npos, "ContentTypes 不含 chart");
        Workbook wb2;
        OK(wb2.load(path, err), "加载无图表文件");
        OK(wb2.chartCount(0) == 0, "读回无图表");
        EQ(wb2.sheet(0).display(0, 0), "42", "数据仍正确");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
