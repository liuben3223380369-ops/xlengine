// 生成带嵌入图片的示例 xlsx（图片由图表光栅化而来），供交叉验证
#include "xlsx.hpp"
#include "image.hpp"
#include "chart.hpp"
#include "canvas.hpp"
#include <iostream>
using namespace xl;

int main() {
    Workbook wb;
    Sheet& sh = wb.sheet(0);
    sh.setName("图表附图片");
    for (int i = 0; i < 4; i++) sh.setValue(0, i, Value::str(std::string("Q") + std::to_string(i + 1)));
    double vals[] = {120, 165, 98, 132};
    for (int i = 0; i < 4; i++) sh.setValue(1, i, Value::num(vals[i]));
    sh.recalc();

    // 原生矢量图表
    {
        Chart c;
        c.type = ChartType::Column;
        c.title = "季度销售额";
        c.categories = {"Q1", "Q2", "Q3", "Q4"};
        DataSeries ds;
        ds.name = "销售额";
        for (double v : vals) ds.values.push_back(v);
        c.series.push_back(ds);
        c.anchor.fromCol = 0; c.anchor.fromRow = 3;
        c.anchor.toCol = 7;   c.anchor.toRow = 18;
        wb.addChart(0, c, "季度销售额");
    }

    // 把同一份数据画成 PNG 再嵌入 —— 演示"图表 + 位图"共存
    {
        Canvas cv(320, 200);
        cv.fillRect(0, 0, 320, 200, rgb(250, 250, 252));
        for (int i = 0; i < 4; i++) {
            double h = vals[i] / 180.0 * 150.0;
            cv.fillRect(20 + i * 70, 175 - h, 50, h, rgb(60, 120, 215));
        }
        cv.fillRect(10, 175, 300, 2, rgb(90, 90, 90));
        std::vector<uint8_t> png;
        if (cv.toPNG(png)) {
            ImagePart im;
            im.fromCol = 8; im.fromRow = 3; im.toCol = 12; im.toRow = 12;
            im.data = png; im.ext = "png"; im.name = "销售额缩略图";
            wb.addImage(0, im);
        }
    }

    std::string err;
    if (!wb.save("out/嵌入图片.xlsx", err)) { std::cout << "保存失败: " << err << "\n"; return 1; }
    std::cout << "已生成 out/嵌入图片.xlsx\n";
    std::cout << "  图表 " << wb.chartCount(0) << " 个，图片 "
              << wb.allImages()[0].size() << " 张（"
              << (wb.allImages()[0].empty() ? 0 : (int)wb.allImages()[0][0].data.size())
              << " 字节）\n";
    return 0;
}
