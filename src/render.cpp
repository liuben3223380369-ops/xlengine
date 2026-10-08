#include "chart.hpp"
#include "layout.hpp"
#include "canvas.hpp"
#include <algorithm>
#include <cmath>
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

const RGBA kText   = rgb(0x33, 0x33, 0x33);
const RGBA kGrid   = rgb(0xE0, 0xE0, 0xE0);
const RGBA kAxis   = rgb(0x80, 0x80, 0x80);
const RGBA kWhite  = rgb(0xFF, 0xFF, 0xFF);
const RGBA kBg     = rgb(0xFF, 0xFF, 0xFF);

RGBA colorOf(const std::string& hex, int idx) {
    RGBA c;
    if (!hex.empty() && parseHexColor(hex, c)) return c;
    RGBA d;
    parseHexColor(paletteColor(idx), d);
    return d;
}

std::string fmtNum(double v) {
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        std::ostringstream o; o << std::fixed << std::setprecision(0) << v; return o.str();
    }
    std::ostringstream o; o << std::setprecision(6) << v; return o.str();
}

struct Frame {
    Canvas* cv = nullptr;
    const Chart* c = nullptr;
    const Layout* L = nullptr;

    void gridAndAxes() {
        if (!cv) return;
        double px = L->plotX, py = L->plotY, pw = L->plotW, ph = L->plotH;
        // 水平网格
        if (c->y.showGrid) {
            for (double t : L->yTicks) {
                double y = L->vToY(t);
                if (y < py - 0.5 || y > py + ph + 0.5) continue;
                cv->line(px, y, px + pw, y, kGrid, 1.0);
            }
        }
        // 垂直网格（数值 X 轴）
        if (L->isValueAxisX && c->x.showGrid) {
            for (double t : L->xTicks) {
                double x = L->vToX(t);
                if (x < px - 0.5 || x > px + pw + 0.5) continue;
                cv->line(x, py, x, py + ph, kGrid, 1.0);
            }
        }
        // 轴
        cv->line(px, py, px, py + ph, kAxis, 1.2);
        cv->line(px, py + ph, px + pw, py + ph, kAxis, 1.2);
        // Y 刻度标签
        if (c->y.showLabels) {
            double step = L->yTicks.size() > 1 ? (L->yTicks[1] - L->yTicks[0]) : 1.0;
            for (double t : L->yTicks) {
                double y = L->vToY(t);
                if (y < py - 1 || y > py + ph + 1) continue;
                std::string s = fmtTick(t, step);
                double sc = 1.6;
                double w = cv->textWidth(s, sc);
                cv->text(px - 8 - w, y - 3.5 * sc, s, kText, sc);
            }
        }
        // X 标签
        if (c->x.showLabels) {
            if (L->swapAxes) {
                // 条形图：底部是数值刻度
                double step = L->xTicks.size() > 1 ? (L->xTicks[1] - L->xTicks[0]) : 1.0;
                for (double t : L->xTicks) {
                    double x = L->vToX(t);
                    if (x < px - 1 || x > px + pw + 1) continue;
                    std::string s2 = fmtTick(t, step);
                    double sc = 1.5;
                    cv->text(x - cv->textWidth(s2, sc) / 2, py + ph + 8, s2, kText, sc);
                }
                // 左侧分类标签
                size_t n = c->pointCount();
                if (n == 0) n = c->categories.size();
                double slot = ph / std::max((size_t)1, n);
                for (size_t i = 0; i < n; i++) {
                    std::string s2 = (i < c->categories.size()) ? c->categories[i] : std::to_string(i + 1);
                    double cy = py + slot * (i + 0.5);
                    double sc = 1.5;
                    double w = cv->textWidth(s2, sc);
                    double avail = px - 10;
                    if (w > avail && s2.size() > 2) {
                        size_t keep = (size_t)(avail / (6 * sc));
                        if (keep < 1) keep = 1;
                        s2 = s2.substr(0, keep); w = cv->textWidth(s2, sc);
                    }
                    cv->text(px - 8 - w, cy - 3.5 * sc, s2, kText, sc);
                }
            } else if (L->isValueAxisX && c->type != ChartType::Histogram) {
                double step = L->xTicks.size() > 1 ? (L->xTicks[1] - L->xTicks[0]) : 1.0;
                for (double t : L->xTicks) {
                    double x = L->vToX(t);
                    if (x < px - 1 || x > px + pw + 1) continue;
                    std::string s = fmtTick(t, step);
                    double sc = 1.5;
                    cv->text(x - cv->textWidth(s, sc) / 2, py + ph + 8, s, kText, sc);
                }
            } else {
                size_t n = (c->type == ChartType::Histogram && !L->binCounts.empty())
                             ? L->binCounts.size() : c->pointCount();
                if (n == 0) n = c->categories.size();
                double sc = 1.5;
                for (size_t i = 0; i < n; i++) {
                    std::string s;
                    if (c->type == ChartType::Histogram) {
                        s = fmtTick(L->binEdges[i], (L->binEdges[1] - L->binEdges[0]));
                    } else if (i < c->categories.size()) {
                        s = c->categories[i];
                    } else {
                        s = std::to_string(i + 1);
                    }
                    double cx = L->catCenter((int)i, (int)n);
                    // 标签过长则省略
                    double w = cv->textWidth(s, sc);
                    double slot = L->plotW / std::max((size_t)1, n);
                    if (w > slot - 4 && s.size() > 3) {
                        size_t keep = (size_t)((slot - 4) / (6 * sc));
                        if (keep < 1) keep = 1;
                        s = s.substr(0, keep);
                        w = cv->textWidth(s, sc);
                    }
                    cv->text(cx - w / 2, py + ph + 8, s, kText, sc);
                }
            }
        }
        // 轴标题
        if (!c->y.title.empty()) {
            double sc = 1.6;
            cv->text(8, py - 14, c->y.title, kText, sc);
        }
        if (!c->x.title.empty()) {
            double sc = 1.6;
            double w = cv->textWidth(c->x.title, sc);
            cv->text(px + pw / 2 - w / 2, py + ph + 30, c->x.title, kText, sc);
        }
    }

