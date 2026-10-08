// 生成带图表的 xlsx：数据 + 图表一起落盘，供 Excel / openpyxl 验证
#include "xlsx.hpp"
#include "chart.hpp"
#include "sheet.hpp"
#include <iostream>

using namespace xl;

int main(int argc, char** argv) {
    std::string path = (argc > 1) ? argv[1] : "/data/workspace/xlengine/out/带图表.xlsx";

    Workbook wb;
    wb.renameSheet(0, "销售数据");
    Sheet& s = wb.sheet(0);

    // ---- 数据区（图表会引用这些区域，所以是联动的）----
    s.setValue(0, 0, Value::str("地区"));
    s.setValue(1, 0, Value::str("Q1"));
    s.setValue(2, 0, Value::str("Q2"));
    s.setValue(3, 0, Value::str("Q3"));
    s.setValue(4, 0, Value::str("Q4"));

    const char* regions[] = {"华北", "华东", "华南", "西部"};
    double data[4][4] = {{120, 165, 143, 197},
                         { 98, 132, 156, 174},
                         { 87, 110, 125, 141},
                         { 64,  78,  92, 108}};
    for (int r = 0; r < 4; r++) {
        s.setValue(0, r + 1, Value::str(regions[r]));
        for (int c = 0; c < 4; c++) s.setValue(c + 1, r + 1, Value::num(data[r][c]));
    }

    // ---- 图表 1：按季度的簇状柱形（引用单元格区域）----
    {
        Chart c;
        c.type = ChartType::Column;
        c.title = "各地区季度销售";
        c.x.title = "季度";
        c.y.title = "销售额";
        c.showLegend = true;
        // 分类标签 = A1:E1 的地区列；这里按"每个季度一个系列"组织
        // 系列 i 的数据 = 第 i+1 季度（B..E 列）的 4 个地区值
        const char* qName[] = {"Q1", "Q2", "Q3", "Q4"};
        for (int q = 0; q < 4; q++) {
            DataSeries ds;
            ds.name = qName[q];
            char buf[64];
            snprintf(buf, sizeof(buf), "销售数据!$%s$2:$%s$5", colToName(q + 1).c_str(), colToName(q + 1).c_str());
            ds.valRange = buf;                       // 引用 B2:B5 等
            ds.catRange = "销售数据!$A$2:$A$5";       // 地区
            c.series.push_back(ds);
        }
        c.anchor.fromCol = 7; c.anchor.fromRow = 1;   // H2 起
        c.anchor.toCol = 15; c.anchor.toRow = 18;
        wb.addChart(0, c, "季度柱形图");
    }

    // ---- 图表 2：各地区全年合计的饼图（内联值，不引用）----
    {
        Chart c;
        c.type = ChartType::Pie;
        c.title = "全年占比";
        c.categories = {regions[0], regions[1], regions[2], regions[3]};
        DataSeries ds;
        ds.name = "全年";
        for (int r = 0; r < 4; r++) {
            double sum = 0;
            for (int q = 0; q < 4; q++) sum += data[r][q];
            ds.values.push_back(sum);
        }
        c.series.push_back(ds);
        c.anchor.fromCol = 7; c.anchor.fromRow = 20;
        c.anchor.toCol = 15; c.anchor.toRow = 36;
        wb.addChart(0, c, "占比饼图");
    }

    std::string err;
    if (!wb.save(path, err)) {
        std::cout << "保存失败: " << err << "\n";
        return 1;
    }
    std::cout << "已生成 " << path << "\n";
    std::cout << "图表数: " << wb.chartCount(0) << "\n";
    for (auto& w : wb.warnings()) std::cout << "  warn: " << w << "\n";

    // 顺便把图表渲染成本地 PNG，便于直接看
    for (size_t i = 0; i < wb.chartCount(0); i++) {
        Chart& c = wb.chartsOf(0)[i];
        // 内联值图表可直接渲染；引用型图表的值在文件里，这里补一份用于渲染
        if (c.series[0].values.empty() && !c.series[0].valRange.empty()) {
            for (size_t si = 0; si < c.series.size(); si++) {
                std::vector<double> v;
                for (int r = 0; r < 4; r++) v.push_back(data[r][si]);
                c.series[si].values = v;
            }
            c.categories = {regions[0], regions[1], regions[2], regions[3]};
        }
        std::vector<unsigned char> png;
        std::string p = "/data/workspace/xlengine/out/xlsx_chart" + std::to_string(i + 1) + ".png";
        if (chartToPNG(c, png, 3)) {
            FILE* f = fopen(p.c_str(), "wb");
            if (f) { fwrite(png.data(), 1, png.size(), f); fclose(f); }
            std::cout << "  渲染 " << p << " (" << png.size() << " B)\n";
        }
    }
    return 0;
}
