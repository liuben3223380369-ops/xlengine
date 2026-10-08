#include "sheetpdf.hpp"
#include "pdf.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdint>

namespace xl {

namespace {

// 宽字符按 2 倍计（与引擎里 LENB 的口径一致）。
// 用于估算列宽：中文列如果按字符数算会窄一半，文字被截断。
double displayWidth(const std::string& s, double fontSize) {
    double w = 0;
    for (uint32_t cp : TtfFont::decodeUtf8(s)) {
        bool wide = (cp >= 0x1100 && cp <= 0x115F) ||
                    (cp >= 0x2E80 && cp <= 0x303E) ||
                    (cp >= 0x3041 && cp <= 0x33FF) ||
                    (cp >= 0x3400 && cp <= 0x4DBF) ||
                    (cp >= 0x4E00 && cp <= 0x9FFF) ||
                    (cp >= 0xAC00 && cp <= 0xD7A3) ||
                    (cp >= 0xF900 && cp <= 0xFAFF) ||
                    (cp >= 0xFF00 && cp <= 0xFF60);
        w += wide ? 1.0 : 0.55;
    }
    return w * fontSize;
}

}

bool sheetToPdf(Sheet& sh, const std::string& outPath,
                const SheetPdfOptions& opt, std::string& err) {
    // ---- 1) 找出有数据的范围 ----
    const auto& cells = sh.allCells();
    // 只有标题没有单元格的 PDF 没有意义，直接拒绝
    if (cells.empty()) { err = "工作表为空"; return false; }
    int minC = 1 << 30, maxC = -1, minR = 1 << 30, maxR = -1;
    for (auto& kv : cells) {
        minC = std::min(minC, kv.first.first);  maxC = std::max(maxC, kv.first.first);
        minR = std::min(minR, kv.first.second); maxR = std::max(maxR, kv.first.second);
    }
    if (maxC < 0) { minC = maxC = 0; minR = maxR = 0; }

    PdfWriter w;
    double pw = 595.28, ph = 841.89;
    if (opt.landscape) std::swap(pw, ph);
    w.setPageSize(pw, ph);
    w.setMargin(28);
    if (!opt.title.empty()) w.setTitle(opt.title);

    std::string font = "F1";
    if (!opt.fontPath.empty()) {
        std::string e2;
        if (!w.embedFont(opt.fontPath, font, e2)) {
            err = "字体嵌入失败: " + e2;
            return false;
        }
    }

    // ---- 2) 列宽：按内容自适应 ----
    int nCols = maxC - minC + 1;
    std::vector<double> colW(nCols, opt.minColWidth);
    // 行表头宽度
    double rowHeadW = opt.showHeaders ? std::max(24.0, opt.fontSize * 2.6) : 0;

    for (int c = 0; c < nCols; c++) {
        double need = opt.minColWidth;
        // 列表头（A/B/C）也要装得下
        if (opt.showHeaders)
            need = std::max(need, displayWidth(colToName(minC + c), opt.headerFontSize) + 8);
        for (int r = minR; r <= maxR; r++) {
            if (!sh.hasCell(minC + c, r)) continue;
            std::string t = sh.display(minC + c, r);
            need = std::max(need, displayWidth(t, opt.fontSize) + 10);
        }
        colW[c] = std::min(need, opt.maxColWidth);
    }
    double tableW = rowHeadW;
    for (double cw : colW) tableW += cw;

    // 表格比页面宽时按比例压缩，而不是拆到下一页（Excel 的行为是横向缩放）
    double usable = pw - 56;
    if (tableW > usable && tableW > 0) {
        double k = usable / tableW;
        rowHeadW *= k;
        for (auto& cw : colW) cw *= k;
        tableW = usable;
    }

    // ---- 3) 分页：按行切 ----
    double topMargin = 40;
    if (!opt.title.empty()) topMargin += 26;
    double headerH = opt.showHeaders ? opt.rowHeight : 0;
    double bottomLimit = ph - 40;
    double avail = bottomLimit - topMargin - headerH;
    int rowsPerPage = std::max(1, (int)std::floor(avail / opt.rowHeight));
    // 首页要重复表头，可容纳的行数少一行
    int nRows = maxR - minR + 1;

    auto drawHeaderRow = [&](double y0) {
        if (!opt.showHeaders) return;
        double x = 28;
        PdfColor headBg = PdfColor::rgb8(0xF2, 0xF2, 0xF2);
        PdfColor line = PdfColor::rgb8(0xBF, 0xBF, 0xBF);
        if (rowHeadW > 0) {
            w.rectBoth(x, y0, rowHeadW, headerH, headBg, line, 0.5);
            x += rowHeadW;
        }
        for (int c = 0; c < nCols; c++) {
            w.rectBoth(x, y0, colW[c], headerH, headBg, line, 0.5);
            std::string t = colToName(minC + c);
            double tw = w.textWidth(t, opt.headerFontSize, font);
            w.text(x + std::max(2.0, (colW[c] - tw) / 2), y0 + 3.5, t,
                   opt.headerFontSize, font, PdfColor::rgb8(0x60, 0x60, 0x60));
            x += colW[c];
        }
    };

    bool first = true;
    for (int r0 = minR; r0 <= maxR; r0 += rowsPerPage) {
        if (!first) w.newPage();
        first = false;

        double y = topMargin;
        if (!opt.title.empty()) {
            w.text(28, 20, opt.title, 14, font, PdfColor::rgb8(0x33, 0x33, 0x33));
        }
        drawHeaderRow(y);
        y += headerH;

        int rEnd = std::min(maxR, r0 + rowsPerPage - 1);
        for (int r = r0; r <= rEnd; r++, y += opt.rowHeight) {
            double x = 28;
            PdfColor line = PdfColor::rgb8(0xD0, 0xD0, 0xD0);
            PdfColor numC = PdfColor::rgb8(0x00, 0x00, 0xC0);
            PdfColor txtC = PdfColor::black();

            if (rowHeadW > 0) {
                if (opt.gridLines)
                    w.rectBoth(x, y, rowHeadW, opt.rowHeight, PdfColor::rgb8(0xF2, 0xF2, 0xF2), line, 0.5);
                std::string rn = std::to_string(r + 1);
                double tw = w.textWidth(rn, opt.headerFontSize, font);
                w.text(x + std::max(2.0, (rowHeadW - tw) / 2), y + 3.5, rn,
                       opt.headerFontSize, font, PdfColor::rgb8(0x60, 0x60, 0x60));
                x += rowHeadW;
            }
            for (int c = 0; c < nCols; c++) {
                int col = minC + c;
                if (opt.gridLines) w.rect(x, y, colW[c], opt.rowHeight, line, 0.4);
                if (!sh.hasCell(col, r)) { x += colW[c]; continue; }
                std::string t = sh.display(col, r);
                if (t.empty()) { x += colW[c]; continue; }
                Value v = sh.valueAt(col, r);
                bool isNum = v.isNum();
                double tw = w.textWidth(t, opt.fontSize, font);
                // 数字右对齐、文本左对齐（Excel 默认）
                double tx = isNum ? std::max(2.0, colW[c] - tw - 4) : 4;
                if (tw > colW[c] - 6) {
                    // 放不下就截断并加省略号，避免溢出到相邻单元格
                    std::string cut;
                    double acc = 0;
                    for (uint32_t cp : TtfFont::decodeUtf8(t)) {
                        std::string one;
                        // 单字符宽度
                        double cw = w.textWidth(std::string(1, (char)(cp < 128 ? cp : 'M')),
                                                opt.fontSize, font);
                        if (acc + cw > colW[c] - 10) break;
                        acc += cw;
                        // 重新编码该码点
                        if (cp < 0x80) one.push_back((char)cp);
                        else if (cp < 0x800) {
                            one.push_back((char)(0xC0 | (cp >> 6)));
                            one.push_back((char)(0x80 | (cp & 0x3F)));
                        } else {
                            one.push_back((char)(0xE0 | (cp >> 12)));
                            one.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                            one.push_back((char)(0x80 | (cp & 0x3F)));
                        }
                        cut += one;
                    }
                    cut += "...";
                    t = cut;
                }
                w.text(x + tx, y + 3.5, t, opt.fontSize, font, isNum ? numC : txtC);
                x += colW[c];
            }
        }
    }

    if (!w.save(outPath, err)) return false;
    (void)nRows;
    return true;
}

} // namespace xl
