// 图表数据源：按单元格区域生成系列。
//
// 这里存在的意义：**图表可以改数据区域了**。
//
// 以前 addChart 把区域里的值读成快照塞进 DataSeries::values，
// 之后再没有线索知道数据来自哪一格 —— 于是"改数据区域"只能删掉重建。
// 现在把源区域记在 Chart::source 里，改区域时按新区域重新生成系列即可。
//
// 系列仍然是快照（不是活引用），所以改单元格数值后图表不会自动更新，
// 这一点没变；变的是"区域本身可编辑"，且编辑路径只有这一份实现。

#include "chart.hpp"
#include "sheet.hpp"
#include "xlsx.hpp"

#include <string>
#include <vector>

namespace xl {

bool rebuildChartFromSource(Sheet& sh, Chart& ch, std::string& err) {
    const ChartSource& src = ch.source;
    if (!src.valid) { err = "该图表没有记录数据源区域"; return false; }
    if (src.c1 < src.c0 || src.r1 < src.r0) { err = "数据源区域无效"; return false; }

    const std::string sn = src.sheetName.empty() ? sh.name() : src.sheetName;
    auto absRange = [&](int a, int b, int c, int d) {
        return sn + "!$" + colToName(a) + "$" + std::to_string(b + 1) +
               ":$" + colToName(c) + "$" + std::to_string(d + 1);
    };

    const int c0 = src.c0, r0 = src.r0, c1 = src.c1, r1 = src.r1;
    const int dataR0 = src.hasHeader ? r0 + 1 : r0;

    std::vector<DataSeries> series;
    std::vector<std::string> categories;

    if (src.catFromFirstCol) {
        // 每列一个系列，首列作分类标签
        for (int c = c0 + 1; c <= c1; c++) {
            DataSeries s;
            std::string nm;
            if (src.hasHeader) nm = sh.display(c, r0);
            s.name = nm.empty() ? ("系列" + std::to_string(c - c0)) : nm;
            s.catRange = absRange(c0, dataR0, c0, r1);
            s.valRange = absRange(c, dataR0, c, r1);
            for (int r = dataR0; r <= r1; r++) {
                Value v = sh.valueAt(c, r);
                s.values.push_back(v.isNum() ? v.n : 0.0);
            }
            series.push_back(std::move(s));
        }
        for (int r = dataR0; r <= r1; r++) categories.push_back(sh.display(c0, r));
    } else {
        // 每行一个系列，首行作分类标签
        for (int r = dataR0; r <= r1; r++) {
            DataSeries s;
            std::string nm;
            if (src.hasHeader) nm = sh.display(c0, r);
            s.name = nm.empty() ? ("系列" + std::to_string(r - r0 + 1)) : nm;
            s.catRange = absRange(c0 + 1, dataR0, c1, dataR0);
            s.valRange = absRange(c0 + 1, r, c1, r);
            for (int c = c0 + 1; c <= c1; c++) {
                Value v = sh.valueAt(c, r);
                s.values.push_back(v.isNum() ? v.n : 0.0);
            }
            series.push_back(std::move(s));
        }
        for (int c = c0 + 1; c <= c1; c++) categories.push_back(sh.display(c, r0));
    }

    if (series.empty()) { err = "选中区域没有可用数据"; return false; }

    ch.series = std::move(series);
    ch.categories = std::move(categories);
    return true;
}

} // namespace xl
