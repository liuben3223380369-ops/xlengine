#include "chartpdf.hpp"
#include "layout.hpp"
#include "value.hpp"
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>

// M_PI 是 POSIX 扩展，不是 C++ 标准：MinGW 在 -std=c++17（严格 ANSI）下
// 不定义它，于是 Windows 构建会报 "M_PI was not declared in this scope"。
// 这里补齐，用 #ifndef 保护以免与平台自带的重复定义。
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


namespace xl {

namespace {

// 与 render.cpp 里的 fmtNum 相同：去掉多余的 .0，保留有限位小数
static std::string fmtNum(double v) {
    if (!std::isfinite(v)) return std::string();
    std::ostringstream o;
    if (std::fabs(v - std::round(v)) < 1e-9) o << (long long)std::round(v);
    else o << std::fixed << std::setprecision(2) << v;
    return o.str();
}

PdfColor hexCol(const std::string& s) {
    unsigned int v = 0xFFFFFF;
    if (s.size() >= 7 && s[0] == '#') {
        try { v = std::stoul(s.substr(1), nullptr, 16); } catch (...) {}
    }
    return PdfColor::rgb8((uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF));
}

}

bool chartToPdf(const Chart& c, const std::string& outPath,
                const ChartPdfOptions& opt, std::string& err) {
    Layout L = computeLayout(c);

    // 画布尺寸 -> PDF 点（96dpi 换算，与 PNG 后端保持 1:1 的比例关系）
    double sx = 1.0, sy = 1.0;
    double pw = opt.widthPt  > 0 ? opt.widthPt  : c.width  * 72.0 / 96.0;
    double ph = opt.heightPt > 0 ? opt.heightPt : c.height * 72.0 / 96.0;
    if (opt.widthPt > 0 && c.width > 0)  sx = pw / (c.width  * 72.0 / 96.0);
    if (opt.heightPt > 0 && c.height > 0) sy = ph / (c.height * 72.0 / 96.0);
    // 等比：布局是按画布像素算的，非等比缩放会把圆画成椭圆
    double k = std::min(sx > 0 ? sx : 1.0, sy > 0 ? sy : 1.0);
    double ox = opt.margin, oy = opt.margin;

    PdfWriter w;
    w.setPageSize(pw + opt.margin * 2, ph + opt.margin * 2);
    w.setMargin(opt.margin);

    std::string font = "F1";
    if (!opt.fontPath.empty()) {
        std::string e2;
        if (!w.embedFont(opt.fontPath, font, e2)) { err = "字体嵌入失败: " + e2; return false; }
    }
    w.newPage();

    // 画布像素 -> PDF 页面坐标（左上原点）
    auto X = [&](double x) { return ox + x * k * 72.0 / 96.0; };
    auto Y = [&](double y) { return oy + y * k * 72.0 / 96.0; };
    auto S = [&](double px) { return px * k * 72.0 / 96.0; };

    // 居中文本（屏幕坐标：给的是中心 x 与顶部 y）
    auto centerText = [&](double cx, double topY, const std::string& s, double size, PdfColor col) {
        double tw = w.textWidth(s, size, font);
        w.text(X(cx) - tw / 2, Y(topY), s, size, font, col);
    };
    auto rightText = [&](double rx, double topY, const std::string& s, double size, PdfColor col) {
        double tw = w.textWidth(s, size, font);
        w.text(X(rx) - tw, Y(topY), s, size, font, col);
    };

    PdfColor grid = hexCol("#E0E0E0");
    PdfColor axis = hexCol("#808080");
    PdfColor labelC = hexCol("#333333");
    PdfColor white = PdfColor::white();

    auto colOf = [&](size_t s) {
        return hexCol((s < L.seriesColors.size()) ? L.seriesColors[s] : paletteColor((int)s));
    };

    // ---- 标题 ----
    if (opt.showTitle && !c.title.empty()) {
        double size = S(19);
        double tw = w.textWidth(c.title, size, font);
        w.text(X(c.width / 2.0) - tw / 2, Y(30) - size * 0.75, c.title, size, font, labelC);
    }

    // ---- 饼图 ----
    if (c.type == ChartType::Pie) {
        std::vector<double> vals;
        if (!c.series.empty())
            for (double v : c.series[0].values) if (std::isfinite(v) && v > 0) vals.push_back(v);
        double total = 0; for (double v : vals) total += v;
        if (total > 0) {
            double cx = X(L.plotX + L.plotW / 2), cy = Y(L.plotY + L.plotH / 2);
            double r = S(std::min(L.plotW, L.plotH) / 2 * 0.86);
            double a = -M_PI / 2;
            for (size_t i = 0; i < vals.size(); i++) {
                double frac = vals[i] / total;
                double a1 = a + frac * 2 * M_PI;
                w.pieWedge(cx, cy, r, a, a1, colOf(i), white, S(1.5));
                // 百分比标签：放在扇形中心线上 62% 处
                // 标签位置：从圆心沿扇形中心线走到 62% 半径处（画布坐标）
                double mid = (a + a1) / 2;
                double rCanvas = std::min(L.plotW, L.plotH) / 2 * 0.86 * 0.62;
                std::ostringstream os;
                os << std::fixed << std::setprecision(0) << (frac * 100) << "%";
                centerText(L.plotX + L.plotW / 2 + rCanvas * std::cos(mid),
                           L.plotY + L.plotH / 2 + rCanvas * std::sin(mid),
                           os.str(), S(12), white);
                a = a1;
            }
            if (c.showLegend) {
                double ly = L.plotY + 12;
                for (size_t i = 0; i < vals.size(); i++) {
                    std::string nm = (i < c.categories.size()) ? c.categories[i]
                                                               : ("S" + std::to_string(i + 1));
                    w.rectBoth(X(L.plotX + L.plotW + 6), Y(ly), S(11), S(11), colOf(i), axis, 0.4);
                    w.text(X(L.plotX + L.plotW + 21), Y(ly + 10) - S(12) * 0.75, nm, S(12), font, labelC);
                    ly += 16;
                }
            }
        }
        return w.save(outPath, err);
    }

    // ---- 网格 ----
    if (c.y.showGrid)
        for (double t : L.yTicks) {
            double y = L.vToY(t);
            if (y < L.plotY - 0.5 || y > L.plotY + L.plotH + 0.5) continue;
            w.line(X(L.plotX), Y(y), X(L.plotX + L.plotW), Y(y), grid, S(1));
        }
    // ---- 轴 ----
    w.line(X(L.plotX), Y(L.plotY), X(L.plotX), Y(L.plotY + L.plotH), axis, S(1.2));
    w.line(X(L.plotX), Y(L.plotY + L.plotH), X(L.plotX + L.plotW), Y(L.plotY + L.plotH), axis, S(1.2));

    // ---- Y 刻度 ----
    if (c.y.showLabels) {
        double step = L.yTicks.size() > 1 ? (L.yTicks[1] - L.yTicks[0]) : 1.0;
        for (double t : L.yTicks) {
            double y = L.vToY(t);
            if (y < L.plotY - 1 || y > L.plotY + L.plotH + 1) continue;
            rightText(L.plotX - 8, y + 4 - S(12) * 0.75, fmtTick(t, step), S(12), labelC);
        }
    }
    // ---- X 标签 ----
    if (c.x.showLabels) {
        if (L.isValueAxisX && c.type != ChartType::Histogram) {
            double step = L.xTicks.size() > 1 ? (L.xTicks[1] - L.xTicks[0]) : 1.0;
            for (double t : L.xTicks) {
                double x = L.vToX(t);
                if (x < L.plotX - 1 || x > L.plotX + L.plotW + 1) continue;
                centerText(x, L.plotY + L.plotH + 20 - S(12) * 0.75, fmtTick(t, step), S(12), labelC);
            }
        } else {
            size_t n = (c.type == ChartType::Histogram && !L.binCounts.empty())
                         ? L.binCounts.size() : c.pointCount();
            if (n == 0) n = c.categories.size();
            for (size_t i = 0; i < n; i++) {
                std::string s = (c.type == ChartType::Histogram)
                    ? fmtTick(L.binEdges[i], L.binEdges[1] - L.binEdges[0])
                    : (i < c.categories.size() ? c.categories[i] : std::to_string(i + 1));
                centerText(L.catCenter((int)i, (int)n), L.plotY + L.plotH + 20 - S(12) * 0.75,
                           s, S(12), labelC);
            }
        }
    }
    if (!c.y.title.empty())
        w.text(X(10), Y(L.plotY - 10) - S(13) * 0.75, c.y.title, S(13), font, labelC);
    if (!c.x.title.empty())
        centerText(L.plotX + L.plotW / 2, L.plotY + L.plotH + 44 - S(13) * 0.75,
                   c.x.title, S(13), labelC);

    // ---- 系列 ----
    size_t n = c.pointCount();
    size_t ns = c.series.size();

    if (c.type == ChartType::Column || c.type == ChartType::StackedColumn) {
        bool stacked = (c.type == ChartType::StackedColumn);
        double slot = L.plotW / std::max((size_t)1, n);
        double inner = slot * 0.82;
        double bw = stacked ? inner : inner / std::max((size_t)1, ns);
        for (size_t i = 0; i < n; i++) {
            double base = L.catCenter((int)i, (int)n) - inner / 2;
            double acc = 0;
            for (size_t s = 0; s < ns; s++) {
                if (i >= c.series[s].values.size()) continue;
                double v = c.series[s].values[i];
                if (!std::isfinite(v)) continue;
                double x, top, bot;
                if (stacked) {
                    x = base;
                    double y0 = L.vToY(acc), y1 = L.vToY(acc + v);
                    top = std::min(y0, y1); bot = std::max(y0, y1);
                    acc += v;
                } else {
                    x = base + s * bw;
                    double y0 = L.vToY(v), y1 = L.vToY(0);
                    top = std::min(y0, y1); bot = std::max(y0, y1);
                }
                double h = std::max(1.0, bot - top);
                w.fillRect(X(x), Y(top), S(bw - 1), S(h), colOf(s));
                if (c.showDataLabels)
                    centerText(x + bw / 2, top - 6 - S(11) * 0.75, fmtNum(v), S(11), labelC);
            }
        }
    } else if (c.type == ChartType::Bar) {
        double slot = L.plotH / std::max((size_t)1, n);
        double inner = slot * 0.82;
        double bh = inner / std::max((size_t)1, ns);
        for (size_t i = 0; i < n; i++) {
            double base = L.plotY + L.plotH - (slot * (i + 0.5) + inner / 2);
            for (size_t s = 0; s < ns; s++) {
                if (i >= c.series[s].values.size()) continue;
                double v = c.series[s].values[i];
                if (!std::isfinite(v)) continue;
                double x0 = L.vToX(0), x1 = L.vToX(v);
                double le = std::min(x0, x1);
                w.fillRect(X(le), Y(base + s * bh), S(std::max(1.0, std::fabs(x1 - x0))),
                           S(bh - 1), colOf(s));
            }
        }
    } else if (c.type == ChartType::Line || c.type == ChartType::Area ||
               c.type == ChartType::StackedArea) {
        bool area = (c.type != ChartType::Line);
        bool stacked = (c.type == ChartType::StackedArea);
        std::vector<double> prevCum(n, 0.0);
        for (size_t s = 0; s < ns; s++) {
            std::vector<double> xs, ys, yBase;
            for (size_t i = 0; i < c.series[s].values.size(); i++) {
                double v = c.series[s].values[i];
                if (!std::isfinite(v)) continue;
                xs.push_back(L.catCenter((int)i, (int)n));
                // 堆积：本系列的带是 [prevCum, prevCum+v]，不是 [0, prevCum+v]。
                // 一律填到基线会让后一个系列把前一个整个盖住，只剩最上面一条。
                yBase.push_back(L.vToY(stacked ? prevCum[i] : 0));
                double yv = stacked ? prevCum[i] + v : v;
                ys.push_back(L.vToY(yv));
                if (stacked) prevCum[i] += v;
            }
            if (xs.empty()) continue;
            if (area) {
                // 多边形：上边界正向 + 下边界逆向，闭合成一条带
                std::vector<double> px, py;
                for (size_t i = 0; i < xs.size(); i++) { px.push_back(xs[i]); py.push_back(ys[i]); }
                for (size_t i = xs.size(); i-- > 0;) { px.push_back(xs[i]); py.push_back(yBase[i]); }
                std::vector<double> pxs, pys;
                for (size_t i = 0; i < px.size(); i++) { pxs.push_back(X(px[i])); pys.push_back(Y(py[i])); }
                // 非堆积面积图各系列会重叠，必须半透明，否则只看到最上面那个
                w.fillPolygon(pxs, pys, colOf(s), stacked ? 1.0 : 0.45);
            }
            // 折线：逐段画（PdfWriter 暂不提供 polyline，用多段 line 等效）
            for (size_t i = 1; i < xs.size(); i++)
                w.line(X(xs[i - 1]), Y(ys[i - 1]), X(xs[i]), Y(ys[i]), colOf(s), S(2.2));
            for (size_t i = 0; i < xs.size(); i++)
                w.ellipse(X(xs[i]), Y(ys[i]), S(3.2), S(3.2), colOf(s), white, S(1));
            if (c.showDataLabels)
                for (size_t i = 0; i < xs.size(); i++)
                    centerText(xs[i], ys[i] - 8 - S(11) * 0.75,
                               fmtNum(c.series[s].values[i]), S(11), labelC);
        }
    } else if (c.type == ChartType::Scatter) {
        for (size_t s = 0; s < ns; s++)
            for (size_t i = 0; i < c.series[s].values.size(); i++) {
                double xv = (i < c.series[s].xs.size()) ? c.series[s].xs[i] : (double)i;
                double yv = c.series[s].values[i];
                if (!std::isfinite(xv) || !std::isfinite(yv)) continue;
                w.ellipse(X(L.vToX(xv)), Y(L.vToY(yv)), S(4), S(4), colOf(s), white, S(1));
            }
    } else if (c.type == ChartType::Histogram && !L.binCounts.empty()) {
        double slot = L.plotW / L.binCounts.size();
        for (size_t i = 0; i < L.binCounts.size(); i++) {
            double x = L.plotX + slot * i + 1;
            double y = L.vToY(L.binCounts[i]);
            double base = L.vToY(0);
            w.fillRect(X(x), Y(std::min(y, base)), S(slot - 2),
                       S(std::max(1.0, std::fabs(base - y))), colOf(0));
        }
    }

    // ---- 图例 ----
    if (c.showLegend) {
        double boxW = 14, itemGap = 18;
        double total = 0;
        for (auto& nm : L.seriesNames) total += boxW + 4 + nm.size() * 7.0 + itemGap;
        double x = c.width / 2.0 - total / 2;
        double y = c.height - 20;
        for (size_t i = 0; i < L.seriesNames.size(); i++) {
            w.rectBoth(X(x), Y(y), S(boxW), S(boxW), colOf(i), axis, 0.4);
            w.text(X(x + boxW + 4), Y(y + 11) - S(12) * 0.75, L.seriesNames[i], S(12), font, labelC);
            x += boxW + 4 + L.seriesNames[i].size() * 7.0 + itemGap;
        }
    }

    return w.save(outPath, err);
}

} // namespace xl
