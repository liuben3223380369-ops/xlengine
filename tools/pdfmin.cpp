// 最小 PDF 验证：嵌入中文与拉丁字符，检查文件结构是否可被解析
#include "pdf.hpp"
#include <iostream>
using namespace xl;
int main(int argc, char** argv){
    std::string font = argc>1 ? argv[1] : "/usr/share/fonts/truetype/alibaba-puhuiti/AlibabaPuHuiTi-2-35-Thin.ttf";
    std::string out  = argc>2 ? argv[2] : "/data/workspace/xlengine/out/min.pdf";
    PdfWriter w;
    w.setPageSize(595.28, 841.89);
    w.setTitle("xlengine 最小验证");
    std::string fn, err;
    if(!w.embedFont(font, fn, err)){ std::cout<<"字体失败: "<<err<<"\n"; return 1; }
    std::cout<<"资源名: "<<fn<<"\n";
    w.newPage();
    w.text(60, 80, "Hello PDF 你好世界", 18);
    w.text(60, 120, "混合 ASCII 与中文：销售额 1,234.56", 14);
    w.rect(60, 150, 300, 40, PdfColor::rgb8(0x44,0x72,0xC4), 1.0);
    w.fillRect(60, 200, 100, 30, PdfColor::rgb8(0xED,0x7D,0x31));
    for (int i=0;i<4;i++) w.pieWedge(400, 300, 80, i*1.5708, (i+1)*1.5708,
                                     PdfColor::rgb8(0x44,0x72,0xC4), PdfColor::black(), 0.8);
    if(!w.save(out, err)){ std::cout<<"保存失败: "<<err<<"\n"; return 1; }
    std::cout<<"已生成 "<<out<<"  页数="<<w.pageCount()<<"\n";
    return 0;
}
