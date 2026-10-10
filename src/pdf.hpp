#pragma once
// ---------------------------------------------------------------------------
// PDF 写入器（PDF 1.4，不依赖任何库）
//
// 三条最容易出错、且错了以后"文件能生成、但没人打得开"的规则：
//   1. xref 表里的每个偏移必须是该对象在文件中真实的字节位置。
//      先拼对象、再回填偏移，不能边写边猜长度。
//   2. 每个 stream 的 /Length 必须是其内容的字节数。
//   3. 内容流里的字符串要做 PDF 转义；非 ASCII 一律走十六进制 <...>。
//
// 字体走 Type0 / CIDFontType2 + Identity-H：
//   CID 就是子集字体里的字形序号，配合 /ToUnicode 才能选中、复制、搜索。
//
// 关于渲染顺序：CID 要等子集化之后才存在，而子集又依赖"整份文档用到哪些字"。
// 所以这里不直接写内容流，而是先把绘制指令缓存下来，save() 时统一
// 收集字符 -> 子集化 -> 编码内容流。直接在 text() 里编码会拿到空的 CID 表。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include "ttf.hpp"

namespace xl {

struct PdfColor {
    double r = 0, g = 0, b = 0;
    static PdfColor black() { return PdfColor{0, 0, 0}; }
    static PdfColor white() { return PdfColor{1, 1, 1}; }
    static PdfColor rgb8(uint8_t r_, uint8_t g_, uint8_t b_) {
        return PdfColor{r_ / 255.0, g_ / 255.0, b_ / 255.0};
    }
};

class PdfWriter {
public:
    PdfWriter();
    ~PdfWriter();

    void setPageSize(double w, double h);      // PDF 点，A4 = 595.28 x 841.89
    void setMargin(double m);
    void setTitle(const std::string& t);

    // 嵌入字体，返回资源名（F1 / F2 ...）
    bool embedFont(const std::string& ttfPath, std::string& resName, std::string& err);

    // 嵌入"备用字体"：主字体缺字形时用它补。
    //
    // 必须有的原因：中文字体常常不含 ASCII 数字（实测 DroidSansFallbackFull.ttf
    // 就没有 0-9 的字形）。只嵌一个字体的话，PDF 里所有数字会变成 .notdef ——
    // 不报错、不崩溃，就是数字整片消失，只有拿 pdftotext 抽一遍才发现。
    bool embedFallback(const std::string& ttfPath, std::string& err);

    // ---- 绘制（作用于当前页）----
    void newPage();
    // 文本：以左上角为起点（屏幕习惯），内部转成 PDF 坐标
    void text(double xLeft, double yTop, const std::string& s, double size,
              const std::string& font = "F1", PdfColor c = PdfColor::black());
    double textWidth(const std::string& s, double size, const std::string& font = "F1") const;
    double ascentRatio(const std::string& font = "F1") const;
    double descentRatio(const std::string& font = "F1") const;

    void line(double x0, double y0, double x1, double y1, PdfColor c, double lw = 0.7);
    void rect(double x, double y, double w, double h, PdfColor stroke, double lw = 0.7);
    void fillRect(double x, double y, double w, double h, PdfColor fill, double alpha = 1.0);
    void rectBoth(double x, double y, double w, double h, PdfColor fill,
                  PdfColor stroke, double lw = 0.7);
    // alpha < 1 走 ExtGState 的 /ca。面积图必须用它：
    // 不透明的话后画的系列会把先画的整个盖住，堆积面积图就只剩最后一个系列。
    void fillPolygon(const std::vector<double>& xs, const std::vector<double>& ys,
                     PdfColor fill, double alpha = 1.0);
    void ellipse(double cx, double cy, double rx, double ry,
                 PdfColor fill, PdfColor stroke, double lw = 0.7);
    void pieWedge(double cx, double cy, double r, double a0, double a1,
                  PdfColor fill, PdfColor stroke, double lw = 0.7);
    void clipRect(double x, double y, double w, double h);

    // 给每页底部加盖"第 N 页 / 共 M 页"。
    //
    // 必须在 save() 之前调用，且只应在所有页面都画完之后调一次：
    // 总页数只有到最后才知道，边画边盖的话第一页写不出"共几页"。
    //
    // cjk=false 时只写 "N / M" —— 未嵌中文字体的 PDF 里写中文会变成乱码，
    // 所以能不能用中文取决于调用方有没有成功嵌入字体。
    void stampPageNumbers(const std::string& font, double size, bool cjk);

    bool save(const std::string& path, std::string& err);

    double pageW() const { return pw_; }
    double pageH() const { return ph_; }
    int pageCount() const;

private:
    enum class OpKind { Text, Line, Rect, FillRect, RectBoth, Poly, Ellipse, Pie, Clip };

    struct Op {
        OpKind kind;
        // 文本
        std::string s; std::string font; double size = 10;
        // 几何（PDF 坐标，已由调用层翻转 Y）
        double x = 0, y = 0, w = 0, h = 0;
        double x1 = 0, y1 = 0;
        double rx = 0, ry = 0, a0 = 0, a1 = 0;
        std::vector<double> xs, ys;
        PdfColor fill, stroke;
        double lw = 0.7;
        double alpha = 1.0;
    };

    struct FontImpl {
        std::string resName;
        std::string baseName;
        std::unique_ptr<TtfFont> tf;
        std::vector<uint8_t> fontData;
        std::map<uint32_t, uint16_t> cids;
    };

    // 透明度状态：alpha 值 -> 资源名（GS0 / GS1 ...）
    std::vector<double> gstates_;
    std::vector<FontImpl> fonts_;
    std::vector<std::vector<Op>> pages_;
    std::vector<Op> cur_;
    std::string fallback_;          // 备用字体资源名（空 = 没有备用）
    double pw_ = 595.28, ph_ = 841.89;
    double mg_ = 24;
    std::string title_;

    // 屏幕坐标（左上原点、Y 向下）-> PDF 坐标（左下原点、Y 向上）
    double flipY(double yTop) const { return ph_ - yTop; }

    FontImpl* fontByName(const std::string& n);
    const FontImpl* fontByName(const std::string& n) const;
    std::string encodeText(const std::string& utf8, const FontImpl& f) const;
    std::string buildContent(const std::vector<Op>& ops);
    // 取（或新建）该 alpha 对应的 ExtGState 资源名
    std::string gstateFor(double alpha);
};

std::string pdfEscape(const std::string& s);

} // namespace xl
