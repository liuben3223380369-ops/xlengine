#pragma once
// ---------------------------------------------------------------------------
// 工作表 -> PDF（栅格与分页）
//
// 分页是这里唯一有难度的部分，规则要照 Excel 的行为来：
//   - 每页重复表头行（否则第二页往上没有列名，看不出是什么）
//   - 按行切分，不按列：列向溢出时横向压缩而不是拆成两张纸
//   - 分页不能把一"行"切在两页中间
// ---------------------------------------------------------------------------
#include <string>
#include "sheet.hpp"
#include "pdf.hpp"

namespace xl {

class Workbook;   // 前向声明，避免 sheetpdf.hpp 反向依赖 xlsx.hpp

struct SheetPdfOptions {
    bool landscape = false;          // 横向（默认纵向）
    bool pageNumbers = false;        // 每页底部加盖"第 N 页 / 共 M 页"
    bool gridLines = true;
    bool showHeaders = true;         // 显示 A/B/C 与 1/2/3 表头
    bool repeatHeaderRow = true;     // 每页重复首行
    double fontSize = 9;
    double headerFontSize = 8;
    double minColWidth = 40;
    double maxColWidth = 160;
    double rowHeight = 16;
    std::string fontPath;            // 为空则用内置回退（ASCII 点阵，中文会缺）
    std::string fallbackFontPath;    // 主字体缺字形时用它补（常见：中文字体缺数字）
    std::string title;
};

// sheet -> PDF。返回 false 时 err 说明原因
bool sheetToPdf(Sheet& sh, const std::string& outPath,
                const SheetPdfOptions& opt, std::string& err);

// 多表导出：所有工作表依次追加到同一个 PDF。
// outSheets 返回实际导出的表数（空表跳过）。
bool workbookToPdf(Workbook& wb, const std::string& outPath,
                   const SheetPdfOptions& opt, std::string& err,
                   int* outSheets = nullptr);

} // namespace xl
