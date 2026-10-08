#pragma once
// ---------------------------------------------------------------------------
// 图表数据模型。
//
// 为什么必须自绘：Excel 的图表语义（chart XML、分组方式、坐标轴联动、
// 数据标签位置、图例覆盖规则）没有任何开源组件完整覆盖，
// 上一轮实证核验中 IronCalc 源码内 "chart" 关键词为零。
// 所以这里从模型到光栅化全部自己实现。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <cmath>
#include "value.hpp"

namespace xl {

enum class ChartType {
    Column,          // 垂直柱状（簇状）
    Bar,             // 水平条形
    Line,            // 折线
    Area,            // 面积
    Pie,             // 饼图
    Scatter,         // 散点（XY）
    StackedColumn,   // 堆积柱状
    StackedArea,     // 堆积面积
    Histogram        // 直方图（自动分箱）
};

struct DataSeries {
    std::string name;
    std::vector<double> values;          // Y 值（Scatter 时为 Y）
    std::vector<double> xs;              // 仅 Scatter：X 值；为空则用 0..n-1
    std::string color;                   // "#RRGGBB"，空则用默认调色板

    // 数据源区域（写入 xlsx 时用）。
    // 非空则写成 <c:numRef>/<c:strRef> 指向单元格，图表随数据联动；
    // 为空则退化成内联值（numLit），图表与单元格脱钩。
    // 形如 "销售数据!$A$2:$A$5"，绝对引用。
    std::string catRange;                // 分类标签区域
    std::string valRange;                // 数值区域
    std::string xRange;                  // 散点图的 X 区域

    bool hasColor() const { return !color.empty(); }
};

struct Axis {
    std::string title;
    bool autoRange = true;
    double min = 0, max = 0;             // autoRange=false 时生效
    bool showGrid = true;
    bool showLabels = true;
    int tickCount = 5;                   // 期望刻度数，实际按 nice number 调整
};

// 图表锚定到单元格区域（twoCellAnchor）
struct ChartAnchor {
    int fromCol = 0, fromRow = 0;
    int toCol = 8, toRow = 16;           // 默认占 8 列 × 16 行
};

struct Chart {
    ChartType type = ChartType::Column;
    std::string title;
    std::vector<DataSeries> series;
    std::vector<std::string> categories; // X 轴分类标签（Scatter 忽略）
    Axis x, y;
    bool showLegend = true;
    bool showDataLabels = false;
    int width = 900;
    int height = 560;

    ChartAnchor anchor;                  // 在表上的位置（写入 xlsx 用）

    // 直方图分箱数（仅 Histogram 用）
    int bins = 10;

    size_t seriesCount() const { return series.size(); }
    size_t pointCount() const;           // 最长系列的长度
    // 自动范围（含 0 基线对齐、nice 刻度）
    void computeRange(double& ymin, double& ymax, std::vector<double>& ticks) const;
};

// 默认调色板（Excel 2013+ 的 Office 主题色顺序）
const char* paletteColor(int index);

// ---------------------------------------------------------------------------
// 渲染输出
// ---------------------------------------------------------------------------
std::string chartToSVG(const Chart& c);
// 输出 PNG 字节流；SS = 超采样倍数（抗锯齿）
bool chartToPNG(const Chart& c, std::vector<unsigned char>& out, int ss = 3);
// 终端 ASCII 预览（调试用，无图形环境下验证渲染逻辑）
std::string chartToASCII(const Chart& c, int cols = 78, int rows = 24);

// 便捷：从单元格区域直接建图（供 UI / 脚本层调用）
bool addSeriesFromValues(DataSeries& s, const std::vector<Value>& vals);

} // namespace xl
