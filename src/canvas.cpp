#include "canvas.hpp"
#include "font5x7.h"
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>

// M_PI 是 POSIX 扩展，不是 C++ 标准：MinGW 在 -std=c++17（严格 ANSI）下
// 不定义它，于是 Windows 构建会报 "M_PI was not declared in this scope"。
// 这里补齐，用 #ifndef 保护以免与平台自带的重复定义。
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


namespace xl {

bool parseHexColor(const std::string& s, RGBA& out) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (s.size() == 7 && s[0] == '#') {
        int r = hex(s[1]) * 16 + hex(s[2]);
        int g = hex(s[3]) * 16 + hex(s[4]);
        int b = hex(s[5]) * 16 + hex(s[6]);
        if (r < 0 || g < 0 || b < 0) return false;
        out = rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
        return true;
    }
    if (s.size() == 4 && s[0] == '#') {
        int r = hex(s[1]), g = hex(s[2]), b = hex(s[3]);
        if (r < 0 || g < 0 || b < 0) return false;
        out = rgb((uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(b * 17));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// CRC32（PNG 规范要求，无现成库可用则自算）
// ---------------------------------------------------------------------------
static uint32_t crcTable[256];
static bool crcInit() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[n] = c;
    }
    return true;
}
static bool g_crcReady = crcInit();

uint32_t crc32Bytes(const uint8_t* data, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void putBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)(x));
}

void pngWriteChunk(std::vector<uint8_t>& out, const char* type,
                   const uint8_t* data, size_t len) {
    putBE32(out, (uint32_t)len);
    std::vector<uint8_t> body;
    body.insert(body.end(), type, type + 4);
    if (data && len) body.insert(body.end(), data, data + len);
    out.insert(out.end(), body.begin(), body.end());
    putBE32(out, crc32Bytes(body.data(), body.size()));
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------
Canvas::Canvas(int w, int h, int ss) : w_(w), h_(h), ss_(ss < 1 ? 1 : ss) {
    buf_.assign((size_t)w_ * ss_ * h_ * ss_ * 4, 0);
}

void Canvas::clear(RGBA c) {
    for (size_t i = 0; i < buf_.size(); i += 4) {
        buf_[i] = c.r; buf_[i+1] = c.g; buf_[i+2] = c.b; buf_[i+3] = c.a;
    }
}

void Canvas::blend(int x, int y, RGBA c) {
    if (!inBuf(x, y)) return;
    size_t i = idx(x, y);
    double a = c.a / 255.0;
    double ia = 1.0 - a;
    buf_[i]   = (uint8_t)(c.r * a + buf_[i]   * ia);
    buf_[i+1] = (uint8_t)(c.g * a + buf_[i+1] * ia);
    buf_[i+2] = (uint8_t)(c.b * a + buf_[i+2] * ia);
    buf_[i+3] = (uint8_t)(255 * a + buf_[i+3] * ia);
}

void Canvas::blendAA(int x, int y, RGBA c, double cov) {
    if (!inBuf(x, y)) return;
    size_t i = idx(x, y);
    double a = (c.a / 255.0) * cov;
    double ia = 1.0 - a;
    buf_[i]   = (uint8_t)(c.r * a + buf_[i]   * ia);
    buf_[i+1] = (uint8_t)(c.g * a + buf_[i+1] * ia);
    buf_[i+2] = (uint8_t)(c.b * a + buf_[i+2] * ia);
    buf_[i+3] = (uint8_t)(255 * a + buf_[i+3] * ia);
}

void Canvas::fillRect(double x, double y, double w, double h, RGBA c) {
    int x0 = (int)std::floor(x * ss_), y0 = (int)std::floor(y * ss_);
    int x1 = (int)std::ceil((x + w) * ss_), y1 = (int)std::ceil((y + h) * ss_);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) blend(xx, yy, c);
}

void Canvas::rectOutline(double x, double y, double w, double h, RGBA c, double lw) {
    line(x, y, x + w, y, c, lw);
    line(x + w, y, x + w, y + h, c, lw);
    line(x + w, y + h, x, y + h, c, lw);
    line(x, y + h, x, y, c, lw);
}

