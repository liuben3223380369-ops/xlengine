#include "layout.hpp"
#include "chart.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace xl {

double niceNum(double x, bool round) {
    if (x <= 0 || !std::isfinite(x)) return 1.0;
    int expv = (int)std::floor(std::log10(x));
    double f = x / std::pow(10.0, expv);
    double nf;
    if (round) {
        if (f < 1.5) nf = 1.0;
        else if (f < 3.0) nf = 2.0;
        else if (f < 7.0) nf = 5.0;
        else nf = 10.0;
    } else {
        if (f <= 1.0) nf = 1.0;
        else if (f <= 2.0) nf = 2.0;
        else if (f <= 5.0) nf = 5.0;
        else nf = 10.0;
    }
    return nf * std::pow(10.0, expv);
}

void niceScale(double min, double max, int maxTicks,
               double& niceMin, double& niceMax, double& step) {
    if (!std::isfinite(min) || !std::isfinite(max) || max <= min) { min = 0; max = 1; }
    double range = niceNum(max - min, false);
    step = niceNum(range / std::max(1, maxTicks - 1), true);
    niceMin = std::floor(min / step) * step;
    niceMax = std::ceil(max / step) * step;
    // 浮点误差会让最后一个刻度画不出来，这里做容差修正
    if (niceMax - max < step * 1e-9) niceMax = max;
    if (step <= 0) step = 1.0;
}

const char* paletteColor(int index) {
    // Office 主题色顺序（与 Excel 默认系列色一致）
    static const char* pal[] = {
        "#4472C4", "#ED7D31", "#A5A5A5", "#FFC000",
        "#5B9BD5", "#70AD47", "#264478", "#9E480E",
        "#636363", "#997300", "#255E91", "#43682B"
    };
    static const int n = (int)(sizeof(pal) / sizeof(pal[0]));
    return pal[index % n];
}

