#pragma once
// ---------------------------------------------------------------------------
// 软件光栅化画布 + 自写 PNG 编码器。
// 不依赖任何图形库：抗锯齿靠超采样，PNG 靠 zlib + 自算 CRC32。
// ---------------------------------------------------------------------------
#include <vector>
#include <string>
#include <cstdint>

namespace xl {

struct RGBA { uint8_t r, g, b, a; };

inline RGBA rgb(uint8_t r, uint8_t g, uint8_t b) { return RGBA{r, g, b, 255}; }

// 解析 "#RRGGBB" / "#RGB"
bool parseHexColor(const std::string& s, RGBA& out);

class Canvas {
public:
    Canvas(int w, int h, int ss = 3);     // 实际缓冲 w*ss × h*ss，输出时降采样

    int width()  const { return w_; }
    int height() const { return h_; }

    void clear(RGBA c);
    void fillRect(double x, double y, double w, double h, RGBA c);
    void rectOutline(double x, double y, double w, double h, RGBA c, double lw = 1.0);
    void line(double x0, double y0, double x1, double y1, RGBA c, double lw = 1.0);
    // 单像素抗锯齿直线（Xiaolin Wu）。粗线由 line() 沿法向平移多次调用它，
    // 必须拆成独立函数，否则 line() 递归调用自身会无限下潜。
    void lineAA(double x0, double y0, double x1, double y1, RGBA c);
    void polyline(const std::vector<double>& xs, const std::vector<double>& ys,
                  RGBA c, double lw = 1.0);
    void fillPolygon(const std::vector<double>& xs, const std::vector<double>& ys, RGBA c);
    void circle(double cx, double cy, double r, RGBA fill, RGBA stroke, double lw = 1.0);
    // 扇形（饼图）：startAngle/endAngle 为弧度，0 = 3 点方向，逆时针为正
    void pieWedge(double cx, double cy, double r,
                  double a0, double a1, RGBA fill, RGBA stroke, double lw = 1.0);
    void ellipse(double cx, double cy, double rx, double ry,
                 RGBA fill, RGBA stroke, double lw = 1.0);
    // 文本：左上角起点，scale = 像素倍数
    void text(double x, double y, const std::string& s, RGBA c, double scale = 1.0);
    double textWidth(const std::string& s, double scale = 1.0) const;

    // 降采样并编码为 PNG
    bool toPNG(std::vector<uint8_t>& out) const;
    // 降采样后的 RGB 像素（供测试/预览）
    void toRGB(std::vector<uint8_t>& out) const;

private:
    int w_, h_, ss_;
    std::vector<uint8_t> buf_;            // RGBA，尺寸 (w*ss) × (h*ss)
    void blend(int x, int y, RGBA c);
    void blendAA(int x, int y, RGBA c, double cov);
    int idx(int x, int y) const { return (y * w_ * ss_ + x) * 4; }
    bool inBuf(int x, int y) const { return x >= 0 && y >= 0 && x < w_ * ss_ && y < h_ * ss_; }
};

// PNG 编码辅助（自实现 CRC32 + zlib 流）
uint32_t crc32Bytes(const uint8_t* data, size_t len);
void pngWriteChunk(std::vector<uint8_t>& out, const char* type,
                   const uint8_t* data, size_t len);

} // namespace xl
