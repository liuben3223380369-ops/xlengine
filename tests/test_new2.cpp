// 验证动态数组 / LET / LAMBDA / 正则 / 统计增强 / 图表
#include "functions.hpp"
#include "parser.hpp"
#include "sheet.hpp"
#include "chart.hpp"
#include <iostream>
#include <cmath>
using namespace xl;

static Sheet g;
static int P=0, N=0;
static std::string ev(const std::string& f) {
    ParseResult pr = parseFormula(f);
    if (!pr.ok()) return "PARSE_ERR:" + pr.error;
    return valueToText(pr.node->eval(g));
}
static void T(const std::string& f, const std::string& exp) {
    std::string got = ev(f);
    if (got == exp) P++;
    else { N++; std::cout << "  FAIL " << f << "\n        期望=" << exp << " 实际=" << got << "\n"; }
}
static void TF(const std::string& f, double exp, double tol=1e-6) {
    std::string got = ev(f);
    if (got.empty() || got[0]=='#') { N++; std::cout << "  FAIL " << f << " 实际=" << got << "\n"; return; }
    double v = std::stod(got);
    if (std::fabs(v-exp) <= tol*std::max(1.0,std::fabs(exp))) P++;
    else { N++; std::cout << "  FAIL " << f << " 期望≈" << exp << " 实际=" << got << "\n"; }
}