// Xiaolin Wu 抗锯齿直线（单像素宽，设备像素坐标输入）
void Canvas::lineAA(double x0, double y0, double x1, double y1, RGBA c) {
    double X0 = x0, Y0 = y0, X1 = x1, Y1 = y1;
    bool steep = std::fabs(Y1 - Y0) > std::fabs(X1 - X0);
    if (steep) { std::swap(X0, Y0); std::swap(X1, Y1); }
    if (X0 > X1) { std::swap(X0, X1); std::swap(Y0, Y1); }
    double dx = X1 - X0, dy = Y1 - Y0;
    double grad = dx == 0 ? 1.0 : dy / dx;

    double xend = std::round(X0);
    double yend = Y0 + grad * (xend - X0);
    double xgap = 1.0 - (X0 + 0.5 - std::floor(X0 + 0.5));
    int xpxl1 = (int)xend, ypxl1 = (int)std::floor(yend);
    if (steep) {
        blendAA(ypxl1, xpxl1, c, (1.0 - (yend - std::floor(yend))) * xgap);
        blendAA(ypxl1 + 1, xpxl1, c, (yend - std::floor(yend)) * xgap);
    } else {
        blendAA(xpxl1, ypxl1, c, (1.0 - (yend - std::floor(yend))) * xgap);
        blendAA(xpxl1, ypxl1 + 1, c, (yend - std::floor(yend)) * xgap);
    }
    double intery = yend + grad;

    xend = std::round(X1);
    yend = Y1 + grad * (xend - X1);
    xgap = X1 + 0.5 - std::floor(X1 + 0.5);
    int xpxl2 = (int)xend, ypxl2 = (int)std::floor(yend);
    if (steep) {
        blendAA(ypxl2, xpxl2, c, (1.0 - (yend - std::floor(yend))) * xgap);
        blendAA(ypxl2 + 1, xpxl2, c, (yend - std::floor(yend)) * xgap);
    } else {
        blendAA(xpxl2, ypxl2, c, (1.0 - (yend - std::floor(yend))) * xgap);
        blendAA(xpxl2, ypxl2 + 1, c, (yend - std::floor(yend)) * xgap);
    }

    if (steep) {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            blendAA((int)std::floor(intery), x, c, 1.0 - (intery - std::floor(intery)));
            blendAA((int)std::floor(intery) + 1, x, c, intery - std::floor(intery));
            intery += grad;
        }
    } else {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            blendAA(x, (int)std::floor(intery), c, 1.0 - (intery - std::floor(intery)));
            blendAA(x, (int)std::floor(intery) + 1, c, intery - std::floor(intery));
            intery += grad;
        }
    }
}

void Canvas::line(double x0, double y0, double x1, double y1, RGBA c, double lw) {
    double wpx = lw * ss_;                 // 设备像素宽度
    if (wpx <= 1.0) {
        lineAA(x0 * ss_, y0 * ss_, x1 * ss_, y1 * ss_, c);
        return;
    }
    double dx = (x1 - x0) * ss_, dy = (y1 - y0) * ss_;
    double len = std::hypot(dx, dy);
    if (len < 1e-9) { fillRect(x0 - lw / 2, y0 - lw / 2, lw, lw, c); return; }
    double nx = -dy / len, ny = dx / len;
    int copies = (int)std::ceil(wpx);
    for (int k = 0; k < copies; k++) {
        double t = (copies == 1) ? 0.0 : (double)k / (copies - 1) - 0.5;
        double off = t * (double)copies;
        lineAA(x0 * ss_ + nx * off, y0 * ss_ + ny * off,
               x1 * ss_ + nx * off, y1 * ss_ + ny * off, c);
    }
}

void Canvas::polyline(const std::vector<double>& xs, const std::vector<double>& ys,
                      RGBA c, double lw) {
    for (size_t i = 1; i < xs.size() && i < ys.size(); i++)
        line(xs[i-1], ys[i-1], xs[i], ys[i], c, lw);
}

// 扫描线多边形填充（支持任意简单多边形）
void Canvas::fillPolygon(const std::vector<double>& xs, const std::vector<double>& ys, RGBA c) {
    size_t n = std::min(xs.size(), ys.size());
    if (n < 3) return;
    double minY = *std::min_element(ys.begin(), ys.begin() + n);
    double maxY = *std::max_element(ys.begin(), ys.begin() + n);
    int y0 = (int)std::floor(minY * ss_), y1 = (int)std::ceil(maxY * ss_);
    std::vector<double> crossings;
    for (int py = y0; py <= y1; py++) {
        double sy = (py + 0.5) / ss_;
        crossings.clear();
        for (size_t i = 0; i < n; i++) {
            size_t j = (i + 1) % n;
            double yi = ys[i], yj = ys[j];
            if ((yi <= sy && yj > sy) || (yj <= sy && yi > sy)) {
                double t = (sy - yi) / (yj - yi);
                crossings.push_back(xs[i] + t * (xs[j] - xs[i]));
            }
        }
        std::sort(crossings.begin(), crossings.end());
        for (size_t k = 0; k + 1 < crossings.size(); k += 2) {
            int xa = (int)std::round(crossings[k] * ss_);
            int xb = (int)std::round(crossings[k+1] * ss_);
            for (int px = xa; px < xb; px++) blend(px, py, c);
        }
    }
}

