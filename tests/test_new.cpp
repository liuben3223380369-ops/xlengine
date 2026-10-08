// 新函数的正确性验证：期望值来自 Excel 语义，不是"跑通就行"
#include "functions.hpp"
#include "parser.hpp"
#include "sheet.hpp"
#include "date.hpp"
#include <iostream>
#include <iomanip>
using namespace xl;

static Sheet g;
static int P = 0, N = 0;

static std::string ev(const std::string& f) {
    ParseResult pr = parseFormula(f);
    if (!pr.ok()) return "PARSE_ERR:" + pr.error;
    return valueToText(pr.node->eval(g));
}
static void T(const std::string& f, const std::string& exp) {
    std::string got = ev(f);
    if (got == exp) P++;
    else { N++; std::cout << "  FAIL " << f << "\n        期望=" << exp << "  实际=" << got << "\n"; }
}
// 浮点比较
static void TF(const std::string& f, double exp, double tol = 1e-6) {
    std::string got = ev(f);
    if (got.size() >= 1 && (got[0] == '#')) {
        N++; std::cout << "  FAIL " << f << "  实际=" << got << " 期望≈" << exp << "\n"; return;
    }
    double v = std::stod(got);
    if (std::fabs(v - exp) <= tol * std::max(1.0, std::fabs(exp))) P++;
    else { N++; std::cout << "  FAIL " << f << "  期望≈" << exp << " 实际=" << got << "\n"; }
}

