#include "pdf.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cstdint>

// M_PI 是 POSIX 扩展，不是 C++ 标准：MinGW 在 -std=c++17（严格 ANSI）下
// 不定义它，于是 Windows 构建会报 "M_PI was not declared in this scope"。
// 这里补齐，用 #ifndef 保护以免与平台自带的重复定义。
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


namespace xl {

namespace {

std::string fmt(double d) {
    if (d == 0) return "0";
    std::ostringstream o;
    o << std::setprecision(6) << d;
    std::string s = o.str();
    if (s == "-0" || s == "-0.000000") s = "0";
    return s;
}
std::string hex4(uint16_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    s += d[(v >> 12) & 0xF]; s += d[(v >> 8) & 0xF];
    s += d[(v >> 4) & 0xF];  s += d[v & 0xF];
    return s;
}
// 数字按 PDF 的十进制整数表示（避免 locale 影响）
std::string intStr(long long v) {
    std::ostringstream o; o << v; return o.str();
}

} // namespace

std::string pdfEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '\\' || c == '(' || c == ')') { o += '\\'; o += (char)c; }
        else if (c < 32) {
            o += '\\';
            o += (char)('0' + ((c >> 6) & 7));
            o += (char)('0' + ((c >> 3) & 7));
            o += (char)('0' + (c & 7));
        } else o += (char)c;
    }
    return o;
}

PdfWriter::PdfWriter() {}
PdfWriter::~PdfWriter() = default;

void PdfWriter::setPageSize(double w, double h) { pw_ = w; ph_ = h; }
void PdfWriter::setMargin(double m) { mg_ = m; }
void PdfWriter::setTitle(const std::string& t) { title_ = t; }

PdfWriter::FontImpl* PdfWriter::fontByName(const std::string& n) {
    for (auto& f : fonts_) if (f.resName == n) return &f;
    return nullptr;
}
const PdfWriter::FontImpl* PdfWriter::fontByName(const std::string& n) const {
    for (auto& f : fonts_) if (f.resName == n) return &f;
    return nullptr;
}

bool PdfWriter::embedFont(const std::string& ttfPath, std::string& resName, std::string& err) {
    auto tf = std::make_unique<TtfFont>();
    if (!tf->load(ttfPath, err)) return false;
    FontImpl f;
    f.resName = "F" + std::to_string(fonts_.size() + 1);
    f.baseName = "SUBSET+" + std::to_string(fonts_.size() + 1);
    f.tf = std::move(tf);
    fonts_.push_back(std::move(f));
    resName = fonts_.back().resName;
    return true;
}

double PdfWriter::ascentRatio(const std::string& font) const {
    const FontImpl* f = fontByName(font);
    if (!f || !f->tf) return 0.8;
    auto& m = f->tf->metrics();
    return (double)m.ascent / (double)m.unitsPerEm;
}
double PdfWriter::descentRatio(const std::string& font) const {
    const FontImpl* f = fontByName(font);
    if (!f || !f->tf) return 0.2;
    auto& m = f->tf->metrics();
    return (double)(-m.descent) / (double)m.unitsPerEm;
}

double PdfWriter::textWidth(const std::string& s, double size, const std::string& font) const {
    const FontImpl* f = fontByName(font);
    if (!f || !f->tf) return 0;
    return f->tf->textWidthEm(s) * size;
}

std::string PdfWriter::encodeText(const std::string& utf8, const FontImpl& f) const {
    std::string hex;
    for (uint32_t cp : TtfFont::decodeUtf8(utf8)) {
        auto it = f.cids.find(cp);
        hex += hex4(it != f.cids.end() ? it->second : 0);
    }
    return hex;
}

// ---------------------------------------------------------------------------
// 绘制指令：只记录，不立即生成 PDF 语法
// ---------------------------------------------------------------------------
void PdfWriter::newPage() {
    // 连续调用 newPage() 会产生空白页。首帧为空时只是"开始第一页"，不应记账。
    if (!cur_.empty()) pages_.push_back(std::move(cur_));
    cur_.clear();
}

void PdfWriter::text(double xLeft, double yTop, const std::string& s, double size,
                     const std::string& font, PdfColor c) {
    Op o;
    o.kind = OpKind::Text;
    o.s = s; o.font = font; o.size = size;
    // 转成 PDF 坐标：基线位置 = 翻转换算后的 y 减去字号
    o.x = xLeft;
    o.y = flipY(yTop) - size;
    o.fill = c;
    cur_.push_back(std::move(o));
}

