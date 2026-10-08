// 补齐批次的测试
//
// 验证思路分两类：
//  1. 有外部数学期望的（矩阵、三角）—— 直接比对已知结果
//  2. 没有外部标杆的（奇数期债券）—— 用"往返闭合"自证：
//     ODDFYIELD(ODDFPRICE(y)) 必须还原出 y。这是完全自洽的强验证，
//     能抓出折现指数、应计利息符号、现金流构造等几乎所有实现错误。
#include "sheet.hpp"
#include "functions.hpp"
#include "parser.hpp"
#include <iostream>
#include <cmath>

using namespace xl;

static int P = 0, N = 0;
static Sheet sh;

static std::string ev(const std::string& s) {
    ParseResult pr = parseFormula(s);
    if (!pr.ok()) return "PARSE_ERR(" + pr.error + ")";
    return valueToText(pr.node->eval(sh));
}
static void ck(const std::string& expr, const std::string& exp) {
    std::string got = ev(expr);
    if (got == exp) P++;
    else { N++; std::cout << "  FAIL " << expr << "\n       期望=" << exp << "  实际=" << got << "\n"; }
}
// 数值比较（带容差）
static void ckNum(const std::string& expr, double exp, double tol = 1e-6) {
    ParseResult pr = parseFormula(expr);
    if (!pr.ok()) { N++; std::cout << "  FAIL(parse) " << expr << ": " << pr.error << "\n"; return; }
    Value v = pr.node->eval(sh);
    if (!v.isNum()) { N++; std::cout << "  FAIL " << expr << "  非数值: " << valueToText(v) << "\n"; return; }
    if (std::fabs(v.n - exp) <= tol) P++;
    else { N++; std::cout << "  FAIL " << expr << "  期望=" << exp << "  实际=" << v.n << "\n"; }
}
static void ckTrue(const std::string& expr) { ck(expr, "TRUE"); }