    void title() {
        if (!cv || c->title.empty()) return;
        double sc = 2.4;
        double w = cv->textWidth(c->title, sc);
        cv->text(c->width / 2.0 - w / 2, 12, c->title, kText, sc);
    }

    void legend() {
        if (!cv || !c->showLegend) return;
        if (c->type == ChartType::Pie) return;             // 饼图用数据标签代替
        double sc = 1.5;
        double boxW = 14, itemGap = 18;
        double total = 0;
        for (size_t i = 0; i < L->seriesNames.size(); i++)
            total += boxW + 4 + cv->textWidth(L->seriesNames[i], sc) + itemGap;
        double x = c->width / 2.0 - total / 2;
        double y = c->height - 20;
        for (size_t i = 0; i < L->seriesNames.size(); i++) {
            cv->fillRect(x, y, boxW, boxW, colorOf(L->seriesColors[i], (int)i));
            cv->rectOutline(x, y, boxW, boxW, kAxis, 1.0);
            cv->text(x + boxW + 4, y + 2, L->seriesNames[i], kText, sc);
            x += boxW + 4 + cv->textWidth(L->seriesNames[i], sc) + itemGap;
        }
    }

    void dataLabel(double x, double y, const std::string& s, RGBA col) {
        if (!c->showDataLabels) return;
        double sc = 1.3;
        double w = cv->textWidth(s, sc);
        cv->text(x - w / 2, y, s, col, sc);
    }

    // 堆积柱：每个分类内，系列依次向上堆叠，acc 记录已堆高度
    void drawColumns(bool stacked) {
        if (!cv) return;
        size_t n = c->pointCount();
        if (n == 0) return;
        size_t ns = c->series.size();
        double slot = L->plotW / n;
        double inner = slot * 0.82;
        double bw = stacked ? inner : inner / std::max((size_t)1, ns);
        for (size_t i = 0; i < n; i++) {
            double base = L->catCenter((int)i, (int)n) - inner / 2;
            double acc = 0;
            for (size_t s = 0; s < ns; s++) {
                if (i >= c->series[s].values.size()) continue;
                double v = c->series[s].values[i];
                if (!std::isfinite(v)) continue;
                RGBA col = colorOf(L->seriesColors[s], (int)s);
                double x, top, bot;
                if (stacked) {
                    x = base;
                    double y0 = L->vToY(acc), y1 = L->vToY(acc + v);
                    top = std::min(y0, y1); bot = std::max(y0, y1);
                    acc += v;
                } else {
                    x = base + s * bw;
                    double y0 = L->vToY(v), y1 = L->vToY(0);
                    top = std::min(y0, y1); bot = std::max(y0, y1);
                }
                double h = std::max(1.0, bot - top);
                cv->fillRect(x, top, bw - 1, h, col);
                if (stacked) cv->rectOutline(x, top, bw - 1, h, kWhite, 0.8);
                dataLabel(x + bw / 2, top - 14, fmtNum(v), kText);
            }
        }
    }