void PdfWriter::line(double x0, double y0, double x1, double y1, PdfColor c, double lw) {
    Op o; o.kind = OpKind::Line;
    o.x = x0; o.y = flipY(y0); o.x1 = x1; o.y1 = flipY(y1);
    o.stroke = c; o.lw = lw;
    cur_.push_back(std::move(o));
}

void PdfWriter::rect(double x, double y, double w, double h, PdfColor stroke, double lw) {
    Op o; o.kind = OpKind::Rect;
    o.x = x; o.w = w; o.h = h;
    o.y = flipY(y) - h;                 // y 是顶边，翻转换算后要用底边
    o.stroke = stroke; o.lw = lw;
    cur_.push_back(std::move(o));
}

void PdfWriter::fillRect(double x, double y, double w, double h, PdfColor fill, double alpha) {
    Op o; o.kind = OpKind::FillRect;
    o.x = x; o.w = w; o.h = h;
    o.y = flipY(y) - h;
    o.fill = fill; o.alpha = alpha;
    if (alpha < 1.0) gstateFor(alpha);
    cur_.push_back(std::move(o));
}

void PdfWriter::rectBoth(double x, double y, double w, double h, PdfColor fill,
                         PdfColor stroke, double lw) {
    Op o; o.kind = OpKind::RectBoth;
    o.x = x; o.w = w; o.h = h;
    o.y = flipY(y) - h;
    o.fill = fill; o.stroke = stroke; o.lw = lw;
    cur_.push_back(std::move(o));
}

void PdfWriter::fillPolygon(const std::vector<double>& xs, const std::vector<double>& ys,
                            PdfColor fill, double alpha) {
    if (xs.empty() || xs.size() != ys.size()) return;
    Op o; o.kind = OpKind::Poly;
    o.xs = xs; o.ys = ys;
    for (auto& v : o.ys) v = flipY(v);
    o.fill = fill; o.alpha = alpha;
    if (alpha < 1.0) gstateFor(alpha);
    cur_.push_back(std::move(o));
}

std::string PdfWriter::gstateFor(double alpha) {
    for (size_t i = 0; i < gstates_.size(); i++)
        if (std::fabs(gstates_[i] - alpha) < 1e-6) return "GS" + std::to_string(i);
    gstates_.push_back(alpha);
    return "GS" + std::to_string(gstates_.size() - 1);
}

void PdfWriter::ellipse(double cx, double cy, double rx, double ry,
                        PdfColor fill, PdfColor stroke, double lw) {
    Op o; o.kind = OpKind::Ellipse;
    o.x = cx; o.y = flipY(cy); o.rx = rx; o.ry = ry;
    o.fill = fill; o.stroke = stroke; o.lw = lw;
    cur_.push_back(std::move(o));
}

void PdfWriter::pieWedge(double cx, double cy, double r, double a0, double a1,
                         PdfColor fill, PdfColor stroke, double lw) {
    Op o; o.kind = OpKind::Pie;
    // PDF 里 Y 轴向上，因此角度要镜像，否则饼图上下颠倒
    o.x = cx; o.y = flipY(cy); o.rx = r;
    o.a0 = -a1; o.a1 = -a0;
    o.fill = fill; o.stroke = stroke; o.lw = lw;
    cur_.push_back(std::move(o));
}

void PdfWriter::clipRect(double x, double y, double w, double h) {
    Op o; o.kind = OpKind::Clip;
    o.x = x; o.w = w; o.h = h;
    o.y = flipY(y) - h;
    cur_.push_back(std::move(o));
}

