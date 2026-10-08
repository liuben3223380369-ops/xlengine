#pragma once
// ---------------------------------------------------------------------------
// 图表 XML（OOXML DrawingML-Chart）的序列化与反序列化。
//
// 与 xlsx.hpp 的关系：xlsx.cpp 负责把各部件打包进 ZIP，这里只负责
// chart1.xml / drawing1.xml 这两个部件的文本内容。
//
// 支持的类型映射（OOXML 用"图族 + 分组"表达图表类型，与直观分类不同）：
//   柱形/条形  -> c:barChart  + barDir=col|bar          （条形不是 barChart 之外的族）
//   折线       -> c:lineChart
//   面积       -> c:areaChart
//   饼图       -> c:pieChart
//   散点       -> c:scatterChart（X、Y 都走数值轴）
//   直方图     -> c:barChart  + 分箱结果内联（Excel 2016+ 原生直方图另有一套，这里降级）
//
// 读取时若遇到不支持的类型或结构，返回 false 并记录原因，不抛异常。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include "chart.hpp"
#include "image.hpp"

namespace xl {

// 单个图表在表上的完整落盘信息
struct ChartPart {
    Chart chart;
    int sheetIndex = 0;
    std::string title;                   // 图形框架名称，如 "图表 1"
};

// ---- 生成部件文本 ----
// chartXml:    xl/charts/chartN.xml 的内容
std::string buildChartXml(const Chart& c, std::string& err);

// drawingXml:  xl/drawings/drawingN.xml 的内容（引用 rId1 指向 chart）
// 图表与图片共用一个 drawing 部件。图表在前、图片在后，
// rels 里的 rId 也按同样顺序分配（图表 rId1..N，图片 rId N+1..）。
std::string buildDrawingXml(const std::vector<ChartPart>& parts,
                            const std::vector<ImagePart>& images,
                            std::string& err);

// drawing rels: drawingN.xml.rels —— 每个图形框架指向一个 chart
// 生成 drawing 的 rels：先图表后图片，返回总关系数
std::string buildDrawingRels(int chartCount, int imageCount = 0,
                             const std::vector<std::string>* imageTargets = nullptr);

// sheet rels:  sheetN.xml.rels —— 指向 drawing
std::string buildSheetRels(int drawingId);

// ---- 解析 ----
bool parseChartXml(const std::string& xml, Chart& out, std::string& err);

// chartType <-> OOXML 名称
std::string chartTypeToOoxml(ChartType t);
ChartType ooxmlToChartType(const std::string& family, const std::string& barDir,
                           const std::string& grouping, bool& ok);

} // namespace xl