    // 水平条形：分类沿 Y 轴，值沿 X 轴
    void drawBars() {
        if (!cv) return;
        size_t n = c->pointCount();
        if (n == 0) return;
        size_t ns = c->series.size();
        double slot = L->plotH / n;
        double inner = slot * 0.82;
        double bh = inner / std::max((size_t)1, ns);
        for (size_t i = 0; i < n; i++) {
            double base = L->plotY + (L->plotH * ((double)i + 0.5) / n) - inner / 2;
            for (size_t s = 0; s < ns; s++) {
                if (i >= c->series[s].values.size()) continue;
                double v = c->series[s].values[i];
                if (!std::isfinite(v)) continue;
                RGBA col = colorOf(L->seriesColors[s], (int)s);
                double x0 = L->vToX(0), x1 = L->vToX(v);
                double le = std::min(x0, x1);
                double w = std::max(1.0, std::fabs(x1 - x0));
                cv->fillRect(le, base + s * bh, w, bh - 1, col);
            }
        }
    }

    // 折线 / 面积 / 堆积面积。
    // 堆积面积的填充区间必须是 [累计下沿, 累计上沿]，不能一律填到 0，否则系列重叠。
    void drawLines(bool area, bool stacked) {
        if (!cv) return;
        size_t n = c->pointCount();
        if (n == 0) return;
        size_t ns = c->series.size();
        std::vector<double> prevCum(n, 0.0);
        for (size_t s = 0; s < ns; s++) {
            std::vector<double> xs, ys, lower;
            for (size_t i = 0; i < c->series[s].values.size(); i++) {
                double v = c->series[s].values[i];
                if (!std::isfinite(v)) continue;
                double x = (c->type == ChartType::Scatter && i < c->series[s].xs.size())
                             ? L->vToX(c->series[s].xs[i]) : L->catCenter((int)i, (int)n);
                xs.push_back(x);
                // 非堆积时 prevCum 必须保持为 0：否则第二条线会被画成两条线之和
                lower.push_back(L->vToY(stacked ? prevCum[i] : 0));
                ys.push_back(L->vToY(stacked ? prevCum[i] + v : v));
                if (stacked) prevCum[i] += v;
            }
            if (xs.empty()) continue;
            RGBA col = colorOf(L->seriesColors[s], (int)s);
            if (area) {
                std::vector<double> axs, ays;
                for (size_t i = 0; i < xs.size(); i++) { axs.push_back(xs[i]); ays.push_back(ys[i]); }
                for (size_t i = xs.size(); i-- > 0;) { axs.push_back(xs[i]); ays.push_back(lower[i]); }
                cv->fillPolygon(axs, ays, RGBA{col.r, col.g, col.b, 120});
            }
            if (xs.size() >= 2) cv->polyline(xs, ys, col, 2.2);

            // 标记与数据标签只在点数不多时绘制。
            //
            // 性能：5 万个点意味着 5 万个圆 + 5 万个文本标签，实测占渲染耗时
            // 九成以上（每点约 227us，其中绝大部分花在这两处）。
            // 视觉：点数一多，标记和标签会完全重叠成一团，本来就没意义 ——
            // Excel 自身也是点多就只画线。
            //
            // 所以这不是"为了快而牺牲效果"，而是两者的正确行为一致：
            // 点少才标注，点多只看趋势。
            const size_t kMaxMarkers = 500;     // 超过则不画标记
            const size_t kMaxLabels  = 50;      // 超过则不画数值标签
            if (xs.size() <= kMaxMarkers)
                for (size_t i = 0; i < xs.size(); i++)
                    cv->circle(xs[i], ys[i], 3.2, col, kWhite, 1.0);
            if (xs.size() <= kMaxLabels)
                for (size_t i = 0; i < xs.size(); i++)
                    dataLabel(xs[i], ys[i] - 16, fmtNum(c->series[s].values[i]), kText);
        }
    }

