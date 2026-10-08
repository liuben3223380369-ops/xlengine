// 生成各类图表的 PNG / SVG，用于肉眼验证渲染正确性
#include "chart.hpp"
#include <iostream>
#include <fstream>
using namespace xl;

static bool writeFile(const std::string& path, const std::vector<unsigned char>& d) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)d.data(), d.size());
    return true;
}
static bool writeText(const std::string& path, const std::string& s) {
    std::ofstream f(path);
    if (!f) return false;
    f << s;
    return true;
}

int main() {
    struct Case { const char* file; Chart c; };
    std::vector<Case> cases;

    // 1. 簇状柱形：季度销售
    {
        Chart c; c.type = ChartType::Column; c.title = "Quarterly Sales by Region";
        c.categories = {"Q1","Q2","Q3","Q4"};
        c.series.push_back({"North", {120, 165, 143, 197}});
        c.series.push_back({"South", { 98, 132, 156, 174}});
        c.series.push_back({"West",  { 87, 110, 125, 141}});
        c.y.title = "Revenue (K)"; c.x.title = "Quarter";
        c.showLegend = true;
        cases.push_back({"chart_column", c});
    }
    // 2. 折线：12 个月趋势
    {
        Chart c; c.type = ChartType::Line; c.title = "Monthly Active Users";
        c.categories = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        c.series.push_back({"2025", {3200,3450,3610,3900,4120,4080,4360,4610,4890,5120,5340,5610}});
        c.series.push_back({"2024", {2800,2950,3100,3240,3380,3510,3620,3780,3900,4020,4150,4290}});
        c.y.title = "Users"; c.showLegend = true;
        cases.push_back({"chart_line", c});
    }
    // 3. 饼图
    {
        Chart c; c.type = ChartType::Pie; c.title = "Market Share";
        c.categories = {"Alpha","Beta","Gamma","Delta"};
        c.series.push_back({"share", {42, 27, 19, 12}});
        c.showLegend = true;
        cases.push_back({"chart_pie", c});
    }
    // 4. 散点
    {
        Chart c; c.type = ChartType::Scatter; c.title = "Height vs Weight";
        DataSeries s; s.name = "sample";
        s.xs     = {150,160,165,170,175,180,185,190,168,172,178,182};
        s.values = { 50, 55, 58, 62, 66, 70, 74, 79, 60, 64, 68, 72};
        c.series.push_back(s);
        c.x.title = "Height (cm)"; c.y.title = "Weight (kg)";
        cases.push_back({"chart_scatter", c});
    }
    // 5. 堆积柱
    {
        Chart c; c.type = ChartType::StackedColumn; c.title = "Revenue by Product Line";
        c.categories = {"Q1","Q2","Q3","Q4"};
        c.series.push_back({"Hardware", {60, 72, 81, 95}});
        c.series.push_back({"Software", {40, 55, 68, 82}});
        c.series.push_back({"Service",  {22, 28, 35, 41}});
        c.showLegend = true;
        cases.push_back({"chart_stacked", c});
    }
    // 6. 面积
    {
        Chart c; c.type = ChartType::Area; c.title = "Traffic Over Time";
        c.categories = {"1","2","3","4","5","6","7","8"};
        c.series.push_back({"Visits", {1200,1500,1380,1720,1900,1850,2100,2350}});
        cases.push_back({"chart_area", c});
    }
    // 7. 堆积面积
    {
        Chart c; c.type = ChartType::StackedArea; c.title = "Stacked Contributions";
        c.categories = {"W1","W2","W3","W4","W5","W6"};
        c.series.push_back({"A", {30, 35, 40, 38, 42, 46}});
        c.series.push_back({"B", {20, 25, 22, 28, 30, 33}});
        c.series.push_back({"C", {10, 12, 15, 14, 16, 18}});
        c.showLegend = true;
        cases.push_back({"chart_stackedarea", c});
    }
    // 8. 条形
    {
        Chart c; c.type = ChartType::Bar; c.title = "Top Channels";
        c.categories = {"Search","Social","Email","Direct","Referral"};
        c.series.push_back({"Sessions", {5200, 4100, 2600, 1900, 1250}});
        cases.push_back({"chart_bar", c});
    }
    // 9. 直方图
    {
        Chart c; c.type = ChartType::Histogram; c.title = "Score Distribution";
        c.bins = 12;
        DataSeries s; s.name = "scores";
        for (int i = 0; i < 400; i++) {
            double u1 = (double)((i * 1103515245u + 12345u) % 10000u) / 10000.0;
            double u2 = (double)((i * 69069u + 1u) % 10007u) / 10007.0;
            double z = std::sqrt(-2.0 * std::log(u1 + 1e-9)) * std::cos(2 * 3.14159265358979 * u2);
            s.values.push_back(75.0 + z * 10.0);
        }
        c.series.push_back(s);
        c.x.title = "Score"; c.y.title = "Count";
        cases.push_back({"chart_histogram", c});
    }

    int ok = 0;
    for (auto& cs : cases) {
        std::vector<unsigned char> png;
        std::string pngPath = std::string("/data/workspace/xlengine/out/") + cs.file + ".png";
        std::string svgPath = std::string("/data/workspace/xlengine/out/") + cs.file + ".svg";
        fprintf(stderr, "[case] %s png...\n", cs.file);
        bool a = chartToPNG(cs.c, png, 3);
        fprintf(stderr, "[case] %s png done=%d\n", cs.file, (int)a);
        bool b = a && writeFile(pngPath, png);
        fprintf(stderr, "[case] %s svg...\n", cs.file);
        bool d = writeText(svgPath, chartToSVG(cs.c));
        fprintf(stderr, "[case] %s svg done=%d\n", cs.file, (int)d);
        if (a && b && d) { ok++; printf("  %-22s PNG %6zu B   SVG ok\n", cs.file, png.size()); }
        else             printf("  %-22s FAILED\n", cs.file);
    }
    printf("\n生成 %d/%zu 个图表\n", ok, cases.size());
    // ASCII 预览一张，验证数值轴计算
    printf("\n--- ASCII 预览（柱状）---\n%s", chartToASCII(cases[0].c).c_str());
    return ok == (int)cases.size() ? 0 : 1;
}