// ---------------------------------------------------------------------------
// 生成内容流
// ---------------------------------------------------------------------------
std::string PdfWriter::buildContent(const std::vector<Op>& ops) {
    std::string s;
    bool inClip = false;
    for (const Op& o : ops) {
        switch (o.kind) {
            case OpKind::Text: {
                const FontImpl* f = fontByName(o.font);
                if (!f) break;
                s += "BT /" + o.font + " " + fmt(o.size) + " Tf "
                   + fmt(o.fill.r) + " " + fmt(o.fill.g) + " " + fmt(o.fill.b) + " rg "
                   + fmt(o.x) + " " + fmt(o.y) + " Td <" + encodeText(o.s, *f) + "> Tj ET\n";
                break;
            }
            case OpKind::Line:
                s += "q " + fmt(o.lw) + " w " + fmt(o.stroke.r) + " " + fmt(o.stroke.g)
                   + " " + fmt(o.stroke.b) + " RG " + fmt(o.x) + " " + fmt(o.y) + " m "
                   + fmt(o.x1) + " " + fmt(o.y1) + " l S Q\n";
                break;
            case OpKind::Rect:
                s += "q " + fmt(o.lw) + " w " + fmt(o.stroke.r) + " " + fmt(o.stroke.g)
                   + " " + fmt(o.stroke.b) + " RG " + fmt(o.x) + " " + fmt(o.y) + " "
                   + fmt(o.w) + " " + fmt(o.h) + " re S Q\n";
                break;
            case OpKind::FillRect:
                s += "q ";
                if (o.alpha < 1.0) s += "/" + gstateFor(o.alpha) + " gs ";
                s += fmt(o.fill.r) + " " + fmt(o.fill.g) + " " + fmt(o.fill.b)
                   + " rg " + fmt(o.x) + " " + fmt(o.y) + " " + fmt(o.w) + " " + fmt(o.h)
                   + " re f Q\n";
                break;
            case OpKind::RectBoth:
                s += "q " + fmt(o.lw) + " w " + fmt(o.fill.r) + " " + fmt(o.fill.g)
                   + " " + fmt(o.fill.b) + " rg " + fmt(o.stroke.r) + " " + fmt(o.stroke.g)
                   + " " + fmt(o.stroke.b) + " RG " + fmt(o.x) + " " + fmt(o.y) + " "
                   + fmt(o.w) + " " + fmt(o.h) + " re B Q\n";
                break;
            case OpKind::Poly: {
                if (o.xs.empty()) break;
                s += "q ";
                if (o.alpha < 1.0) s += "/" + gstateFor(o.alpha) + " gs ";
                s += fmt(o.fill.r) + " " + fmt(o.fill.g) + " " + fmt(o.fill.b) + " rg ";
                for (size_t i = 0; i < o.xs.size(); i++)
                    s += (i ? " " : "") + fmt(o.xs[i]) + " " + fmt(o.ys[i]) + (i ? " l" : " m");
                s += " h f Q\n";
                break;
            }
            case OpKind::Ellipse: {
                if (o.rx <= 0 || o.ry <= 0) break;
                double k = 0.5523, kx = o.rx * k, ky = o.ry * k;
                double x0 = o.x - o.rx, y0 = o.y - o.ry;
                double x1 = o.x + o.rx, y1 = o.y + o.ry;
                s += "q " + fmt(o.lw) + " w " + fmt(o.fill.r) + " " + fmt(o.fill.g) + " "
                   + fmt(o.fill.b) + " rg " + fmt(o.stroke.r) + " " + fmt(o.stroke.g) + " "
                   + fmt(o.stroke.b) + " RG ";
                s += fmt(o.x) + " " + fmt(y0) + " m ";
                s += fmt(o.x + kx) + " " + fmt(y0) + " " + fmt(x1) + " " + fmt(o.y - ky)
                   + " " + fmt(x1) + " " + fmt(o.y) + " c ";
                s += fmt(x1) + " " + fmt(o.y + ky) + " " + fmt(o.x + kx) + " " + fmt(y1)
                   + " " + fmt(o.x) + " " + fmt(y1) + " c ";
                s += fmt(o.x - kx) + " " + fmt(y1) + " " + fmt(x0) + " " + fmt(o.y + ky)
                   + " " + fmt(x0) + " " + fmt(o.y) + " c ";
                s += fmt(x0) + " " + fmt(o.y - ky) + " " + fmt(o.x - kx) + " " + fmt(y0)
                   + " " + fmt(o.x) + " " + fmt(y0) + " c ";
                s += "h B Q\n";
                break;
            }
            case OpKind::Pie: {
                if (o.rx <= 0) break;
                double sweep = o.a1 - o.a0;
                if (sweep <= 1e-9) break;
                int seg = (int)std::ceil(sweep / (M_PI / 12));
                if (seg < 2) seg = 2;
                double k = (4.0 / 3.0) * std::tan(sweep / seg / 4.0) * o.rx;
                s += "q " + fmt(o.lw) + " w " + fmt(o.fill.r) + " " + fmt(o.fill.g) + " "
                   + fmt(o.fill.b) + " rg " + fmt(o.stroke.r) + " " + fmt(o.stroke.g) + " "
                   + fmt(o.stroke.b) + " RG ";
                s += fmt(o.x) + " " + fmt(o.y) + " m ";
                double px = o.x + o.rx * std::cos(o.a0), py = o.y + o.rx * std::sin(o.a0);
                s += fmt(px) + " " + fmt(py) + " l ";
                for (int i = 0; i < seg; i++) {
                    double t0 = o.a0 + sweep * i / seg, t1 = o.a0 + sweep * (i + 1) / seg;
                    double x0 = o.x + o.rx * std::cos(t0), y0 = o.y + o.rx * std::sin(t0);
                    double x1 = o.x + o.rx * std::cos(t1), y1 = o.y + o.rx * std::sin(t1);
                    double c0x = x0 - k * std::sin(t0), c0y = y0 + k * std::cos(t0);
                    double c1x = x1 + k * std::sin(t1), c1y = y1 - k * std::cos(t1);
                    s += fmt(c0x) + " " + fmt(c0y) + " " + fmt(c1x) + " " + fmt(c1y)
                       + " " + fmt(x1) + " " + fmt(y1) + " c ";
                }
                s += "h B Q\n";
                break;
            }
            case OpKind::Clip:
                s += "q " + fmt(o.x) + " " + fmt(o.y) + " " + fmt(o.w) + " " + fmt(o.h)
                   + " re W n\n";
                inClip = true;
                (void)inClip;
                break;
        }
    }
    return s;
}