    void drawScatter() {
        if (!cv) return;
        for (size_t s = 0; s < c->series.size(); s++) {
            RGBA col = colorOf(L->seriesColors[s], (int)s);
            for (size_t i = 0; i < c->series[s].values.size(); i++) {
                double xv = (i < c->series[s].xs.size()) ? c->series[s].xs[i] : (double)i;
                double yv = c->series[s].values[i];
                if (!std::isfinite(xv) || !std::isfinite(yv)) continue;
                cv->circle(L->vToX(xv), L->vToY(yv), 4.0, col, kWhite, 1.0);
            }
        }
    }

    void drawHistogram() {
        if (!cv || L->binCounts.empty()) return;
        double nb = (double)L->binCounts.size();
        double slot = L->plotW / nb;
        for (size_t i = 0; i < L->binCounts.size(); i++) {
            double x = L->plotX + slot * i + 1;
            double w = slot - 2;
            double y = L->vToY(L->binCounts[i]);
            double base = L->vToY(0);
            RGBA col = colorOf(L->seriesColors[0], 0);
            cv->fillRect(x, std::min(y, base), w, std::max(1.0, std::fabs(base - y)), col);
            cv->rectOutline(x, std::min(y, base), w, std::max(1.0, std::fabs(base - y)), kWhite, 1.0);
        }
    }

    void drawPie() {
        if (!cv) return;
        // 饼图只用第一个系列
        std::vector<double> vals;
        for (size_t i = 0; i < c->series.size(); i++) {
            if (!c->series[i].values.empty()) {
                for (size_t k = 0; k < c->series[i].values.size(); k++) {
                    double v = c->series[i].values[k];
                    if (std::isfinite(v) && v > 0) vals.push_back(v);
                }
                break;
            }
        }
        if (vals.empty()) return;
        double total = 0; for (double v : vals) total += v;
        if (total <= 0) return;
        double cx = L->plotX + L->plotW / 2;
        double cy = L->plotY + L->plotH / 2;
        double r = std::min(L->plotW, L->plotH) / 2 * 0.86;
        double a = -M_PI / 2;               // 从 12 点方向开始（Excel 惯例）
        for (size_t i = 0; i < vals.size(); i++) {
            double frac = vals[i] / total;
            double a1 = a + frac * 2 * M_PI;
            RGBA col = colorOf(L->seriesColors[i], (int)i);
            cv->pieWedge(cx, cy, r, a, a1, col, kWhite, 1.5);
            // 百分比标签
            double mid = (a + a1) / 2;
            double lr = r * 0.62;
            std::ostringstream o;
            o << std::fixed << std::setprecision(1) << frac * 100 << "%";
            std::string s = o.str();
            double sc = 1.5;
            double w = cv->textWidth(s, sc);
            cv->text(cx + lr * std::cos(mid) - w / 2,
                     cy + lr * std::sin(mid) - 3 * sc, s, kWhite, sc);
            a = a1;
        }
        // 图例：饼图放右侧
        if (c->showLegend) {
            double sc = 1.4;
            double lx = L->plotX + L->plotW + 6;
            double ly = L->plotY + 6;
            for (size_t i = 0; i < vals.size(); i++) {
                std::string nm = (i < c->categories.size()) ? c->categories[i]
                               : ((i < L->seriesNames.size() && !L->seriesNames[i].empty())
                                    ? L->seriesNames[i] : ("S" + std::to_string(i + 1)));
                cv->fillRect(lx, ly, 11, 11, colorOf(L->seriesColors[i], (int)i));
                cv->text(lx + 15, ly + 1, nm, kText, sc);
                ly += 16;
            }
        }
    }
};

} // namespace