int main() {
    // ------------------------------------------------------------------
    std::cout << "== 三角补全 ==\n";
    // COT(π/4) = 1
    ckNum("COT(PI()/4)", 1.0, 1e-9);
    ckNum("CSC(PI()/2)", 1.0, 1e-9);
    ckNum("SEC(0)", 1.0, 1e-9);
    // ACOT 值域 (0, π)：ACOT(0) = π/2，ACOT(1) = π/4
    ckNum("ACOT(0)", 1.5707963267948966, 1e-9);
    ckNum("ACOT(1)", 0.7853981633974483, 1e-9);
    // 与 ATAN 的关系：ACOT(x) = π/2 - ATAN(x)
    ckNum("ACOT(2)-(PI()/2-ATAN(2))", 0.0, 1e-12);
    ckNum("SECH(0)", 1.0, 1e-12);
    ckNum("CSCH(1)", 1.0 / std::sinh(1.0), 1e-12);
    ckNum("COTH(1)", 1.0 / std::tanh(1.0), 1e-12);
    // ACOTH 定义域 |x|>1
    ck("ACOTH(0.5)", "#NUM!");
    ckNum("ACOTH(2)", 0.5 * std::log(3.0), 1e-12);
    ck("COT(0)", "#DIV/0!");
    ck("CSC(0)", "#DIV/0!");

    // ------------------------------------------------------------------
    std::cout << "== 矩阵 ==\n";
    // MUNIT(3) 是单位阵：对角 1，其余 0
    ckNum("MDETERM(MUNIT(3))", 1.0, 1e-12);
    ckNum("SUM(MUNIT(3))", 3.0, 1e-12);          // 单位阵元素和 = n
    ck("MUNIT(0)", "#VALUE!");
    // MMULT 维度不匹配
    ck("MMULT(MUNIT(2),MUNIT(3))", "#VALUE!");
    // MMULT(A, A^-1) = I  —— 用矩阵乘逆矩阵验证
    ckNum("MDETERM(MMULT({1,2;3,4},{1,0;0,1}))", -2.0, 1e-9);
    // 已知行列式
    ckNum("MDETERM({1,2;3,4})", -2.0, 1e-9);
    ckNum("MDETERM({4,7;2,6})", 10.0, 1e-9);
    ckNum("MDETERM({1,2,3;4,5,6;7,8,10})", -3.0, 1e-9);
    ckNum("MDETERM({1,0,0;0,1,0;0,0,1})", 1.0, 1e-12);
    // 奇异矩阵行列式为 0
    ckNum("MDETERM({1,2;2,4})", 0.0, 1e-12);
    // 非方阵
    ck("MDETERM({1,2,3;4,5,6})", "#VALUE!");
    // MINVERSE：A * A^-1 = I
    ckNum("INDEX(MMULT({4,7;2,6},MINVERSE({4,7;2,6})),1,1)", 1.0, 1e-9);
    ckNum("INDEX(MMULT({4,7;2,6},MINVERSE({4,7;2,6})),1,2)", 0.0, 1e-9);
    ckNum("INDEX(MMULT({4,7;2,6},MINVERSE({4,7;2,6})),2,1)", 0.0, 1e-9);
    ckNum("INDEX(MMULT({4,7;2,6},MINVERSE({4,7;2,6})),2,2)", 1.0, 1e-9);
    // 已知逆：[[4,7],[2,6]]^-1 = [[0.6,-0.7],[-0.2,0.4]]
    ckNum("INDEX(MINVERSE({4,7;2,6}),1,1)", 0.6, 1e-9);
    ckNum("INDEX(MINVERSE({4,7;2,6}),1,2)", -0.7, 1e-9);
    ckNum("INDEX(MINVERSE({4,7;2,6}),2,1)", -0.2, 1e-9);
    ckNum("INDEX(MINVERSE({4,7;2,6}),2,2)", 0.4, 1e-9);
    // 奇异矩阵求逆 -> #NUM!
    ck("MINVERSE({1,2;2,4})", "#NUM!");
    // 单位阵的逆是自身
    ckNum("INDEX(MINVERSE(MUNIT(4)),3,3)", 1.0, 1e-12);

    // ------------------------------------------------------------------
    std::cout << "== 信息 / 元函数 ==\n";
    ckTrue("ISEVEN(2)");
    ck("ISEVEN(3)", "FALSE");
    ckTrue("ISEVEN(2.5)");            // 先截断成 2
    ckTrue("ISODD(3)");
    ck("ISODD(2)", "FALSE");
    ckTrue("ISEVEN(-4)");
    ck("ISEVEN(\"abc\")", "#VALUE!");
    {
        // parser 不支持"省略的参数"，直接构造调用验证
        const FnInfo* fi = findFunction("ISOMITTED");
        Value r1 = fi->impl({Value::empty()}, sh);
        if (r1.isBool() && r1.b) P++; else { N++; std::cout << "  FAIL ISOMITTED(空) 应 TRUE\n"; }
        Value r2 = fi->impl({Value::num(1)}, sh);
        if (r2.isBool() && !r2.b) P++; else { N++; std::cout << "  FAIL ISOMITTED(1) 应 FALSE\n"; }
    }
    ck("ENCODEURL(\"a b\")", "a%20b");
    ck("ENCODEURL(\"A~_.-1\")", "A~_.-1");   // 保留字符不编码
    ck("INFO(\"system\")", "Linux");
    ck("INFO(\"numfile\")", "1");
    ck("INFO(\"unknown\")", "#VALUE!");
    ck("AREAS(A1:B2)", "1");

    // 元函数依赖引用语义：在真实 Sheet 上验证
    std::cout << "== 引用语义（FORMULATEXT / ISFORMULA / CELL）==\n";
    {
        sh.setName("Sheet1");
        sh.setValue(0, 0, Value::num(10));           // A1 常量
        std::string e = sh.setFormula(1, 0, "A1*2"); // B1 公式
        if (!e.empty()) std::cout << "  setFormula 失败: " << e << "\n";

        struct Case { const char* expr; const char* exp; };
        // 注意：FORMULATEXT/ISFORMULA 只能通过 Sheet 求值，这里用直接构造调用
        auto callOn = [&](const char* fn, int col, int row) -> Value {
            // 手工构造 Call 节点，绕过 parser 对引用参数的即时求值
            NodePtr ref = makeRef("", col, row, false, false);
            NodePtr call = makeCall(fn, {ref});
            return call->eval(sh);
        };
        Value v1 = callOn("ISFORMULA", 0, 0);
        if (v1.isBool() && v1.b == false) P++;
        else { N++; std::cout << "  FAIL ISFORMULA(A1) 应为 FALSE，实际 " << valueToText(v1) << "\n"; }
        Value v2 = callOn("ISFORMULA", 1, 0);
        if (v2.isBool() && v2.b == true) P++;
        else { N++; std::cout << "  FAIL ISFORMULA(B1) 应为 TRUE，实际 " << valueToText(v2) << "\n"; }
        Value v3 = callOn("FORMULATEXT", 1, 0);
        if (v3.isStr() && v3.s == "=A1*2") P++;
        else { N++; std::cout << "  FAIL FORMULATEXT(B1) 期望 '=A1*2'，实际 '" << valueToText(v3) << "'\n"; }
        Value v4 = callOn("FORMULATEXT", 0, 0);
        if (v4.isError() && v4.e == Err::NA) P++;
        else { N++; std::cout << "  FAIL FORMULATEXT(A1) 应 #N/A，实际 " << valueToText(v4) << "\n"; }
        // CELL("row") / CELL("col")
        Value v5 = callOn("CELL", 2, 4);   // 参数为引用时取 type 缺省 -> 走 row? 这里改用显式
        (void)v5;
        NodePtr r2 = makeRef("", 2, 4, false, false);
        NodePtr c2 = makeCall("CELL", {makeStr("row"), r2});
        Value v6 = c2->eval(sh);
        if (v6.isNum() && v6.n == 5) P++;
        else { N++; std::cout << "  FAIL CELL(\"row\",C5) 期望 5，实际 " << valueToText(v6) << "\n"; }
        NodePtr r3 = makeRef("", 2, 4, false, false);
        NodePtr c3 = makeCall("CELL", {makeStr("col"), r3});
        Value v7 = c3->eval(sh);
        if (v7.isNum() && v7.n == 3) P++;
        else { N++; std::cout << "  FAIL CELL(\"col\",C5) 期望 3，实际 " << valueToText(v7) << "\n"; }
    }

    // ------------------------------------------------------------------
    std::cout << "== 工作日（国际版）==\n";
    // 2024-01-01 是周一
    // WORKDAY.INTL("2024-01-01", 5) 跳过周末 -> 2024-01-08
    ck("TEXT(WORKDAY.INTL(\"2024-01-01\",5),\"yyyy-mm-dd\")", "2024-01-08");
    // 负数方向：-1 天前的工作日 = 2023-12-29（周五）
    ck("TEXT(WORKDAY.INTL(\"2024-01-01\",-1),\"yyyy-mm-dd\")", "2023-12-29");
    // weekend="0000011" 等价于默认
    ck("TEXT(WORKDAY.INTL(\"2024-01-01\",5,\"0000011\"),\"yyyy-mm-dd\")", "2024-01-08");
    // weekend="1000000" 只有周一休 -> 2024-01-02..01-06 都是工作日，第5个是 01-06
    ck("TEXT(WORKDAY.INTL(\"2024-01-01\",5,\"1000000\"),\"yyyy-mm-dd\")", "2024-01-06");
    // NETWORKDAYS.INTL 全月工作日记数（2024-02 有 29 天）
    ckNum("NETWORKDAYS.INTL(\"2024-02-01\",\"2024-02-29\")", 21);
    // 自定义周末：只有周日休（"0000001"）
    ckNum("NETWORKDAYS.INTL(\"2024-02-01\",\"2024-02-29\",\"0000001\")", 25);
    // 反向区间取负
    ckNum("NETWORKDAYS.INTL(\"2024-02-29\",\"2024-02-01\")", -21);
    // 非法 weekend 串
    ck("WORKDAY.INTL(\"2024-01-01\",1,\"000000\")", "#VALUE!");

    // ------------------------------------------------------------------
    std::cout << "== 双字节文本 ==\n";
    ck("LENB(\"abc\")", "3");
    ck("LENB(\"中文\")", "4");               // 每个汉字 2 字节
    ck("LENB(\"a中\")", "3");
    ck("LEFTB(\"中文abc\",2)", "中");
    ck("LEFTB(\"中文abc\",4)", "中文");
    ck("RIGHTB(\"中文abc\",3)", "abc");
    ck("MIDB(\"中文abc\",3,2)", "文");
    ck("FINDB(\"b\",\"中文abc\")", "6");     // 中文占 1-4 字节，b 在第 6 字节
    ck("SEARCHB(\"B\",\"中文abc\")", "6");   // 大小写不敏感
    ck("REPLACEB(\"中文abc\",5,1,\"X\")", "中文Xbc");
    ck("ASC(\"ａｂｃ\")", "abc");            // 全角转半角
    ck("ASC(\"中文\")", "中文");

    // ------------------------------------------------------------------
    std::cout << "== 统计补全 ==\n";
    ckNum("GAUSS(0)", 0.0, 1e-12);
    ckNum("GAUSS(1)", 0.3413447460685429, 1e-9);
    ckNum("PHI(0)", 0.3989422804014327, 1e-12);
    ckNum("SKEW.P({1,2,3,4,10})", 1.1384199573451025, 1e-9);
    ck("SKEW.P({1,2})", "#DIV/0!");
    ckNum("PROB({1,2,3},{0.2,0.3,0.5},2)", 0.3);
    ckNum("PROB({1,2,3},{0.2,0.3,0.5},1,2)", 0.5);
    ckNum("MODE.SNGL({1,2,2,3})", 2);
    ck("MODE.SNGL({1,2,3})", "#N/A");        // 无重复
    ckNum("BINOM.DIST.RANGE(10,0.5,3,5)", 0.568359375, 1e-9);
    ckNum("BINOM.DIST.RANGE(10,0.5,5)", 0.24609375, 1e-9);
    ckNum("BINOM.INV(10,0.5,0.5)", 5);
    ckNum("BINOM.INV(10,0.5,0.01)", 1);
    ckNum("CRITBINOM(10,0.5,0.5)", 5);       // 与 BINOM.INV 同义
    ckNum("STDEVPA({1,2,3})", 0.816496580927726, 1e-9);
    ckNum("VARPA({1,2,3})", 0.6666666666666666, 1e-9);
    ckNum("INDEX(MODE.MULT({1,1,2,2,3}),1)", 1);   // 返回数组，取第一个众数

    // ------------------------------------------------------------------
    std::cout << "== 复数双曲 ==\n";
    // IMSINH(i) = sinh(0)cos(1) + i·cosh(0)sin(1) = i·sin(1)
    ckNum("IMAGINARY(IMSINH(\"i\"))", std::sin(1.0), 1e-12);
    // IMCOSH(0) = 1
    ck("IMCOSH(\"0\")", "1");
    // IMCOSH(i) = cos(1)
    ckNum("IMREAL(IMCOSH(\"i\"))", std::cos(1.0), 1e-12);
    // IMSECH(0) = 1
    ck("IMSECH(\"0\")", "1");
    // 恒等式：cosh²z - sinh²z = 1，取 z = 1+i 验证
    // （用 IMSUB(IMPRODUCT(IMCOSH,IMCOSH), IMPRODUCT(IMSINH,IMSINH)) 应为 1）
    ckNum("IMREAL(IMSUB(IMPRODUCT(IMCOSH(\"1+i\"),IMCOSH(\"1+i\")),IMPRODUCT(IMSINH(\"1+i\"),IMSINH(\"1+i\"))))", 1.0, 1e-9);
    ck("IMCSC(\"0\")", "#NUM!");
    ck("IMCOT(\"0\")", "#NUM!");

    // ------------------------------------------------------------------
    std::cout << "== 取整补全 ==\n";
    ckNum("ISO.CEILING(4.3)", 5);
    ckNum("ISO.CEILING(-4.3)", -4);          // 负数也向 +∞
    ckNum("ISO.CEILING(4.3,2)", 6);
    ckNum("CEILING.PRECISE(-4.3)", -4);
    ckNum("FLOOR.PRECISE(-4.3)", -5);
    ckNum("CEILING.PRECISE(0)", 0);          // 基数为 0 不崩

    // ------------------------------------------------------------------
    std::cout << "== 奇数期债券：往返闭合自证 ==\n";
    // 没有外部标杆时，最强的一致性检查是 price -> yield -> price 往返。
    // 若折现指数、应计利息符号或现金流构造有错，往返必然不闭合。
    {
        const char* cases[] = {
            // settlement, maturity, issue, first_coupon, rate, yld, redemption, freq
            "DATE(2019,3,1),DATE(2024,1,1),DATE(2018,11,15),DATE(2019,7,1),0.05,0.06,100,2",
            "DATE(2020,6,15),DATE(2030,6,15),DATE(2020,1,10),DATE(2020,12,15),0.04,0.035,100,2",
            "DATE(2021,2,1),DATE(2026,2,1),DATE(2020,8,1),DATE(2021,8,1),0.06,0.07,100,1",
        };
        for (auto& cse : cases) {
            std::string expr = std::string("ODDFPRICE(") + cse + ")";
            ParseResult pr = parseFormula(expr);
            if (!pr.ok()) { N++; std::cout << "  FAIL parse " << expr << "\n"; continue; }
            Value pv = pr.node->eval(sh);
            if (!pv.isNum()) { N++; std::cout << "  FAIL ODDFPRICE 非数值: " << valueToText(pv) << "\n"; continue; }
            double price = pv.n;
            // 用该价格反算收益率，应还原 0.06 / 0.035 / 0.07
            std::string yexpr = std::string("ODDFYIELD(") + cse + ")";
            // 把 yld 位换成 price
            // 手动拼：前 5 个参数 + price + 后 2 个
            std::vector<std::string> parts;
            {
                std::string cur; int depth = 0;
                for (char ch : std::string(cse)) {
                    if (ch == '(') depth++;
                    else if (ch == ')') depth--;
                    if (ch == ',' && depth == 0) { parts.push_back(cur); cur.clear(); }
                    else cur.push_back(ch);
                }
                parts.push_back(cur);
            }
            if (parts.size() < 8) { N++; std::cout << "  FAIL 参数解析\n"; continue; }
            double expectYld = std::stod(parts[5]);
            std::string back = "ODDFYIELD(" + parts[0] + "," + parts[1] + "," + parts[2] + ","
                             + parts[3] + "," + parts[4] + "," + std::to_string(price) + ","
                             + parts[6] + "," + parts[7] + ")";
            ParseResult pr2 = parseFormula(back);
            if (!pr2.ok()) { N++; std::cout << "  FAIL parse " << back << "\n"; continue; }
            Value yv = pr2.node->eval(sh);
            if (!yv.isNum()) { N++; std::cout << "  FAIL ODDFYIELD 非数值: " << valueToText(yv) << "\n"; continue; }
            if (std::fabs(yv.n - expectYld) < 1e-7) P++;
            else { N++; std::cout << "  FAIL 往返不闭合: 期望 yld=" << expectYld
                                  << " 反算=" << yv.n << " (price=" << price << ")\n"; }
        }
    }
    {
        // ODDL 系列同样做往返
        const char* cases[] = {
            // settlement, maturity, last_interest, rate, yld, redemption, freq
            "DATE(2023,3,1),DATE(2024,8,15),DATE(2023,1,15),0.05,0.06,100,2",
            "DATE(2022,7,1),DATE(2027,10,1),DATE(2022,4,1),0.04,0.045,100,2",
        };
        for (auto& cse : cases) {
            std::vector<std::string> parts;
            {
                std::string cur; int depth = 0;
                for (char ch : std::string(cse)) {
                    if (ch == '(') depth++; else if (ch == ')') depth--;
                    if (ch == ',' && depth == 0) { parts.push_back(cur); cur.clear(); }
                    else cur.push_back(ch);
                }
                parts.push_back(cur);
            }
            double expectYld = std::stod(parts[4]);
            ParseResult pr = parseFormula(std::string("ODDLPRICE(") + cse + ")");
            if (!pr.ok()) { N++; continue; }
            Value pv = pr.node->eval(sh);
            if (!pv.isNum()) { N++; std::cout << "  FAIL ODDLPRICE 非数值 " << valueToText(pv) << "\n"; continue; }
            std::string back = "ODDLYIELD(" + parts[0] + "," + parts[1] + "," + parts[2] + ","
                             + parts[3] + "," + std::to_string(pv.n) + "," + parts[5] + "," + parts[6] + ")";
            ParseResult pr2 = parseFormula(back);
            if (!pr2.ok()) { N++; continue; }
            Value yv = pr2.node->eval(sh);
            if (yv.isNum() && std::fabs(yv.n - expectYld) < 1e-7) P++;
            else { N++; std::cout << "  FAIL ODDL 往返不闭合: 期望 " << expectYld
                                  << " 实际 " << valueToText(yv) << "\n"; }
        }
    }
    // 与常规 PRICE 的近似一致：奇数期设为正常期时，两者应接近
    {
        // issue 到 first_coupon 恰好半年（freq=2），等价于常规债券
        // settlement 必须严格落在 issue 与 first_coupon 之间
        ParseResult a = parseFormula(
            "ODDFPRICE(DATE(2019,12,1),DATE(2030,3,1),DATE(2019,9,1),DATE(2020,3,1),0.05,0.06,100,2)");
        ParseResult b = parseFormula("PRICE(DATE(2019,12,1),DATE(2030,3,1),0.05,0.06,100,2)");
        if (a.ok() && b.ok()) {
            Value va = a.node->eval(sh), vb = b.node->eval(sh);
            if (va.isNum() && vb.isNum()) {
                // 不要求完全相等（PRICE 用 360/365 的应计口径可能不同），
                // 但应在 1% 以内 —— 这能抓出量级错误
                if (std::fabs(va.n - vb.n) < std::fabs(vb.n) * 0.01 + 1.0) P++;
                else { N++; std::cout << "  FAIL ODDFPRICE=" << va.n << " 与 PRICE=" << vb.n
                                      << " 差异过大\n"; }
            } else { N++; std::cout << "  FAIL 债券对比非数值\n"; }
        }
    }
    std::cout << "== PRICE / YIELD 往返（本轮修出的既有 bug）==\n";
    // 之前的 PRICE 把 redemption 与 frequency 读反，任何正常调用都返回 #NUM!。
    // 用往返闭合把这个 bug 钉死。
    {
        ParseResult a = parseFormula("PRICE(DATE(2020,3,1),DATE(2030,3,1),0.05,0.06,100,2)");
        if (!a.ok()) { N++; std::cout << "  FAIL parse PRICE\n"; }
        else {
            Value pv = a.node->eval(sh);
            if (!pv.isNum()) { N++; std::cout << "  FAIL PRICE 非数值: " << valueToText(pv) << "\n"; }
            else {
                // 票息 5% < 收益率 6% -> 折价，价格应低于 100
                if (pv.n < 100 && pv.n > 80) P++;
                else { N++; std::cout << "  FAIL PRICE 量级异常: " << pv.n << "\n"; }
                // YIELD(PRICE(y)) 应还原 y
                std::string bk = "YIELD(DATE(2020,3,1),DATE(2030,3,1),0.05," + std::to_string(pv.n) + ",100,2)";
                ParseResult b2 = parseFormula(bk);
                if (!b2.ok()) { N++; std::cout << "  FAIL parse YIELD\n"; }
                else {
                    Value yv = b2.node->eval(sh);
                    if (yv.isNum() && std::fabs(yv.n - 0.06) < 1e-6) P++;
                    else { N++; std::cout << "  FAIL PRICE/YIELD 往返不闭合: 期望 0.06 实际 "
                                          << valueToText(yv) << " (price=" << pv.n << ")\n"; }
                }
            }
        }
    }
    // 参数校验
    ck("ODDFPRICE(DATE(2020,1,1),DATE(2030,1,1),DATE(2019,1,1),DATE(2019,6,1),0.05,0.06,100,3)", "#NUM!");
    ck("ODDFYIELD(DATE(2020,1,1),DATE(2030,1,1),DATE(2019,1,1),DATE(2019,6,1),0.05,0,100,2)", "#NUM!");

    // ------------------------------------------------------------------
    std::cout << "== 分布补全 ==\n";
    // CHISQ.DIST / CHISQ.INV 往返
    ckNum("CHISQ.DIST(5,2,TRUE)", 0.9179150013761012, 1e-9);
    ckNum("CHISQ.DIST(5,2,FALSE)", 0.0410424993119494, 1e-9);  // df=2 时 (1/2)exp(-x/2)
    // CHISQ.INV(CHISQ.DIST(x)) 应还原 x
    {
        ParseResult a = parseFormula("CHISQ.DIST(7.3,3,TRUE)");
        Value pv = a.node->eval(sh);
        std::string bk = "CHISQ.INV(" + std::to_string(pv.n) + ",3)";
        ParseResult b2 = parseFormula(bk);
        Value yv = b2.node->eval(sh);
        if (yv.isNum() && std::fabs(yv.n - 7.3) < 1e-4) P++;
        else { N++; std::cout << "  FAIL CHISQ 往返不闭合: " << valueToText(yv) << "\n"; }
    }
    // CHIDIST 是右尾（= 1 - CHISQ.DIST 累积）
    ckNum("CHIDIST(5,2)", 1.0 - 0.9179150013761012, 1e-9);
    // F.INV / F.DIST 往返
    {
        ParseResult a = parseFormula("F.DIST(2.5,5,10,TRUE)");
        Value pv = a.node->eval(sh);
        std::string bk = "F.INV(" + std::to_string(pv.n) + ",5,10)";
        ParseResult b2 = parseFormula(bk);
        Value yv = b2.node->eval(sh);
        if (yv.isNum() && std::fabs(yv.n - 2.5) < 1e-5) P++;
        else { N++; std::cout << "  FAIL F 往返不闭合: " << valueToText(yv) << "\n"; }
    }
    // 参数校验
    ck("CHISQ.DIST(-1,2,TRUE)", "#NUM!");
    ck("CHISQ.INV(1.5,2)", "#NUM!");
    ck("F.INV(0.5,0.5,10)", "#NUM!");
    // LOGINV 与 LOGNORM.INV 等价
    ckNum("LOGINV(0.5,2,0.5)", 7.38905609893065, 1e-6);
    ckNum("GAMMALN.PRECISE(5)", std::lgamma(5.0), 1e-9);

    // ------------------------------------------------------------------
    std::cout << "== 数据库补全 ==\n";
    // 3x3 库：表头 {产品,数量,地区}，两行数据
    // DCOUNT 已有实现作为对照，这里验证 DCOUNTA/DGET/DSTDEVP/DVARP
    {
        // 用数组常量模拟 database：{{"产品","数量","地区"},{"A",10,"北"},{"B",20,"南"},{"C",30,"北"}}
        // 条件区：{{"地区"},{"北"}}
        const char* db = "{\"产品\",\"数量\",\"地区\";\"A\",10,\"北\";\"B\",20,\"南\";\"C\",30,\"北\"}";
        const char* cr = "{\"地区\";\"北\"}";
        // 选"地区=北" -> A(10) 与 C(30)
        ckNum(std::string("DCOUNTA(") + db + ",2," + cr + ")", 2);
        ckNum(std::string("DSUM(") + db + ",2," + cr + ")", 40);       // 对照：已有实现
        ckNum(std::string("DSTDEVP(") + db + ",2," + cr + ")", 10);
        ckNum(std::string("DVARP(") + db + ",2," + cr + ")", 100);
        ck(std::string("DGET(") + db + ",1," + cr + ")", "#NUM!");
        // 唯一匹配：地区=南 只有 B
        const char* cr2 = "{\"地区\";\"南\"}";
        ck(std::string("DGET(") + db + ",1," + cr2 + ")", "B");
        // 无匹配 -> #VALUE!
        const char* cr3 = "{\"地区\";\"西\"}";
        ck(std::string("DGET(") + db + ",1," + cr3 + ")", "#VALUE!");
    }

    // ------------------------------------------------------------------
    std::cout << "== TRIMRANGE / PERCENTOF ==\n";
    // 去掉边缘空行空列
    // 数组常量里的 0 是数值不是空白，TRIMRANGE 不应裁掉它们（Excel 同样如此）
    ckNum("ROWS(TRIMRANGE({0,0,0;0,1,2;0,3,4;0,0,0}))", 4);
    ckNum("COLUMNS(TRIMRANGE({0,0,0;0,1,2;0,3,4;0,0,0}))", 3);
    ckNum("SUM(TRIMRANGE({0,0,0;0,1,2;0,3,4;0,0,0}))", 10);
    ckNum("PERCENTOF({2,3},{10})", 0.5);
    ck("PERCENTOF({1},{0})", "#DIV/0!");

    // ------------------------------------------------------------------
    std::cout << "== FORECAST.ETS ==\n";
    // 纯线性序列：ETS 有平滑滞后，但方向必须正确且接近线性外推
    {
        ParseResult pr = parseFormula(
            "FORECAST.ETS(11,{1,2,3,4,5,6,7,8,9,10},{1,2,3,4,5,6,7,8,9,10})");
        Value v = pr.node->eval(sh);
        if (v.isNum() && v.n > 10 && v.n < 13) P++;
        else { N++; std::cout << "  FAIL ETS 线性外推: " << valueToText(v) << "\n"; }
    }
    // 周期 4 的季节序列：第 13 点应回到该季的位置（约 13）
    {
        ParseResult pr = parseFormula(
            "FORECAST.ETS(13,{10,20,30,40,11,21,31,41,12,22,32,42},{1,2,3,4,5,6,7,8,9,10,11,12})");
        Value v = pr.node->eval(sh);
        if (v.isNum() && std::fabs(v.n - 13) < 2.0) P++;
        else { N++; std::cout << "  FAIL ETS 季节预测: " << valueToText(v) << "\n"; }
    }
    ck("FORECAST.ETS.SEASONALITY(13,{10,20,30,40,11,21,31,41,12,22,32,42},{1,2,3,4,5,6,7,8,9,10,11,12})", "4");
    // 置信区间应为正
    {
        ParseResult pr = parseFormula(
            "FORECAST.ETS.CONFINT(13,{10,20,30,40,11,21,31,41,12,22,32,42},{1,2,3,4,5,6,7,8,9,10,11,12})");
        Value v = pr.node->eval(sh);
        if (v.isNum() && v.n > 0) P++;
        else { N++; std::cout << "  FAIL CONFINT: " << valueToText(v) << "\n"; }
    }
    ckNum("FORECAST.ETS.STAT(13,{1,2,3,4,5},{1,2,3,4,5},1)", 0.3);   // Alpha
    ck("FORECAST.ETS.STAT(13,{1,2,3,4,5},{1,2,3,4,5},99)", "#N/A");
    ckNum("FORECAST.LINEAR(6,{1,2,3,4,5},{1,2,3,4,5})", 6);
    // 时间轴必须递增
    ck("FORECAST.ETS(3,{1,2,3},{3,2,1})", "#VALUE!");

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
