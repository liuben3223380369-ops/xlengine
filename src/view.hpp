#pragma once
// ---------------------------------------------------------------------------
// 工作表视图属性：列宽、行高、冻结窗格、自动筛选
//
// 这些是最容易被忽略、但用户打开文件时第一眼就看到的东西。
// 之前的版本完全不写它们，于是：
//   - 别人给的文件里设好的列宽全部丢失
//   - 冻结的表头不再冻结
//   - 筛选箭头消失
// 而自测发现不了 —— 文件照样能打开，自己的 reader 也能读回来。
//
// OOXML 的 CT_Worksheet 序列约束（顺序错了 Excel 报文件损坏）：
//   sheetPr → dimension → sheetViews → sheetFormatPr → cols → sheetData
//   → ... → autoFilter → ... → mergeCells → conditionalFormatting
//   → dataValidations → ... → drawing
//
// 特别注意：cols 必须在 sheetData **之前**，autoFilter 在 sheetData **之后**。
//
// 行高不在这里 —— 它写在 <row ht="..." customHeight="1"/> 属性上。
// ---------------------------------------------------------------------------
#include <map>
#include <string>

namespace xl {

// 冻结窗格
struct FreezePane {
    bool enabled = false;
    int frozenCols = 0;      // 左侧冻结的列数（0 = 不冻结列）
    int frozenRows = 0;      // 顶部冻结的行数（0 = 不冻结行）
};

// 自动筛选
struct AutoFilter {
    bool enabled = false;
    int c0 = 0, r0 = 0, c1 = 0, r1 = 0;
};

// 一张表的视图属性
struct SheetLayout {
    FreezePane freeze;
    AutoFilter filter;
    std::map<int, double> colWidths;    // 列号 -> 宽度（字符数）
    std::map<int, double> rowHeights;   // 行号 -> 高度（磅）

    bool empty() const {
        return !freeze.enabled && !filter.enabled && colWidths.empty() && rowHeights.empty();
    }
};

// ---------------------------------------------------------------------------
// 片段生成（由 buildSheetXml 在正确位置插入）
// ---------------------------------------------------------------------------

// <sheetViews>...</sheetViews>，未启用冻结时返回空串
std::string buildSheetViewsXml(const FreezePane& f);

// <cols>...</cols>，无自定义宽度时返回空串
std::string buildColsXml(const std::map<int, double>& widths);

// <autoFilter ref="..."/>，未启用时返回空串
std::string buildAutoFilterXml(const AutoFilter& af);

// 冻结窗格的 topLeftCell：冻结 N 列 M 行时是 (N, M) 处的地址
std::string freezeTopLeftCell(int frozenCols, int frozenRows);

// 当前活动窗格：只冻行 / 只冻列 / 都冻，取值不同
const char* freezeActivePane(int frozenCols, int frozenRows);

} // namespace xl