// ---------------------------------------------------------------------------
// PNG 渲染
// ---------------------------------------------------------------------------
static bool drawToCanvas(const Chart& c, Canvas& cv) {
    Layout L = computeLayout(c);
    Frame F; F.cv = &cv; F.c = &c; F.L = &L;

    cv.clear(kBg);
    F.title();
    if (c.type == ChartType::Pie) { F.drawPie(); return true; }
    F.gridAndAxes();
    switch (c.type) {
        case ChartType::Column:        F.drawColumns(false); break;
        case ChartType::StackedColumn: F.drawColumns(true);  break;
        case ChartType::Bar:           F.drawBars();         break;
        case ChartType::Line:          F.drawLines(false, false); break;
        case ChartType::Area:          F.drawLines(true, false);  break;
        case ChartType::StackedArea:   F.drawLines(true, true);   break;
        case ChartType::Scatter:       F.drawScatter();      break;
        case ChartType::Histogram:     F.drawHistogram();    break;
        default: break;
    }
    F.legend();
    return true;
}

bool chartToPNG(const Chart& c, std::vector<unsigned char>& out, int ss) {
    Canvas cv(c.width, c.height, ss);
    if (!drawToCanvas(c, cv)) return false;
    std::vector<uint8_t> png;
    if (!cv.toPNG(png)) return false;
    out = std::move(png);
    return true;
}

