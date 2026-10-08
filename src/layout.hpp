#pragma once
// 图表布局：把 Chart 的语义（范围、刻度、配色）算成具体几何。
// PNG 与 SVG 两个渲染后端共用同一份布局，避免两处算出不同的图。
#include <string>
#include <vector>
#include "chart.hpp"
#include "value.hpp"

namespace xl {

struct Layout {
    // 绘图区（像素）
    double plotX = 0, plotY = 0, plotW = 0, plotH = 0;
    // 数值范围
    double ymin = 0, ymax = 1;
    double xmin = 0, xmax = 1;         // Scatter 用
    bool isValueAxisX = false;         // Scatter 的 X 是数值轴
    bool swapAxes = false;             // Bar：值走 X 轴，分类走 Y 轴
    std::vector<double> yTicks;
    std::vector<double> xTicks;
    std::vector<std::string> seriesNames;
    std::vector<std::string> seriesColors;
    // 直方图分箱结果
    std::vector<double> binEdges;
    std::vector<double> binCounts;

    double vToY(double v) const {
        if (ymax == ymin) return plotY + plotH / 2;
        double t = (v - ymin) / (ymax - ymin);
        return plotY + plotH - t * plotH;
    }
    double vToX(double v) const {
        if (xmax == xmin) return plotX + plotW / 2;
        double t = (v - xmin) / (xmax - xmin);
        return plotX + t * plotW;
    }
    // 分类索引 -> 中心 X
    double catCenter(int i, int n) const {
        if (n <= 0) return plotX;
        double slot = plotW / n;
        return plotX + slot * (i + 0.5);
    }
};

// nice number 算法：把刻度对齐到 1/2/5 × 10^n
double niceNum(double x, bool round);
void niceScale(double min, double max, int maxTicks,
               double& niceMin, double& niceMax, double& step);

// 计算布局
Layout computeLayout(const Chart& c);
// 数值格式化（轴标签）
std::string fmtTick(double v, double step);

} // namespace xl