std::string fmtTick(double v, double step) {
    if (v == 0) return "0";
    double a = std::fabs(v);
    int dec;
    if (step >= 1) dec = 0;
    else if (step >= 0.1) dec = 1;
    else if (step >= 0.01) dec = 2;
    else if (step >= 0.001) dec = 3;
    else dec = 4;
    if (a >= 1e9)  { std::ostringstream o; o << std::fixed << std::setprecision(1) << v/1e9 << "B"; return o.str(); }
    if (a >= 1e6)  { std::ostringstream o; o << std::fixed << std::setprecision(1) << v/1e6 << "M"; return o.str(); }
    if (a >= 1e4 && step >= 1000) { std::ostringstream o; o << std::fixed << std::setprecision(0) << v/1e3 << "K"; return o.str(); }
    std::ostringstream o;
    o << std::fixed << std::setprecision(dec) << v;
    std::string s = o.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

size_t Chart::pointCount() const {
    size_t n = 0;
    for (auto& s : series) n = std::max(n, s.values.size());
    return n;
}

void Chart::computeRange(double& ymin, double& ymax, std::vector<double>& ticks) const {
    bool any = false;
    double lo = 0, hi = 0;
    if (type == ChartType::Histogram) {
        // 直方图纵轴是频数，范围由分箱结果决定
        double mx = 0;
        for (auto& v : series) if (!v.values.empty()) {
            // 稍后由 layout 重算，这里先给上界
            mx = std::max(mx, (double)v.values.size());
        }
        lo = 0; hi = std::max(1.0, mx);
        any = true;
    } else if (type == ChartType::StackedColumn || type == ChartType::StackedArea) {
        size_t n = pointCount();
        std::vector<double> sum(n, 0.0);
        for (auto& s : series)
            for (size_t i = 0; i < s.values.size() && i < n; i++)
                if (std::isfinite(s.values[i])) sum[i] += s.values[i];
        lo = 0; hi = 0;
        for (double v : sum) { lo = std::min(lo, v); hi = std::max(hi, v); any = true; }
    } else {
        for (auto& s : series) {
            for (double v : s.values) {
                if (!std::isfinite(v)) continue;
                if (!any) { lo = hi = v; any = true; }
                else { lo = std::min(lo, v); hi = std::max(hi, v); }
            }
        }
    }
    if (!any) { lo = 0; hi = 1; }
    // Excel 惯例：数据同号时基线取 0
    if (type != ChartType::Scatter) {
        if (lo > 0) lo = 0;
        if (hi < 0) hi = 0;
    }
    if (lo == hi) { hi = lo + 1; }
    double step;
    int nTick = y.autoRange ? y.tickCount : y.tickCount;
    niceScale(lo, hi, nTick, ymin, ymax, step);
    if (!y.autoRange) { ymin = y.min; ymax = y.max; step = (ymax - ymin) / std::max(1, y.tickCount - 1); }
    ticks.clear();
    for (double t = ymin; t <= ymax + step * 1e-9; t += step) ticks.push_back(t);
}

Layout computeLayout(const Chart& c) {
    Layout L;
    // 边距：左留给 Y 刻度，下留给 X 标签 + 轴标题
    double padL = 72, padR = 28, padT = c.title.empty() ? 24 : 52, padB = 56;
    L.plotX = padL;
    L.plotY = padT;
    L.plotW = std::max(10.0, (double)c.width  - padL - padR);
    L.plotH = std::max(10.0, (double)c.height - padT - padB);

    for (size_t i = 0; i < c.series.size(); i++) {
        L.seriesNames.push_back(c.series[i].name);
        L.seriesColors.push_back(c.series[i].hasColor() ? c.series[i].color : paletteColor((int)i));
    }

    // 直方图：先分箱
    if (c.type == ChartType::Histogram && !c.series.empty()) {
        const std::vector<double>& src = c.series[0].values;
        if (!src.empty()) {
            double mn = *std::min_element(src.begin(), src.end());
            double mx = *std::max_element(src.begin(), src.end());
            int nb = std::max(1, c.bins);
            if (mx == mn) { mx = mn + 1; }
            double w = (mx - mn) / nb;
            L.binEdges.assign(nb + 1, 0);
            for (int i = 0; i <= nb; i++) L.binEdges[i] = mn + w * i;
            L.binCounts.assign(nb, 0);
            for (double v : src) {
                int k = (int)std::floor((v - mn) / w);
                if (k >= nb) k = nb - 1;
                if (k < 0) k = 0;
                L.binCounts[k] += 1;
            }
            L.ymin = 0;
            double step;
            niceScale(0, *std::max_element(L.binCounts.begin(), L.binCounts.end()),
                      c.y.tickCount, L.ymin, L.ymax, step);
            for (double t = L.ymin; t <= L.ymax + step * 1e-9; t += step) L.yTicks.push_back(t);
            L.xmin = mn; L.xmax = mx; L.isValueAxisX = true;
            double xs;
            niceScale(mn, mx, 6, L.xmin, L.xmax, xs);
            L.xTicks.clear();
            for (double t = L.xmin; t <= L.xmax + xs * 1e-9; t += xs) L.xTicks.push_back(t);
            return L;
        }
    }

    // 条形图：值域在 X 轴，分类沿 Y 轴排布 —— 与柱形图正好相反
    if (c.type == ChartType::Bar) {
        L.swapAxes = true;
        L.isValueAxisX = true;
        bool any = false; double lo = 0, hi = 0;
        for (auto& s : c.series)
            for (double v : s.values) {
                if (!std::isfinite(v)) continue;
                if (!any) { lo = hi = v; any = true; } else { lo = std::min(lo, v); hi = std::max(hi, v); }
            }
        if (!any) { lo = 0; hi = 1; }
        if (lo > 0) lo = 0;
        if (hi < 0) hi = 0;
        if (lo == hi) hi = lo + 1;
        double step;
        niceScale(lo, hi, c.x.tickCount, L.xmin, L.xmax, step);
        if (!c.x.autoRange) { L.xmin = c.x.min; L.xmax = c.x.max; step = (L.xmax - L.xmin) / std::max(1, c.x.tickCount - 1); }
        L.xTicks.clear();
        for (double t = L.xmin; t <= L.xmax + step * 1e-9; t += step) L.xTicks.push_back(t);
        // Y 方向按分类数均分
        L.ymin = 0; L.ymax = (double)std::max((size_t)1, c.pointCount());
        return L;
    }

    std::vector<double> ticks;
    double ymin, ymax;
    c.computeRange(ymin, ymax, ticks);
    L.ymin = ymin; L.ymax = ymax; L.yTicks = ticks;

    if (c.type == ChartType::Scatter) {
        L.isValueAxisX = true;
        bool any = false; double lo = 0, hi = 0;
        for (auto& s : c.series) {
            for (size_t i = 0; i < s.values.size(); i++) {
                double v = (i < s.xs.size()) ? s.xs[i] : (double)i;
                if (!std::isfinite(v)) continue;
                if (!any) { lo = hi = v; any = true; } else { lo = std::min(lo, v); hi = std::max(hi, v); }
            }
        }
        if (!any) { lo = 0; hi = 1; }
        if (lo == hi) { lo -= 0.5; hi += 0.5; }
        double step;
        niceScale(lo, hi, 6, L.xmin, L.xmax, step);
        for (double t = L.xmin; t <= L.xmax + step * 1e-9; t += step) L.xTicks.push_back(t);
    }
    return L;
}

} // namespace xl
