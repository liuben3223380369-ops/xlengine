// 各类型图表 -> PDF 矢量导出
#include "chart.hpp"
#include "chartpdf.hpp"
#include <iostream>
using namespace xl;
int main(){
    const char* fp="/usr/share/fonts/truetype/alibaba-puhuiti/AlibabaPuHuiTi-2-35-Thin.ttf";
    struct Case { ChartType t; const char* name; };
    Case cases[] = {
        {ChartType::Column,"柱形图"}, {ChartType::StackedColumn,"堆积柱形图"},
        {ChartType::Bar,"条形图"}, {ChartType::Line,"折线图"},
        {ChartType::Area,"面积图"}, {ChartType::StackedArea,"堆积面积图"},
        {ChartType::Pie,"饼图"}, {ChartType::Scatter,"散点图"},
        {ChartType::Histogram,"直方图"},
    };
    for (auto& cs : cases){
        Chart c;
        c.type = cs.t;
        c.title = std::string("销售分析 — ") + cs.name;
        c.x.title = "季度"; c.y.title = "销售额";
        c.categories = {"华北","华东","华南","西部"};
        DataSeries s1; s1.name="线上"; s1.values={120,165,143,197};
        DataSeries s2; s2.name="线下"; s2.values={98,132,156,174};
        if (cs.t==ChartType::Pie){ c.series={s1}; }
        else if (cs.t==ChartType::Scatter){ s1.xs={1,2,3,4}; c.series={s1}; }
        else if (cs.t==ChartType::Histogram){
            DataSeries h; h.name="分布";
            for(int i=0;i<40;i++) h.values.push_back(50+ (i*37%100));
            c.series={h}; c.categories.clear();
        } else c.series={s1,s2};
        c.width=900; c.height=560;
        ChartPdfOptions o; o.fontPath=fp;
        std::string err;
        std::string path = std::string("/data/workspace/xlengine/out/pdf_") + cs.name + ".pdf";
        if(!chartToPdf(c,path,o,err)){ std::cout<<"失败 "<<cs.name<<": "<<err<<"\n"; return 1; }
        std::cout<<"  "<<cs.name<<" -> "<<path<<"\n";
    }
    return 0;
}
