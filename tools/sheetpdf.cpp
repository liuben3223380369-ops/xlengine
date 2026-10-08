// 表格 -> PDF 示例：含中文、公式、分页
#include "sheet.hpp"
#include "sheetpdf.hpp"
#include "parser.hpp"
#include <iostream>
using namespace xl;
int main(){
    Sheet sh;
    sh.setName("销售数据");
    sh.setValue(0,0,Value::str("地区"));
    sh.setValue(1,0,Value::str("一季度"));
    sh.setValue(2,0,Value::str("二季度"));
    sh.setValue(3,0,Value::str("三季度"));
    sh.setValue(4,0,Value::str("合计"));
    const char* regs[]={"华北","华东","华南","西部","东北"};
    double d[5][3]={{120,165,143},{98,132,156},{87,110,125},{64,78,92},{55,66,77}};
    for(int r=0;r<5;r++){
        sh.setValue(0,r+1,Value::str(regs[r]));
        for(int c=0;c<3;c++) sh.setValue(c+1,r+1,Value::num(d[r][c]));
        std::string e;
        sh.setFormula(4,r+1,"SUM(B" + std::to_string(r+2) + ":D" + std::to_string(r+2) + ")");
    }
    // 造够多的行，验证分页
    for(int r=6;r<70;r++){
        sh.setValue(0,r,Value::str(std::string("测试行") + std::to_string(r)));
        for(int c=1;c<=4;c++) sh.setValue(c,r,Value::num(r*10+c));
    }

    SheetPdfOptions opt;
    opt.fontPath="/usr/share/fonts/truetype/alibaba-puhuiti/AlibabaPuHuiTi-2-35-Thin.ttf";
    opt.title="销售数据（含分页）";
    opt.landscape=false;
    std::string err;
    if(!sheetToPdf(sh,"/data/workspace/xlengine/out/表格.pdf",opt,err)){
        std::cout<<"失败: "<<err<<"\n"; return 1;
    }
    std::cout<<"已生成 out/表格.pdf\n";
    return 0;
}
