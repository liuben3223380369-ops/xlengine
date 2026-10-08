#pragma once
// ---------------------------------------------------------------------------
// 单元格样式
//
// OOXML 的一个 xf 同时编码 numFmtId + fontId + fillId + borderId + alignment，
// 所以"格式码"和"样式"不能各自独立编号 —— 必须是 (格式码, 样式) 的组合对应一个 xf。
// 这是把样式接进 xlsx 时最容易写错的地方：分别编号会导致格式码和样式互相覆盖。
//
// 颜色统一用 RRGGBB 字符串（不含 #）。空串表示"默认/不设置"。
// 之所以不用 int：ARGB 与 RGB 的字节序、以及"有 alpha 还是没有"很容易搞混，
// 而字符串在读写两端都要原样出现，不容易出错。
// ---------------------------------------------------------------------------
#include <string>

namespace xl {

// 水平对齐
enum class HAlign { General, Left, Center, Right, Justify };
// 垂直对齐
enum class VAlign { Bottom, Center, Top };
// 边框线型
enum class BorderStyle { None, Thin, Medium, Thick, Double };

struct CellStyle {
    // ---- 字体 ----
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    int fontSize = 0;                  // 0 = 用默认（11）
    std::string fontColor;             // RRGGBB
    std::string fontName;              // 空 = 用默认（Calibri）

    // ---- 填充 ----
    std::string fillColor;             // RRGGBB，空 = 无填充

    // ---- 对齐 ----
    HAlign hAlign = HAlign::General;
    VAlign vAlign = VAlign::Bottom;
    bool wrapText = false;

    // ---- 边框（四边同一设置，够用且不易误用）----
    BorderStyle border = BorderStyle::None;
    std::string borderColor;           // RRGGBB

    bool empty() const {
        return !bold && !italic && !underline && !strike && fontSize == 0 &&
               fontColor.empty() && fontName.empty() && fillColor.empty() &&
               hAlign == HAlign::General && vAlign == VAlign::Bottom &&
               !wrapText && border == BorderStyle::None && borderColor.empty();
    }

    // 组合键：用于 xf 去重。相同视觉效果的单元格共用一个 xf。
    std::string key() const;
};

// 颜色工具
// "FF0000" / "#FF0000" / "Red" 之类都接受，统一成大写 RRGGBB；失败返回空
std::string normalizeColor(const std::string& s);
// 常用颜色名 -> RRGGBB
std::string colorByName(const std::string& name);

const char* hAlignToOoxml(HAlign a);
const char* vAlignToOoxml(VAlign a);
const char* borderToOoxml(BorderStyle b);
bool ooxmlToHAlign(const std::string& s, HAlign& out);
bool ooxmlToVAlign(const std::string& s, VAlign& out);
bool ooxmlToBorder(const std::string& s, BorderStyle& out);

} // namespace xl
