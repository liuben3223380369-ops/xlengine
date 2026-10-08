#include "view.hpp"
#include "sheet.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace xl {

// ---------------------------------------------------------------------------
// 冻结窗格的派生属性
// ---------------------------------------------------------------------------

// 冻结 N 列 M 行时，topLeftCell 指向未冻结区的第一个格子，
// 也就是 (N, M) —— 注意用的是冻结数量本身，不是 +1
std::string freezeTopLeftCell(int frozenCols, int frozenRows) {
    return colToName(frozenCols) + std::to_string(frozenRows + 1);
}

// activePane 的取值不是随意的：只冻列时是 topRight，只冻行时是 bottomLeft，
// 两者都冻才是 bottomRight。写错了 Excel 会选错初始活动窗格，
// 表现为"打开后光标停在被冻结的区域里"，很怪但不容易定位。
const char* freezeActivePane(int frozenCols, int frozenRows) {
    if (frozenCols > 0 && frozenRows > 0) return "bottomRight";
    if (frozenCols > 0)                   return "topRight";
    return "bottomLeft";
}

// ---------------------------------------------------------------------------
// 片段生成
// ---------------------------------------------------------------------------
std::string buildSheetViewsXml(const FreezePane& f) {
    if (!f.enabled) return std::string();
    if (f.frozenCols <= 0 && f.frozenRows <= 0) return std::string();

    std::ostringstream o;
    o << "<sheetViews>"
      << "<sheetView workbookViewId=\"0\">"
      << "<pane";
    // xSplit 是冻结的列数，ySplit 是冻结的行数。
    // 只冻行时 xSplit 应省略（写 0 也合法，但省略更符合 Excel 的输出）
    if (f.frozenCols > 0) o << " xSplit=\"" << f.frozenCols << "\"";
    if (f.frozenRows > 0) o << " ySplit=\"" << f.frozenRows << "\"";
    o << " topLeftCell=\"" << freezeTopLeftCell(f.frozenCols, f.frozenRows) << "\""
      << " activePane=\"" << freezeActivePane(f.frozenCols, f.frozenRows) << "\""
      << " state=\"frozen\"/>";
    o << "</sheetView>"
      << "</sheetViews>\n";
    return o.str();
}

std::string buildColsXml(const std::map<int, double>& widths) {
    if (widths.empty()) return std::string();

    // 相邻且同宽的列要合并成一个 <col min max>。
    // 不合并的话每列一个 <col>，文件变大且 Excel 里看着碎片化；
    // 真正的原因是这样才与其他实现产出的结构一致，便于比对。
    std::vector<std::pair<int, double>> items(widths.begin(), widths.end());
    std::ostringstream o;
    o << "<cols>\n";
    size_t i = 0;
    while (i < items.size()) {
        size_t j = i;
        while (j + 1 < items.size()
               && items[j + 1].first == items[j].first + 1
               && items[j + 1].second == items[i].second)
            j++;
        o << "<col min=\"" << (items[i].first + 1) << "\" max=\"" << (items[j].first + 1) << "\""
          << " width=\"" << std::setprecision(10) << items[i].second << "\""
          << " customWidth=\"1\"/>\n";
        i = j + 1;
    }
    o << "</cols>\n";
    return o.str();
}

std::string buildAutoFilterXml(const AutoFilter& af) {
    if (!af.enabled) return std::string();
    std::string ref = colToName(af.c0) + std::to_string(af.r0 + 1) + ":"
                    + colToName(af.c1) + std::to_string(af.r1 + 1);
    return "<autoFilter ref=\"" + ref + "\"/>\n";
}

} // namespace xl