int main(int argc, char** argv) {
    todaySerialRef() = 46000;
    bool verbose = (argc > 1);

    std::cout << "== 数学与三角 ==\n";
    TF("=SIN(0)", 0.0); TF("=COS(0)", 1.0); TF("=TAN(0)", 0.0);
    TF("=DEGREES(PI())", 180.0); TF("=RADIANS(180)", 3.14159265358979);
    TF("=ATAN2(1,1)", 0.785398163397448);          // Excel ATAN2(x,y)=atan2(y,x)
    TF("=FACT(5)", 120.0); TF("=FACT(0)", 1.0); TF("=FACTDOUBLE(5)", 15.0);
    TF("=COMBIN(10,3)", 120.0); TF("=PERMUT(10,3)", 720.0);
    TF("=GCD(12,18)", 6.0); TF("=LCM(4,6)", 12.0);
    TF("=MULTINOMIAL(2,3,4)", 1260.0);
    TF("=QUOTIENT(10,3)", 3.0);
    TF("=TRUNC(-2.7)", -2.0); TF("=CEILING(2.5,2)", 4.0); TF("=FLOOR(2.5,2)", 2.0);
    TF("=EVEN(3)", 4.0); TF("=ODD(2)", 3.0); TF("=MROUND(7,5)", 5.0);
    TF("=SQRTPI(1)", 1.77245385090552);
    T("=ROMAN(1994)", "MCMXCIV"); T("=ARABIC(\"MCMXCIV\")", "1994");
    T("=BASE(255,16)", "FF"); TF("=DECIMAL(\"FF\",16)", 255.0);
    TF("=SUMPRODUCT({1,2},{3,4})", 11.0);
    TF("=SUMXMY2({1,2},{3,4})", 8.0); TF("=SUMX2PY2({1,2},{3,4})", 30.0);
    TF("=SERIESSUM(2,0,1,{1,1,1})", 7.0);
    TF("=LOG(8,2)", 3.0); TF("=LOG(100)", 2.0);

    std::cout << "== 统计 ==\n";
    TF("=STDEV.S({1,2,3,4,5})", 1.58113883008419);
    TF("=VAR.S({1,2,3,4,5})", 2.5);
    TF("=STDEV.P({1,2,3,4,5})", 1.4142135623731);
    TF("=AVEDEV({1,2,3,4})", 1.0);
    TF("=DEVSQ({1,2,3,4})", 5.0);
    TF("=GEOMEAN({2,8})", 4.0);
    TF("=HARMEAN({1,2,4})", 1.71428571428571, 1e-9);
    TF("=MEDIAN({1,2,3,4})", 2.5);
    TF("=PERCENTILE.INC({1,2,3,4},0.5)", 2.5);
    TF("=QUARTILE.INC({1,2,3,4},1)", 1.75);
    TF("=RANK.EQ(3,{1,2,3,4})", 2.0);
    TF("=CORREL({1,2,3},{2,4,6})", 1.0);
    TF("=COVARIANCE.S({1,2,3},{2,4,6})", 2.0);
    TF("=SLOPE({2,4,6},{1,2,3})", 2.0);
    TF("=INTERCEPT({2,4,6},{1,2,3})", 0.0);
    TF("=FORECAST(4,{2,4,6},{1,2,3})", 8.0);
    TF("=STEYX({2,4,6},{1,2,3})", 0.0, 1e-6);
    TF("=STANDARDIZE(10,5,2)", 2.5);
    TF("=SKEW({1,2,3,4,100})", 2.2335, 1e-3);
    T("=MODE.SNGL({1,2,2,3})", "2");
    TF("=TRIMMEAN({1,2,3,4,5},0.4)", 3.0);
    TF("=NORM.S.DIST(0,TRUE)", 0.5);
    TF("=NORM.S.INV(0.975)", 1.95996398454005, 1e-6);
    TF("=NORM.DIST(0,0,1,TRUE)", 0.5);
    TF("=NORM.INV(0.5,0,1)", 0.0, 1e-9);
    TF("=T.DIST(2,10,TRUE)", 0.963306025, 1e-5);
    TF("=T.INV(0.975,10)", 2.228138851, 1e-5);
    TF("=CHISQ.DIST.RT(3.84,1)", 0.0500435, 1e-3);
    TF("=EXPON.DIST(1,1,TRUE)", 0.632120558828558);
    TF("=POISSON.DIST(2,2,FALSE)", 0.270670566473225);
    TF("=BINOM.DIST(3,10,0.5,FALSE)", 0.1171875);
    TF("=CONFIDENCE.NORM(0.05,1,100)", 0.195998, 1e-5);

    std::cout << "== 文本 ==\n";
    T("=PROPER(\"john o'brien\")", "John O'Brien");
    T("=EXACT(\"a\",\"A\")", "FALSE");
    T("=TEXTJOIN(\"-\",TRUE,\"a\",\"\",\"b\")", "a-b");
    TF("=SEARCH(\"b\",\"abc\")", 2.0);
    T("=REPLACE(\"abcd\",2,2,\"X\")", "aXd");
    TF("=CODE(\"A\")", 65.0); T("=CHAR(65)", "A");
    T("=CLEAN(\"a\"&CHAR(9)&\"b\")", "ab");
    T("=FIXED(1234.567,2)", "1234.57");
    T("=DOLLAR(1234.5,1)", "$1234.5");
    T("=T(\"abc\")", "abc"); T("=T(1)", "");
    TF("=NUMBERVALUE(\"1,234.5\",\".\",\",\")", 1234.5);
    T("=TEXT(1234.567,\"0.00\")", "1234.57");
    T("=TEXT(0.5,\"0%\")", "50%");
    T("=TEXT(DATE(2024,3,5),\"yyyy-mm-dd\")", "2024-03-05");
    TF("=UNICODE(\"A\")", 65.0); T("=UNICHAR(65)", "A");

    std::cout << "== 查找引用 ==\n";
    T("=ADDRESS(1,1)", "$A$1"); T("=ADDRESS(2,3,4)", "C2");
    TF("=ROWS({1,2;3,4})", 2.0); TF("=COLUMNS({1,2;3,4})", 2.0);
    TF("=INDEX(TRANSPOSE({1,2,3}),2,1)", 2.0);   // 转置后是 3 行 1 列
    TF("=LOOKUP(3,{1,2,3,4},{10,20,30,40})", 30.0);
    TF("=XLOOKUP(2,{1,2,3},{10,20,30})", 20.0);
    T("=XLOOKUP(9,{1,2},{10,20},\"无\")", "无");
    TF("=XMATCH(3,{1,2,3})", 3.0);
    TF("=INDEX(SORT({3,1,2}),1)", 1.0);
    TF("=INDEX(UNIQUE({1,2,2,3}),3)", 3.0);
    T("=IFS(FALSE,1,TRUE,2)", "2");
    T("=SWITCH(2,1,\"a\",2,\"b\",\"z\")", "b");
    TF("=VLOOKUP(2,{1,10;2,20;3,30},2)", 20.0);

    std::cout << "== 信息 ==\n";
    TF("=TYPE(1)", 1.0); TF("=TYPE(\"a\")", 2.0); TF("=TYPE(TRUE)", 4.0);
    TF("=ERROR.TYPE(#DIV/0!)", 2.0); TF("=ERROR.TYPE(#N/A)", 7.0);
    TF("=N(TRUE)", 1.0); TF("=N(\"a\")", 0.0);
    T("=ISNONTEXT(1)", "TRUE"); T("=ISERR(#N/A)", "FALSE"); T("=ISERR(#REF!)", "TRUE");

    std::cout << "== 工程 ==\n";
    T("=COMPLEX(3,4)", "3+4i");
    TF("=IMABS(\"3+4i\")", 5.0);
    TF("=IMREAL(\"3+4i\")", 3.0); TF("=IMAGINARY(\"3+4i\")", 4.0);
    T("=IMSUM(\"1+2i\",\"3+4i\")", "4+6i");
    T("=IMSUB(\"3+4i\",\"1+2i\")", "2+2i");
    T("=IMPRODUCT(\"1+1i\",\"1+1i\")", "2i");
    T("=IMCONJUGATE(\"3+4i\")", "3-4i");
    // Excel 未给 places 时用**最少位数**，不补零。
    // 这两条原先期望 "0000001010" / "00000000FF"，是照着
    // （错误的）实现写的，不是照着 Excel 写的 —— 审计时已修正实现。
    T("=DEC2BIN(10)", "1010");      T("=BIN2DEC(\"1010\")", "10");
    T("=DEC2HEX(255)", "FF");       T("=HEX2DEC(\"FF\")", "255");
    // 给了 places 才补零到指定位数
    T("=DEC2BIN(10,8)", "00001010");
    T("=DEC2HEX(255,6)", "0000FF");
    // 同上：未给 places 不补零。原期望 "000000000F" 是照错误实现写的
    T("=BIN2HEX(\"1111\")", "F");
    T("=BIN2HEX(\"1111\",4)", "000F");
    T("=OCT2DEC(\"777\")", "511");
    TF("=BITAND(12,10)", 8.0); TF("=BITOR(12,10)", 14.0); TF("=BITXOR(12,10)", 6.0);
    TF("=BITLSHIFT(4,2)", 16.0); TF("=BITRSHIFT(16,2)", 4.0);
    TF("=CONVERT(100,\"C\",\"F\")", 212.0);
    TF("=CONVERT(1,\"m\",\"ft\")", 3.28083989501312);
    TF("=CONVERT(1,\"km\",\"mi\")", 0.621371192237334);
    TF("=CONVERT(0,\"C\",\"K\")", 273.15);
    TF("=BESSELJ(1,1)", 0.440050585744933);
    TF("=ERF(1)", 0.842700792949715);
    TF("=DELTA(5,5)", 1.0); TF("=DELTA(5,4)", 0.0);
    TF("=GESTEP(5,5)", 1.0); TF("=GESTEP(4,5)", 0.0);

    std::cout << "== 财务 ==\n";
    TF("=PMT(0.05,10,1000)", -129.504574965, 1e-6);
    TF("=FV(0.05,10,-100)", 1257.789253, 1e-6);
    TF("=PV(0.05,10,-100)", 772.173493, 1e-6);
    TF("=NPER(0.05,-100,1000)", 14.2067, 1e-4);
    TF("=RATE(10,-200,1000)", 0.150983, 1e-4);
    TF("=NPV(0.1,{100,200,300})", 481.5899, 1e-5);
    TF("=IRR({-1000,300,400,500,600})", 0.2487, 1e-3);
    TF("=MIRR({-1000,300,400,500},0.1,0.12)", 0.09816, 1e-4);
    TF("=SLN(10000,1000,5)", 1800.0);
    TF("=SYD(10000,1000,5,1)", 3000.0);
    TF("=SYD(10000,1000,5,5)", 600.0);
    TF("=EFFECT(0.1,4)", 0.10381289, 1e-7);
    TF("=NOMINAL(0.10381289,4)", 0.1, 1e-7);
    TF("=RRI(5,100,200)", 0.148698355, 1e-7);
    TF("=FVSCHEDULE(100,{0.1,0.2})", 132.0);
    TF("=IPMT(0.05,1,10,1000)", -50.0);
    TF("=PPMT(0.05,1,10,1000)", -79.5046, 1e-4);
    TF("=ISPMT(0.1,1,5,1000)", -80.0);
    TF("=DOLLARDE(1.25,8)", 1.3125);
    TF("=TBILLPRICE(DATE(2024,1,1),DATE(2024,7,1),0.05)", 97.4722, 1e-4);
    TF("=EFFECT(0.06,12)", 0.0616778, 1e-6);

    std::cout << "== 日期（锚点）==\n";
    TF("=DATE(2024,1,1)", 45292.0);
    TF("=DATE(1900,3,1)", 61.0);              // 1900 假闰日导致跳号
    TF("=WEEKDAY(DATE(2024,1,1))", 2.0);      // 周一
    TF("=EDATE(DATE(2024,1,31),1)", 45351.0); // 2024-02-29
    TF("=DAYS360(DATE(2024,1,1),DATE(2024,12,31))", 360.0);

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    std::cout << "函数总数 " << functionNames().size() << "\n";
    return N ? 1 : 0;
}