// ---------------------------------------------------------------------------
// SVG 渲染（矢量；文本用系统字体，所以支持中文）
// ---------------------------------------------------------------------------
std::string chartToSVG(const Chart& c) {
    Layout L = computeLayout(c);
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    auto esc = [](const std::string& s) {
        std::string r;
        for (char ch : s) {
            if (ch == '&') r += "&amp;";
            else if (ch == '<') r += "&lt;";
            else if (ch == '>') r += "&gt;";
            else if (ch == '"') r += "&quot;";
            else r += ch;
        }
        return r;
    };
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << c.width
      << "\" height=\"" << c.height << "\" viewBox=\"0 0 " << c.width << " " << c.height << "\">\n";
    o << "<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";
    o << "<g font-family=\"-apple-system,BlinkMacSystemFont,'Segoe UI','Microsoft YaHei',"
         "'PingFang SC','Hiragino Sans GB',sans-serif\">\n";

    if (!c.title.empty())
        o << "<text x=\"" << c.width / 2.0 << "\" y=\"30\" text-anchor=\"middle\" "
          << "font-size=\"19\" fill=\"#333333\">" << esc(c.title) << "</text>\n";

    if (c.type == ChartType::Pie && !c.series.empty()) {
        std::vector<double> vals;
        for (double v : c.series[0].values) if (std::isfinite(v) && v > 0) vals.push_back(v);
        double total = 0; for (double v : vals) total += v;
        if (total > 0) {
            double cx = L.plotX + L.plotW / 2, cy = L.plotY + L.plotH / 2;
            double r = std::min(L.plotW, L.plotH) / 2 * 0.86;
            double a = -M_PI / 2;
            for (size_t i = 0; i < vals.size(); i++) {
                double frac = vals[i] / total;
                double a1 = a + frac * 2 * M_PI;
                std::string col = (i < L.seriesColors.size()) ? L.seriesColors[i] : paletteColor((int)i);
                double x0 = cx + r * std::cos(a),  y0 = cy + r * std::sin(a);
                double x1 = cx + r * std::cos(a1), y1 = cy + r * std::sin(a1);
                int large = (frac > 0.5) ? 1 : 0;
                o << "<path d=\"M " << cx << " " << cy << " L " << x0 << " " << y0
                  << " A " << r << " " << r << " 0 " << large << " 1 " << x1 << " " << y1
                  << " Z\" fill=\"" << col << "\" stroke=\"#FFFFFF\" stroke-width=\"1.5\"/>\n";
                double mid = (a + a1) / 2, lr = r * 0.62;
                o << "<text x=\"" << cx + lr * std::cos(mid) << "\" y=\"" << cy + lr * std::sin(mid)
                  << "\" text-anchor=\"middle\" dominant-baseline=\"middle\" font-size=\"12\" fill=\"#FFFFFF\">"
                  << (frac * 100) << "%</text>\n";
                a = a1;
            }
            if (c.showLegend) {
                double ly = L.plotY + 12;
                for (size_t i = 0; i < vals.size(); i++) {
                    std::string nm = (i < c.categories.size()) ? c.categories[i] : ("S" + std::to_string(i + 1));
                    std::string col = (i < L.seriesColors.size()) ? L.seriesColors[i] : paletteColor((int)i);
                    o << "<rect x=\"" << (L.plotX + L.plotW + 6) << "\" y=\"" << ly
                      << "\" width=\"11\" height=\"11\" fill=\"" << col << "\"/>\n";
                    o << "<text x=\"" << (L.plotX + L.plotW + 21) << "\" y=\"" << (ly + 10)
                      << "\" font-size=\"12\" fill=\"#333333\">" << esc(nm) << "</text>\n";
                    ly += 16;
                }
            }
        }
        o << "</g></svg>\n";
        return o.str();
    }

    // 网格
    if (c.y.showGrid)
        for (double t : L.yTicks) {
            double y = L.vToY(t);
            if (y < L.plotY - 0.5 || y > L.plotY + L.plotH + 0.5) continue;
            o << "<line x1=\"" << L.plotX << "\" y1=\"" << y << "\" x2=\"" << (L.plotX + L.plotW)
              << "\" y2=\"" << y << "\" stroke=\"#E0E0E0\" stroke-width=\"1\"/>\n";
        }
    // 轴
    o << "<line x1=\"" << L.plotX << "\" y1=\"" << L.plotY << "\" x2=\"" << L.plotX
      << "\" y2=\"" << (L.plotY + L.plotH) << "\" stroke=\"#808080\" stroke-width=\"1.2\"/>\n";
    o << "<line x1=\"" << L.plotX << "\" y1=\"" << (L.plotY + L.plotH) << "\" x2=\""
      << (L.plotX + L.plotW) << "\" y2=\"" << (L.plotY + L.plotH)
      << "\" stroke=\"#808080\" stroke-width=\"1.2\"/>\n";
    // Y 刻度
    if (c.y.showLabels) {
        double step = L.yTicks.size() > 1 ? (L.yTicks[1] - L.yTicks[0]) : 1.0;
        for (double t : L.yTicks) {
            double y = L.vToY(t);
            if (y < L.plotY - 1 || y > L.plotY + L.plotH + 1) continue;
            o << "<text x=\"" << (L.plotX - 8) << "\" y=\"" << (y + 4)
              << "\" text-anchor=\"end\" font-size=\"12\" fill=\"#333333\">"
              << esc(fmtTick(t, step)) << "</text>\n";
        }
    }
    // X 标签
    if (c.x.showLabels) {
        if (L.isValueAxisX && c.type != ChartType::Histogram) {
            double step = L.xTicks.size() > 1 ? (L.xTicks[1] - L.xTicks[0]) : 1.0;
            for (double t : L.xTicks) {
                double x = L.vToX(t);
                if (x < L.plotX - 1 || x > L.plotX + L.plotW + 1) continue;
                o << "<text x=\"" << x << "\" y=\"" << (L.plotY + L.plotH + 20)
                  << "\" text-anchor=\"middle\" font-size=\"12\" fill=\"#333333\">"
                  << esc(fmtTick(t, step)) << "</text>\n";
            }
        } else {
            size_t n = (c.type == ChartType::Histogram && !L.binCounts.empty())
                         ? L.binCounts.size() : c.pointCount();
            if (n == 0) n = c.categories.size();
            for (size_t i = 0; i < n; i++) {
                std::string s = (c.type == ChartType::Histogram)
                    ? fmtTick(L.binEdges[i], L.binEdges[1] - L.binEdges[0])
                    : (i < c.categories.size() ? c.categories[i] : std::to_string(i + 1));
                o << "<text x=\"" << L.catCenter((int)i, (int)n) << "\" y=\""
                  << (L.plotY + L.plotH + 20) << "\" text-anchor=\"middle\" font-size=\"12\" fill=\"#333333\">"
                  << esc(s) << "</text>\n";
            }
        }
    }
    if (!c.y.title.empty())
        o << "<text x=\"10\" y=\"" << (L.plotY - 10) << "\" font-size=\"13\" fill=\"#333333\">"
          << esc(c.y.title) << "</text>\n";
    if (!c.x.title.empty())
        o << "<text x=\"" << (L.plotX + L.plotW / 2) << "\" y=\"" << (L.plotY + L.plotH + 44)
          << "\" text-anchor=\"middle\" font-size=\"13\" fill=\"#333333\">" << esc(c.x.title) << "</text>\n";

    // 系列
    size_t n = c.pointCount();
    size_t ns = c.series.size();
    auto colOf = [&](size_t s) {
        return (s < L.seriesColors.size()) ? L.seriesColors[s] : paletteColor((int)s);
    };
    if (c.type == ChartType::Column || c.type == ChartType::StackedColumn) {
        bool stacked = (c.type == ChartType::StackedColumn);
        double slot = L.plotW / std::max((size_t)1, n);
        double inner = slot * 0.82;
        double bw = stacked ? inner : inner / std::max((size_t)1, ns);
        for (size_t i = 0; i < n; i++) {
            double base = L.catCenter((int)i, (int)n) - inner / 2;
            std::vector<double> cum(ns, 0.0);
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
                o << "<rect x=\"" << x << "\" y=\"" << top << "\" width=\"" << (bw - 1)
                  << "\" height=\"" << h << "\" fill=\"" << colOf(s) << "\"/>\n";
                if (c.showDataLabels)
                    o << "<text x=\"" << (x + bw / 2) << "\" y=\"" << (top - 6)
                      << "\" text-anchor=\"middle\" font-size=\"11\" fill=\"#333\">"
                      << esc(fmtNum(v)) << "</text>\n";
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
                o << "<rect x=\"" << le << "\" y=\"" << (base + s * bh) << "\" width=\""
                  << std::max(1.0, std::fabs(x1 - x0)) << "\" height=\"" << (bh - 1)
                  << "\" fill=\"" << colOf(s) << "\"/>\n";
            }
        }
    } else if (c.type == ChartType::Line || c.type == ChartType::Area ||
               c.type == ChartType::StackedArea) {
        bool area = (c.type != ChartType::Line);
        bool stacked = (c.type == ChartType::StackedArea);
        std::vector<double> prevCum(n, 0.0);
        for (size_t s = 0; s < ns; s++) {
            std::vector<double> xs, ys;
            for (size_t i = 0; i < c.series[s].values.size(); i++) {
                double v = c.series[s].values[i];
                if (!std::isfinite(v)) continue;
                double x = L.catCenter((int)i, (int)n);
                double yv = stacked ? prevCum[i] + v : v;
                xs.push_back(x); ys.push_back(L.vToY(yv));
                if (stacked) prevCum[i] += v;
            }
            if (xs.empty()) continue;
            if (area) {
                o << "<path d=\"M " << xs[0] << " " << L.vToY(0);
                for (size_t i = 0; i < xs.size(); i++) o << " L " << xs[i] << " " << ys[i];
                o << " L " << xs.back() << " " << L.vToY(0) << " Z\" fill=\"" << colOf(s)
                  << "\" fill-opacity=\"0.45\"/>\n";
            }
            o << "<path d=\"M " << xs[0] << " " << ys[0];
            for (size_t i = 1; i < xs.size(); i++) o << " L " << xs[i] << " " << ys[i];
            o << "\" fill=\"none\" stroke=\"" << colOf(s) << "\" stroke-width=\"2.2\"/>\n";
            // 与 PNG 路径同样的取舍：点数多时不输出标记与标签。
            // SVG 这里影响更直接 —— 5 万个 <circle> 会让文件涨到几十 MB，
            // 而它们在视觉上根本分辨不出来。
            if (xs.size() <= 500)
                for (size_t i = 0; i < xs.size(); i++)
                    o << "<circle cx=\"" << xs[i] << "\" cy=\"" << ys[i] << "\" r=\"3.2\" fill=\""
                      << colOf(s) << "\" stroke=\"#FFF\"/>\n";
            if (c.showDataLabels && xs.size() <= 50)
                for (size_t i = 0; i < xs.size(); i++)
                    o << "<text x=\"" << xs[i] << "\" y=\"" << (ys[i] - 8)
                      << "\" text-anchor=\"middle\" font-size=\"11\" fill=\"#333\">"
                      << esc(fmtNum(c.series[s].values[i])) << "</text>\n";
        }
    } else if (c.type == ChartType::Scatter) {
        for (size_t s = 0; s < ns; s++)
            for (size_t i = 0; i < c.series[s].values.size(); i++) {
                double xv = (i < c.series[s].xs.size()) ? c.series[s].xs[i] : (double)i;
                double yv = c.series[s].values[i];
                if (!std::isfinite(xv) || !std::isfinite(yv)) continue;
                o << "<circle cx=\"" << L.vToX(xv) << "\" cy=\"" << L.vToY(yv)
                  << "\" r=\"4\" fill=\"" << colOf(s) << "\" stroke=\"#FFF\"/>\n";
            }
    } else if (c.type == ChartType::Histogram && !L.binCounts.empty()) {
        double slot = L.plotW / L.binCounts.size();
        for (size_t i = 0; i < L.binCounts.size(); i++) {
            double x = L.plotX + slot * i + 1;
            double y = L.vToY(L.binCounts[i]);
            double base = L.vToY(0);
            o << "<rect x=\"" << x << "\" y=\"" << std::min(y, base) << "\" width=\"" << (slot - 2)
              << "\" height=\"" << std::max(1.0, std::fabs(base - y)) << "\" fill=\"" << colOf(0) << "\"/>\n";
        }
    }

    // 图例
    if (c.showLegend) {
        double boxW = 14, itemGap = 18;
        double total = 0;
        for (auto& nm : L.seriesNames) total += boxW + 4 + nm.size() * 7.0 + itemGap;
        double x = c.width / 2.0 - total / 2;
        double y = c.height - 20;
        for (size_t i = 0; i < L.seriesNames.size(); i++) {
            o << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << boxW << "\" height=\""
              << boxW << "\" fill=\"" << colOf(i) << "\" stroke=\"#808080\"/>\n";
            o << "<text x=\"" << (x + boxW + 4) << "\" y=\"" << (y + 11)
              << "\" font-size=\"12\" fill=\"#333333\">" << esc(L.seriesNames[i]) << "</text>\n";
            x += boxW + 4 + L.seriesNames[i].size() * 7.0 + itemGap;
        }
    }
    o << "</g></svg>\n";
    return o.str();
}