void Canvas::ellipse(double cx, double cy, double rx, double ry,
                     RGBA fill, RGBA stroke, double lw) {
    int steps = 720;
    std::vector<double> xs, ys;
    for (int i = 0; i < steps; i++) {
        double t = 2.0 * M_PI * i / steps;
        xs.push_back(cx + rx * std::cos(t));
        ys.push_back(cy + ry * std::sin(t));
    }
    if (fill.a > 0) fillPolygon(xs, ys, fill);
    if (stroke.a > 0) {
        for (int i = 0; i < steps; i++) {
            int j = (i + 1) % steps;
            line(xs[i], ys[i], xs[j], ys[j], stroke, lw);
        }
    }
}

void Canvas::circle(double cx, double cy, double r, RGBA fill, RGBA stroke, double lw) {
    ellipse(cx, cy, r, r, fill, stroke, lw);
}

void Canvas::pieWedge(double cx, double cy, double r,
                      double a0, double a1, RGBA fill, RGBA stroke, double lw) {
    if (a1 <= a0) return;
    // 整圆特判
    if (a1 - a0 >= 2.0 * M_PI - 1e-9) { circle(cx, cy, r, fill, stroke, lw); return; }
    int steps = (int)std::max(8, (int)(64 * (a1 - a0) / M_PI));
    std::vector<double> xs{ cx }, ys{ cy };
    for (int i = 0; i <= steps; i++) {
        double t = a0 + (a1 - a0) * i / steps;
        xs.push_back(cx + r * std::cos(t));
        ys.push_back(cy + r * std::sin(t));
    }
    if (fill.a > 0) fillPolygon(xs, ys, fill);
    if (stroke.a > 0) {
        line(cx, cy, xs[1], ys[1], stroke, lw);
        for (int i = 1; i + 1 < (int)xs.size(); i++)
            line(xs[i], ys[i], xs[i+1], ys[i+1], stroke, lw);
        line(xs.back(), ys.back(), cx, cy, stroke, lw);
    }
}

double Canvas::textWidth(const std::string& s, double scale) const {
    if (s.empty()) return 0;
    return (s.size() * 6 - 1) * scale;      // 5px 字形 + 1px 间距
}

void Canvas::text(double x, double y, const std::string& s, RGBA c, double scale) {
    double penX = x;
    for (char ch : s) {
        int i = (unsigned char)ch - 32;
        if (i >= 0 && i < 95) {
            for (int row = 0; row < 7; row++) {
                unsigned bits = FONT5x7[i][row];
                for (int col = 0; col < 5; col++) {
                    if ((bits >> (4 - col)) & 1) {
                        fillRect(penX + col * scale, y + row * scale,
                                 scale, scale, c);
                    }
                }
            }
        } else {
            // 未定义字形（如中文）：画占位方框，提示该通道不支持
            rectOutline(penX, y, 5 * scale, 7 * scale, c, 1.0);
        }
        penX += 6 * scale;
    }
}

void Canvas::toRGB(std::vector<uint8_t>& out) const {
    out.assign((size_t)w_ * h_ * 3, 0);
    int s = ss_;
    for (int y = 0; y < h_; y++) {
        for (int x = 0; x < w_; x++) {
            uint32_t r = 0, g = 0, b = 0, a = 0;
            for (int dy = 0; dy < s; dy++)
                for (int dx = 0; dx < s; dx++) {
                    size_t i = idx(x * s + dx, y * s + dy);
                    r += buf_[i]; g += buf_[i+1]; b += buf_[i+2]; a += buf_[i+3];
                }
            uint32_t n = (uint32_t)(s * s);
            size_t o = ((size_t)y * w_ + x) * 3;
            out[o]   = (uint8_t)(r / n);
            out[o+1] = (uint8_t)(g / n);
            out[o+2] = (uint8_t)(b / n);
        }
    }
}

bool Canvas::toPNG(std::vector<uint8_t>& out) const {
    out.clear();
    static const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.insert(out.end(), sig, sig + 8);

    // IHDR: 8-bit RGB，无交错
    uint8_t ihdr[13];
    auto be32 = [](uint8_t* p, uint32_t v) {
        p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
    };
    be32(ihdr, (uint32_t)w_);
    be32(ihdr + 4, (uint32_t)h_);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    pngWriteChunk(out, "IHDR", ihdr, 13);

    // 原始扫描线：每行前置 filter type 0
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h_ * (w_ * 3 + 1));
    std::vector<uint8_t> rgbBuf;
    toRGB(rgbBuf);
    for (int y = 0; y < h_; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgbBuf.begin() + (size_t)y * w_ * 3,
                              rgbBuf.begin() + ((size_t)y + 1) * w_ * 3);
    }
    // zlib 压缩
    uLongf cap = compressBound((uLongf)raw.size());
    std::vector<uint8_t> comp(cap);
    if (compress2(comp.data(), &cap, raw.data(), (uLongf)raw.size(), 9) != Z_OK) return false;
    comp.resize(cap);
    pngWriteChunk(out, "IDAT", comp.data(), comp.size());

    pngWriteChunk(out, "IEND", nullptr, 0);
    return true;
}

} // namespace xl
