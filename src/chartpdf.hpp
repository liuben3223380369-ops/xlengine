#pragma once
// ---------------------------------------------------------------------------
// 图表 -> PDF（矢量）
//
// 与 PNG 后端共用 computeLayout()，保证两个后端画出的几何完全一致；
// 区别只在于输出：PDF 写的是矢量指令，放大不失真、文字可选可搜。
//
// 坐标系差异是这里唯一的坑：Canvas 的 Y 向下、原点在左上，
// PDF 的 Y 向上、原点在左下。PdfWriter 内部已做翻转，因此调用层
// 一律按"屏幕坐标（左上原点）"传参即可。
#include <string>
#include "chart.hpp"
#include "pdf.hpp"

namespace xl {

struct ChartPdfOptions {
    double widthPt = 0;      // 0 = 用 c.width / c.height 按 96dpi 换算
    double heightPt = 0;
    double margin = 24;
    bool showTitle = true;
    std::string fontPath;
};

bool chartToPdf(const Chart& c, const std::string& outPath,
                const ChartPdfOptions& opt, std::string& err);

} // namespace xl