// ---------------------------------------------------------------------------
// ASCII 预览：无图形环境下验证渲染逻辑
// ---------------------------------------------------------------------------
std::string chartToASCII(const Chart& c, int /*cols*/, int rows) {
    std::ostringstream o;
    Layout L = computeLayout(c);
    if (c.type == ChartType::Pie) {
        std::vector<double> vals;
        if (!c.series.empty()) for (double v : c.series[0].values) if (v > 0) vals.push_back(v);
        double total = 0; for (double v : vals) total += v;
        o << "Pie: " << vals.size() << " 项，合计 " << total << "\n";
        for (size_t i = 0; i < vals.size(); i++)
            o << "  " << (i < c.categories.size() ? c.categories[i] : std::to_string(i + 1))
              << ": " << vals[i] << " (" << (vals[i] / total * 100) << "%)\n";
        return o.str();
    }
    // 简易柱形 ASCII
    std::vector<double> vals;
    if (!c.series.empty()) vals = c.series[0].values;
    if (vals.empty()) return "(无数据)\n";
    double mx = *std::max_element(vals.begin(), vals.end());
    double mn = *std::min_element(vals.begin(), vals.end());
    if (mn > 0) mn = 0;
    for (int r = rows; r >= 1; r--) {
        double thr = mn + (mx - mn) * r / rows;
        for (double v : vals) o << (v >= thr ? "##" : "  ");
        o << "\n";
    }
    o << "Y: " << mn << " .. " << mx << "  刻度数=" << L.yTicks.size() << "\n";
    return o.str();
}

bool addSeriesFromValues(DataSeries& s, const std::vector<Value>& vals) {
    for (auto& v : vals) {
        if (v.isError()) return false;
        if (v.isEmpty()) { s.values.push_back(std::nan("")); continue; }
        double d; Value e;
        if (!toNumber(v, d, e)) { s.values.push_back(std::nan("")); continue; }
        s.values.push_back(d);
    }
    return true;
}

} // namespace xl