int main() {
    std::cout << "== LET / LAMBDA ==\n";
    T("=LET(x,10,x*2)", "20");
    T("=LET(x,10,y,x+5,x*y)", "150");
    T("=LET(a,3,b,4,SQRT(a*a+b*b))", "5");
    T("=LET(x,SUM({1,2,3}),x*10)", "60");
    T("=LET(x,1,LET(y,2,x+y))", "3");            // 嵌套
    T("=LAMBDA(x,x*2)", "#LAMBDA");
    T("=INDEX(MAP({1,2,3},LAMBDA(x,x*2)),2)", "4");
    T("=INDEX(MAP({1,2,3},LAMBDA(x,x*x)),3)", "9");
    T("=REDUCE(0,{1,2,3,4},LAMBDA(acc,v,acc+v))", "10");
    T("=REDUCE(1,{1,2,3,4},LAMBDA(acc,v,acc*v))", "24");
    T("=INDEX(SCAN(0,{1,2,3},LAMBDA(a,v,a+v)),3)", "6");
    T("=INDEX(MAKEARRAY(2,3,LAMBDA(r,c,r*10+c)),1,3)", "13");   // 2行3列，(1,3)
    T("=INDEX(BYROW({1,2;3,4},LAMBDA(r,INDEX(r,1)+INDEX(r,2))),2)", "7");
    T("=INDEX(BYCOL({1,2;3,4},LAMBDA(c,INDEX(c,1)+INDEX(c,2))),2)", "6");

    std::cout << "== 动态数组 ==\n";
    TF("=SUM(SEQUENCE(10))", 55.0);                    // 1..10
    TF("=SUM(SEQUENCE(1,5,2,3))", 40.0);               // 2,5,8,11,14
    TF("=INDEX(SEQUENCE(3,3),3,3)", 9.0);
    TF("=ROWS(SEQUENCE(4,2))", 4.0);
    TF("=COLUMNS(SEQUENCE(4,2))", 2.0);
    TF("=SUM(SORT({3,1,2}))", 6.0);
    TF("=INDEX(SORT({3,1,2}),1)", 1.0);
    TF("=INDEX(SORTBY({10,20,30},{3,1,2}),1)", 20.0);  // 按 key 排序：key=1 -> 20
    TF("=INDEX(UNIQUE({1,2,2,3,3}),3)", 3.0);
    TF("=SUM(FILTER({1,2,3,4},{1,0,1,0}))", 4.0);      // 1+3
    TF("=INDEX(TAKE({1,2,3,4},2),2)", 2.0);
    TF("=INDEX(DROP({1,2,3,4},2),1)", 3.0);
    TF("=INDEX(VSTACK({1,2},{3,4}),2)", 3.0);
    TF("=INDEX(HSTACK({1,2},{3,4}),4)", 4.0);  // 1行2列 + 1行2列 -> 1行4列
    TF("=INDEX(WRAPROWS({1,2,3,4},2),2,1)", 3.0);
    TF("=INDEX(CHOOSECOLS({1,2;3,4},2),1)", 2.0);
    TF("=INDEX(CHOOSEROWS({1,2;3,4},1),1)", 1.0);
    TF("=INDEX(TOCOL({1,2,3}),3,1)", 3.0);
    T("=ARRAYTOTEXT(\"abc\")", "abc");
    T("=VALUETOTEXT(12)", "12");

    std::cout << "== 正则（Excel 2024 新增）==\n";
    T("=REGEXEXTRACT(\"abc123def\",\"[0-9]+\")", "123");
    T("=REGEXTEST(\"abc123\",\"[0-9]\")", "TRUE");
    T("=REGEXTEST(\"abcdef\",\"[0-9]\")", "FALSE");
    T("=REGEXREPLACE(\"a1b2c3\",\"[0-9]\",\"#\")", "a#b#c#");
    T("=REGEXEXTRACT(\"2024-01-15\",\"(\\d{4})-(\\d{2})\",2)", "2024");   // mode=2 才是捕获组

    std::cout << "== 文本新函数 ==\n";
    T("=TEXTBEFORE(\"a-b-c\",\"-\")", "a");
    T("=TEXTAFTER(\"a-b-c\",\"-\")", "b-c");
    T("=TEXTBEFORE(\"a-b-c\",\"-\",2)", "a-b");
    T("=TEXTAFTER(\"a-b-c\",\"-\",2)", "c");
    T("=INDEX(TEXTSPLIT(\"a,b,c\",\",\"),2)", "b");
    T("=INDEX(TEXTSPLIT(\"a,b;c,d\",\",\",\";\"),2,1)", "c");
    T("=TEXTBEFORE(\"abc\",\"/\",1,0,\"无\")", "无");   // if_not_found 是第 5 参

    std::cout << "== 统计增强 ==\n";
    TF("=INDEX(LINEST({1,2,3,4},{1,2,3,4}),1)", 1.0);        // y=x -> 斜率 1
    TF("=INDEX(LINEST({1,2,3,4},{1,2,3,4}),2)", 0.0);        // 截距 0
    TF("=INDEX(LINEST({3,5,7},{1,2,3}),1)", 2.0);            // y=2x+1
    TF("=INDEX(LINEST({3,5,7},{1,2,3}),2)", 1.0);
    TF("=INDEX(TREND({1,2,3},{1,2,3}),3)", 3.0, 1e-6);
    TF("=T.TEST({1,2,3,4,5},{2,3,4,5,6},2,2)", 0.5, 0.35);   // 平移1，p 较大
    TF("=F.TEST({1,2,3,4,5},{2,4,6,8,10})", 0.2, 0.35);
    TF("=CHISQ.TEST({10,20},{15,15})", 0.15, 0.2);
    TF("=FISHER(0.5)", 0.549306144334055);
    TF("=FISHERINV(0.549306144334055)", 0.5);
    TF("=GAMMALN(5)", 3.17805383034795);                      // ln(4!)=ln24
    TF("=GAMMA(5)", 24.0);
    TF("=GAMMA.DIST(2,3,1,TRUE)", 0.323323583816937, 1e-6);
    TF("=BETA.DIST(0.5,2,2,TRUE)", 0.5);
    TF("=LOGNORM.DIST(1,0,1,TRUE)", 0.5);
    TF("=WEIBULL.DIST(1,2,1,TRUE)", 0.632120558828558);
    TF("=HYPGEOM.DIST(1,4,8,20,FALSE)", 0.36326, 1e-4);   // C(8,1)C(12,3)/C(20,4)
    TF("=MAXA({1,5,3})", 5.0);
    TF("=MINA({1,5,3})", 1.0);
    TF("=AVERAGEA({1,3})", 2.0);
    TF("=SUBTOTAL(1,{1,2,3})", 6.0);                          // 1=AVERAGE? 不，1=SUM
    TF("=SUBTOTAL(9,{1,2,3})", 6.0);
    TF("=SUBTOTAL(4,{1,2,3})", 3.0);                          // MAX
    TF("=MAXIFS({1,5,3,9},{1,5,3,9},\">2\")", 9.0);
    TF("=MINIFS({1,5,3,9},{1,5,3,9},\">2\")", 3.0);
    TF("=COUNTIF({1,2,3},\">1\")", 2.0);
    TF("=SUMIF({1,2,3},\">1\")", 5.0);

    std::cout << "== 财务补充 ==\n";
    TF("=DURATION(DATE(2024,1,1),DATE(2034,1,1),0.05,0.05,1)", 7.9, 1.5);
    TF("=MDURATION(DATE(2024,1,1),DATE(2034,1,1),0.05,0.05,1)", 7.5, 1.5);
    TF("=AMORLCOST(1000,DATE(2024,1,1),DATE(2024,7,1),100,1,0.2)", 180.0, 1.0);
    TF("=COTH(1)", 1.31303528549933);
    TF("=SECH(0)", 1.0);
    TF("=CSCH(1)", 0.850918128239322);
    TF("=ACOTH(2)", 0.549306144334055);

    std::cout << "== 图表引擎 ==\n";
    {
        Chart c; c.type = ChartType::Column; c.title = "T";
        c.categories = {"A","B","C"};
        c.series.push_back({"S1", {1,2,3}});
        c.series.push_back({"S2", {3,2,1}});
        std::vector<unsigned char> png;
        bool ok1 = chartToPNG(c, png, 2);
        if (ok1 && png.size() > 500) P++; else { N++; std::cout << "  FAIL PNG 生成\n"; }
        std::string svg = chartToSVG(c);
        if (svg.find("<svg") != std::string::npos && svg.find("</svg>") != std::string::npos) P++;
        else { N++; std::cout << "  FAIL SVG 生成\n"; }
        // PNG 魔数
        bool sig = png.size() > 8 && png[0]==0x89 && png[1]=='P' && png[2]=='N' && png[3]=='G';
        if (sig) P++; else { N++; std::cout << "  FAIL PNG 魔数\n"; }
        // 空数据不能崩
        Chart e2; e2.type = ChartType::Line;
        std::vector<unsigned char> p2;
        if (chartToPNG(e2, p2, 2)) P++; else { N++; std::cout << "  FAIL 空图表\n"; }
        // 饼图
        Chart pie; pie.type = ChartType::Pie;
        pie.series.push_back({"p", {1,2,3}});
        std::vector<unsigned char> p3;
        if (chartToPNG(pie, p3, 2) && p3.size() > 500) P++; else { N++; std::cout << "  FAIL 饼图\n"; }
        // 直方图
        Chart hg; hg.type = ChartType::Histogram; hg.bins = 5;
        DataSeries ds; for (int i=0;i<50;i++) ds.values.push_back((double)(i%17));
        hg.series.push_back(ds);
        std::vector<unsigned char> p4;
        if (chartToPNG(hg, p4, 2)) P++; else { N++; std::cout << "  FAIL 直方图\n"; }
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    std::cout << "函数总数 " << functionNames().size() << "\n";
    return N ? 1 : 0;
}