int PdfWriter::pageCount() const {
    return (int)pages_.size() + (cur_.empty() ? 0 : 1);
}

// ---------------------------------------------------------------------------
// 落盘
// ---------------------------------------------------------------------------
bool PdfWriter::save(const std::string& path, std::string& err) {
    std::vector<std::vector<Op>> all = pages_;
    if (!cur_.empty()) all.push_back(cur_);
    if (all.empty()) { err = "没有内容"; return false; }

    // ---- 1) 收集所有字符码点，做字体子集 ----
    std::vector<uint32_t> used;
    for (auto& pg : all) {
        for (auto& o : pg) {
            if (o.kind != OpKind::Text) continue;
            const FontImpl* f = fontByName(o.font);
            if (!f || !f->tf) continue;
            for (uint32_t cp : TtfFont::decodeUtf8(o.s)) used.push_back(cp);
        }
    }
    used.push_back(0x20);                       // 保证空格可用
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());

    for (auto& f : fonts_) {
        if (!f.tf) continue;
        std::string e2;
        if (!f.tf->subset(used, f.fontData, f.cids, e2)) {
            err = "字体子集化失败: " + e2;
            return false;
        }
    }

    // ---- 2) 生成各页内容流 ----
    std::vector<std::string> contents;
    for (auto& pg : all) contents.push_back(buildContent(pg));

    // ---- 3) 组装对象 ----
    // 对象编号：
    //   1 = Catalog
    //   2 = Pages
    //   3..(2+N) = Page
    //   之后：每页一个 Contents
    //   之后：每个字体 4 个对象（Type0, CIDFontType2, FontDescriptor, FontFile2, ToUnicode = 5）
    int nPages = (int)contents.size();
    int firstContent = 3 + nPages;
    int firstGs = firstContent + nPages;
    int firstFont = firstGs + (int)gstates_.size();
    const int kFontObjs = 5;

    std::vector<std::string> objs;                 // 下标 0 -> 对象 1
    // 含二进制的对象（FontFile2）：正文之外还要原样拼一段字节。
    // 直接 append 到 std::string 没问题，但先存 stream 头尾更清晰。
    std::map<int, const std::vector<uint8_t>*> rawStreamObjs;
    auto need = [&](int n) { while ((int)objs.size() < n) objs.push_back(std::string()); };

    int totalObjs = firstFont + (int)fonts_.size() * kFontObjs - 1;
    need(totalObjs);

    objs[0] = "<< /Type /Catalog /Pages 2 0 R >>";

    std::string kids;
    for (int i = 0; i < nPages; i++)
        kids += (i ? " " : "") + intStr(3 + i) + " 0 R";
    objs[1] = "<< /Type /Pages /Count " + intStr(nPages) + " /Kids [" + kids + "] >>";

    // ExtGState 资源（透明度）
    std::vector<int> gsObjNums;
    std::string gsRes;
    {
        // 每个 ExtGState 一个对象，排在字体之前
        int gsBase = 3 + nPages + nPages;
        for (size_t i = 0; i < gstates_.size(); i++) {
            gsObjNums.push_back(gsBase + (int)i);
            gsRes += " /GS" + std::to_string(i) + " " + intStr(gsBase + (int)i) + " 0 R";
        }
    }

    // 字体资源字典（所有页共用）
    std::string fontRes;
    for (size_t i = 0; i < fonts_.size(); i++) {
        int base = firstFont + (int)i * kFontObjs;
        fontRes += " /" + fonts_[i].resName + " " + intStr(base) + " 0 R";
    }

    for (int i = 0; i < nPages; i++) {
        objs[2 + i] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
                    + fmt(pw_) + " " + fmt(ph_) + "] /Resources << /Font <<"
                    + fontRes + " >>";
        if (!gsRes.empty()) objs[2 + i] += " /ExtGState <<" + gsRes + " >>";
        objs[2 + i] += " >> /Contents " + intStr(firstContent + i) + " 0 R >>";
    }

    // 内容流对象：/Length 必须是真实字节数
    for (int i = 0; i < nPages; i++) {
        const std::string& c = contents[i];
        objs[firstContent - 1 + i] = "<< /Length " + intStr((long long)c.size())
                                   + " >>\nstream\n" + c + "endstream";
    }
    // ExtGState 对象
    for (size_t i = 0; i < gstates_.size(); i++)
        objs[firstGs - 1 + i] = "<< /Type /ExtGState /ca " + fmt(gstates_[i])
                              + " /CA " + fmt(gstates_[i]) + " >>";

    // 字体对象
    for (size_t i = 0; i < fonts_.size(); i++) {
        FontImpl& f = fonts_[i];
        int base = firstFont + (int)i * kFontObjs;
        auto& m = f.tf->metrics();
        // CID 顺序：按 cids 里的字形序号排，取宽度
        std::vector<uint32_t> wids(f.cids.size() ? (uint32_t)f.cids.size() + 1 : 1, 0);
        // 建立 GID -> 宽度
        std::map<uint16_t, uint32_t> gidW;
        for (auto& kv : f.cids) {
            uint16_t g = kv.second;
            gidW[g] = 0;                                   // 占位，稍后从原字体取
        }
        // 用原字体的 advance（子集宽度与原字体一致）
        for (auto& kv : f.cids) {
            uint32_t cp = kv.first;
            uint16_t origG = f.tf->glyphFor(cp);
            gidW[kv.second] = (uint32_t)std::llround(f.tf->advanceOf(origG) * 1000.0);
        }
        uint16_t maxG = 0;
        for (auto& kv : f.cids) maxG = std::max(maxG, kv.second);
        // /W 的格式是 [c [w1 w2 ...]] 或 [c_first c_last w]，
        // 不能写成一串裸数字 —— 那样 PDF 会把它当 range 三元组解析，
        // 解析器要么报错要么取到完全错误的宽度。
        std::string widths = "0 [";
        for (uint16_t g = 0; g <= maxG; g++) {
            auto it = gidW.find(g);
            widths += intStr(it != gidW.end() ? it->second : (g == 0 ? 1000 : 500));
            if (g != maxG) widths += " ";
        }
        widths += "]";

        // 0: Type0
        objs[base - 1] = "<< /Type /Font /Subtype /Type0 /BaseFont /" + f.baseName
                       + " /Encoding /Identity-H /DescendantFonts [" + intStr(base + 1)
                       + " 0 R] /ToUnicode " + intStr(base + 4) + " 0 R >>";
        // 1: CIDFontType2
        objs[base] = "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + f.baseName
                   + " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>"
                   + " /CIDToGIDMap /Identity /FontDescriptor " + intStr(base + 2)
                   + " 0 R /DW 1000 /W [" + widths + "] >>";
        // 2: FontDescriptor
        std::string bbox = "[ " + intStr(m.bboxXMin) + " " + intStr(m.bboxYMin) + " "
                         + intStr(m.bboxXMax) + " " + intStr(m.bboxYMax) + " ]";
        objs[base + 1] = "<< /Type /FontDescriptor /FontName /" + f.baseName
                       + " /Flags 4 /FontBBox " + bbox
                       + " /ItalicAngle 0 /Ascent " + intStr(m.ascent)
                       + " /Descent " + intStr(m.descent)
                       + " /CapHeight " + intStr(m.ascent)
                       + " /StemV 80 /FontFile2 " + intStr(base + 3) + " 0 R >>";
        // 3: FontFile2（流）
        {
            // fontData 是二进制，不能走 std::string 的 +=（会截断在 \0 处）
            std::string body = "<< /Length " + intStr((long long)f.fontData.size())
                             + " /Length1 " + intStr((long long)f.fontData.size())
                             + " >>\nstream\n";
            objs[base + 2] = body;
            // objs 是 0 基下标、对象号 = 下标+1，因此 objs[base+2] 对应对象号 base+3。
            // 这里必须用对象号做 key，写成 base+2 会让二进制挂到 FontDescriptor 上，
            // 结果 FontFile2 是空流而字体丢了 —— 文件照样能生成，只是字全画不出来。
            rawStreamObjs[base + 3] = &f.fontData;
        }
        // 4: ToUnicode CMap（流）
        {
            std::ostringstream cmap;
            cmap << "/CIDInit /ProcSet findresource begin\n"
                 << "12 dict begin\nbegincmap\n"
                 << "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                 << "/CMapName /Adobe-Identity-UCS def\n"
                 << "/CMapType 2 def\n"
                 << "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
            // 分批，每批最多 100 条
            std::vector<std::pair<uint16_t, uint32_t>> pairs;
            for (auto& kv : f.cids) pairs.push_back({kv.second, kv.first});
            std::sort(pairs.begin(), pairs.end());
            const size_t kBatch = 100;
            for (size_t b = 0; b < pairs.size(); b += kBatch) {
                size_t e = std::min(pairs.size(), b + kBatch);
                cmap << intStr((long long)(e - b)) << " beginbfchar\n";
                for (size_t k = b; k < e; k++)
                    cmap << "<" << hex4(pairs[k].first) << "> <"
                         << hex4((uint16_t)pairs[k].second) << ">\n";
                cmap << "endbfchar\n";
            }
            cmap << "endcmap\nCMapName currentdict /CMap defineresource pop\n"
                 << "end\nend\n";
            std::string body = cmap.str();
            std::string o = "<< /Length " + intStr((long long)body.size())
                          + " >>\nstream\n" + body + "endstream";
            objs[base + 3] = o;
        }
    }

    // ---- 4) 写文件并回填 xref 偏移 ----
    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<long long> offsets(objs.size() + 1, 0);
    for (size_t i = 0; i < objs.size(); i++) {
        offsets[i + 1] = (long long)out.size();
        out += intStr((long long)i + 1) + " 0 obj\n";
        out += objs[i];
        auto rit = rawStreamObjs.find((int)i + 1);
        if (rit != rawStreamObjs.end()) {
            const std::vector<uint8_t>& raw = *rit->second;
            out.append((const char*)raw.data(), raw.size());
            out += "\nendstream";
        }
        out += "\nendobj\n";
    }
    long long xrefPos = (long long)out.size();
    std::ostringstream xr;
    xr << "xref\n0 " << (objs.size() + 1) << "\n";
    xr << "0000000000 65535 f \n";
    for (size_t i = 1; i <= objs.size(); i++) {
        std::ostringstream o;
        o << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
        xr << o.str();
    }
    out += xr.str();
    out += "trailer\n<< /Size " + intStr((long long)objs.size() + 1)
         + " /Root 1 0 R";
    if (!title_.empty()) out += " /Info << /Title (" + pdfEscape(title_) + ") >>";
    out += " >>\nstartxref\n" + intStr(xrefPos) + "\n%%EOF\n";

    std::ofstream f(path, std::ios::binary);
    if (!f) { err = "无法写入: " + path; return false; }
    f.write(out.data(), (std::streamsize)out.size());
    if (!f) { err = "写入失败"; return false; }
    return true;
}

} // namespace xl
