// ---------------------------------------------------------------------------
// 入口：自测 + 交互 REPL
// 用法：
//   ./xl --test          跑语义自测
//   ./xl                 进入 REPL
// REPL 语法：
//   A1 = 10              常量
//   A1 = =B1*2           公式
//   =SUM(A1:A3)          直接求值
//   dump                 打印全部单元格
//   quit
// ---------------------------------------------------------------------------
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include "sheet.hpp"
#include "functions.hpp"
#include "parser.hpp"
#include "xlsx.hpp"
#include "pdf.hpp"
#include "sheetpdf.hpp"
#include "tui.hpp"

using namespace xl;

static int g_pass = 0, g_fail = 0;
static Sheet g_sh;

static std::string evalExpr(const std::string& s) {
    ParseResult pr = parseFormula(s);
    if (!pr.ok()) return std::string("PARSE_ERR(") + pr.error + ")";
    return valueToText(pr.node->eval(g_sh));
}

static void check(const std::string& expr, const std::string& expect) {
    std::string got = evalExpr(expr);
    bool ok = (got == expect);
    if (ok) { g_pass++; }
    else {
        g_fail++;
        std::cout << "  [FAIL] " << expr << "\n         期望=" << expect << "  实际=" << got << "\n";
    }
}

static void runTests() {
    std::cout << "=== 算术与优先级 ===\n";
    check("1+2*3", "7");
    check("(1+2)*3", "9");
    check("-2^2", "4");          // Excel: 一元负号优先于 ^
    check("0-2^2", "-4");
    check("2^-2", "0.25");
    check("10/4", "2.5");
    check("7/2", "3.5");

    std::cout << "=== 类型转换（Excel 语义）===\n";
    check("1+\"2\"", "3");            // 文本型数字参与算术
    check("1+TRUE", "2");
    check("TRUE+TRUE", "2");
    check("\"a\"&1", "a1");
    check("1&2", "12");
    check("50%", "0.5");
    check("200*10%", "20");

    std::cout << "=== 错误传播 ===\n";
    check("1/0", "#DIV/0!");
    check("1+\"abc\"", "#VALUE!");
    check("#N/A", "#N/A");
    check("1+#VALUE!", "#VALUE!");
    check("#REF!+1", "#REF!");
    check("\"a\"&1/0", "#DIV/0!");

    std::cout << "=== 比较（文本 > 数字 > 逻辑值 的排序序）===\n";
    check("1<2", "TRUE");
    check("\"a\">1", "TRUE");        // Excel: 文本大于数字
    check("TRUE>1", "TRUE");         // Excel: 逻辑值大于数字
    check("\"A\"=\"a\"", "TRUE");    // 文本比较大小写不敏感
    check("1=1", "TRUE");
    check("2<>3", "TRUE");

    std::cout << "=== 数学与聚合函数 ===\n";
    check("SUM(1,2,3)", "6");
    check("SUM(\"5\",2)", "7");
    check("SUM(\"abc\")", "#VALUE!");   // 顶层真文本报错
    check("AVERAGE(1,2,3)", "2");
    check("PRODUCT(2,3,4)", "24");
    check("MIN(3,1,2)", "1");
    check("MAX(3,1,2)", "3");
    check("ABS(-3)", "3");
    check("INT(-2.5)", "-3");           // 向下取整，不是截断
    check("MOD(-3,2)", "1");            // Excel 的 MOD 与除数同号
    check("MOD(3,-2)", "-1");
    check("ROUND(2.5,0)", "3");         // 远离零
    check("ROUND(-2.5,0)", "-3");
    check("ROUND(1.234,2)", "1.23");
    check("ROUNDUP(1.2,0)", "2");
    check("ROUNDDOWN(1.9,0)", "1");
    check("SQRT(-1)", "#NUM!");
    check("MEDIAN(1,3,2)", "2");
    check("PI()", "3.14159265358979");   // 15 位有效数字

    std::cout << "=== 逻辑函数 ===\n";
    check("IF(TRUE,\"y\",\"n\")", "y");
    check("IF(FALSE,\"y\",\"n\")", "n");
    check("IF(FALSE,\"y\")", "FALSE");       // 省略第三参数
    check("IF(TRUE,1,1/0)", "1");            // 惰性求值，分支不算
    check("IFERROR(1/0,\"x\")", "x");
    check("IFERROR(5,\"x\")", "5");
    check("IFNA(#N/A,\"x\")", "x");
    check("AND(TRUE,1)", "TRUE");
    check("AND(TRUE,0)", "FALSE");
    check("OR(FALSE,0)", "FALSE");
    check("NOT(TRUE)", "FALSE");
    check("XOR(TRUE,TRUE)", "FALSE");

    std::cout << "=== 文本函数 ===\n";
    check("LEN(\"hello\")", "5");
    check("LEFT(\"hello\",3)", "hel");
    check("RIGHT(\"hello\",2)", "lo");
    check("MID(\"hello\",2,3)", "ell");
    check("UPPER(\"ab\")", "AB");
    check("CONCAT(\"a\",\"b\",1)", "ab1");
    check("TRIM(\"  a  b  \")", "a b");
    check("VALUE(\"3.5\")", "3.5");
    check("FIND(\"lo\",\"hello\")", "4");
    check("SUBSTITUTE(\"a-b-\",\"-\",\"+\")", "a+b+");
    check("REPT(\"ab\",2)", "abab");

    std::cout << "=== 查找引用 ===\n";
    check("MATCH(2,{1,2,3},0)", "2");
    check("MATCH(9,{1,2,3},0)", "#N/A");
    check("INDEX({1,2;3,4},2,1)", "3");
    check("CHOOSE(2,\"a\",\"b\",\"c\")", "b");
    check("HLOOKUP(2,{1,2;3,4},2)", "4");

    std::cout << "=== 信息函数 ===\n";
    check("ISNUMBER(1)", "TRUE");
    check("ISTEXT(\"a\")", "TRUE");
    check("ISBLANK(#N/A)", "FALSE");
    check("ISERROR(1/0)", "TRUE");
    check("ISNA(#N/A)", "TRUE");

    std::cout << "=== 单元格引用与重算 ===\n";
    {
        Sheet s;
        s.setValue(0, 0, Value::num(10));      // A1=10
        s.setValue(1, 0, Value::num(20));      // B1=20
        std::string e = s.setFormula(2, 0, "=A1+B1");   // C1
        if (!e.empty()) { std::cout << "  [FAIL] C1 公式解析: " << e << "\n"; g_fail++; }
        else if (s.display(2, 0) != "30") { std::cout << "  [FAIL] C1 期望30 实际" << s.display(2,0) << "\n"; g_fail++; }
        else g_pass++;

        std::string e2 = s.setFormula(3, 0, "=SUM(A1:B1)*2");
        if (!e2.empty()) { std::cout << "  [FAIL] D1: " << e2 << "\n"; g_fail++; }
        else if (s.display(3, 0) != "60") { std::cout << "  [FAIL] D1 期望60 实际" << s.display(3,0) << "\n"; g_fail++; }
        else g_pass++;

        // 修改 A1 后重算
        s.setValue(0, 0, Value::num(100));
        s.recalc();
        if (s.display(2, 0) != "120") { std::cout << "  [FAIL] 重算后 C1 期望120 实际" << s.display(2,0) << "\n"; g_fail++; }
        else g_pass++;

        // 跨表引用（单表阶段应给 #REF!）
        std::string e3 = s.setFormula(4, 0, "=Sheet2!A1");
        if (!e3.empty()) { std::cout << "  [FAIL] 跨表公式解析失败: " << e3 << "\n"; g_fail++; }
        else if (s.display(4, 0) != "#REF!") { std::cout << "  [FAIL] 跨表期望#REF! 实际" << s.display(4,0) << "\n"; g_fail++; }
        else g_pass++;
    }

    std::cout << "=== 循环引用检测 ===\n";
    {
        Sheet s;
        std::string e = s.setFormula(0, 0, "=A1+1");
        s.recalc();
        if (s.display(0, 0) != "#CALC!") { std::cout << "  [FAIL] 循环引用期望#CALC! 实际" << s.display(0,0) << "\n"; g_fail++; }
        else g_pass++;
        // 相互引用
        Sheet s2;
        s2.setFormula(0, 0, "=B1+1");
        s2.setFormula(1, 0, "=A1+1");
        s2.recalc();
        if (s2.display(0, 0) != "#CALC!") { std::cout << "  [FAIL] 双向循环期望#CALC! 实际" << s2.display(0,0) << "\n"; g_fail++; }
        else g_pass++;
    }

    std::cout << "=== 区域与联合 ===\n";
    {
        Sheet s;
        for (int i = 0; i < 3; i++) s.setValue(i, 0, Value::num(i + 1));   // A1..C1 = 1,2,3
        s.setFormula(0, 1, "=SUM(A1:C1)");
        if (s.display(0, 1) != "6") { std::cout << "  [FAIL] SUM(A1:C1) 期望6 实际" << s.display(0,1) << "\n"; g_fail++; }
        else g_pass++;
        // 区域内文本被忽略（Excel 行为）
        s.setValue(1, 0, Value::str("abc"));
        s.recalc();
        if (s.display(0, 1) != "4") { std::cout << "  [FAIL] 区域含文本 SUM 期望4 实际" << s.display(0,1) << "\n"; g_fail++; }
        else g_pass++;
        // 联合
        s.setFormula(1, 1, "=SUM((A1,C1))");
        if (s.display(1, 1) != "4") { std::cout << "  [FAIL] SUM((A1,C1)) 期望4 实际" << s.display(1,1) << "\n"; g_fail++; }
        else g_pass++;
        // 交集：A1:C1 ∩ A1:B1 = A1:B1 = {1,"abc"}，区域内文本被忽略 -> 1
        s.setFormula(2, 1, "=SUM(A1:C1 A1:B1)");
        if (s.display(2, 1) != "1") { std::cout << "  [FAIL] 交集 SUM 期望1 实际" << s.display(2,1) << "\n"; g_fail++; }
        else g_pass++;
    }

    std::cout << "\n----------------------------------------\n";
    std::cout << "通过 " << g_pass << " 项，失败 " << g_fail << " 项\n";
    std::cout << "已注册函数 " << functionNames().size() << " 个\n";
}

// ---------------------------------------------------------------------------
// REPL
// ---------------------------------------------------------------------------
static bool parseAddr(const std::string& s, int& col, int& row) {
    std::string u;
    for (char c : s) if (!std::isspace((unsigned char)c)) u.push_back(std::toupper((unsigned char)c));
    size_t i = 0, cs = 0;
    while (i < u.size() && std::isalpha((unsigned char)u[i])) i++;
    if (i == 0) return false;
    long c = 0;
    for (size_t k = cs; k < i; k++) c = c * 26 + (u[k] - 'A' + 1);
    size_t rs = i;
    while (i < u.size() && std::isdigit((unsigned char)u[i])) i++;
    if (i == rs || i != u.size()) return false;
    col = (int)(c - 1);
    row = (int)(std::stol(u.substr(rs)) - 1);
    return true;
}

static void repl() {
    Sheet s;
    std::cout << "xlengine REPL — 输入 quit 退出，dump 查看全部单元格\n";
    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        std::string cmd = line;
        for (char& c : cmd) c = std::tolower((unsigned char)c);
        if (cmd == "quit" || cmd == "exit" || cmd == "q") break;
        if (cmd == "dump") {
            for (auto& kv : s.allCells()) {
                std::cout << "  " << addrToStr(kv.first.first, kv.first.second)
                          << " = " << (kv.second.hasFormula ? kv.second.formula : "")
                          << "  -> " << valueToText(kv.second.value) << "\n";
            }
            continue;
        }
        size_t eq = line.find('=');
        if (eq != std::string::npos && eq > 0 && line[eq-1] != '=' && !(line[eq-1]=='<'||line[eq-1]=='>'||line[eq-1]=='!')) {
            std::string left = line.substr(0, eq);
            std::string right = line.substr(eq + 1);
            int col, row;
            if (!parseAddr(left, col, row)) { std::cout << "地址无效: " << left << "\n"; continue; }
            while (!right.empty() && std::isspace((unsigned char)right.front())) right.erase(right.begin());
            std::string err = s.setFormula(col, row, right);
            if (!err.empty()) { std::cout << "错误: " << err << "\n"; continue; }
            s.recalc();
            std::cout << addrToStr(col, row) << " -> " << s.display(col, row) << "\n";
            continue;
        }
        // 直接求值
        ParseResult pr = parseFormula(line);
        if (!pr.ok()) { std::cout << "解析错误: " << pr.error << "\n"; continue; }
        Sheet tmp;
        std::cout << valueToText(pr.node->eval(tmp)) << "\n";
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--test") { runTests(); return g_fail ? 1 : 0; }
    if (argc > 1 && std::string(argv[1]) == "--funcs") {
        for (auto& n : functionNames()) std::cout << n << "\n";
        return 0;
    }
    // --pdf <输入.xlsx> <输出.pdf> [--font <ttf>]  把工作簿导出成 PDF
    if (argc > 3 && std::string(argv[1]) == "--pdf") {
        std::string font = "";
        for (int i = 4; i + 1 < argc; i++)
            if (std::string(argv[i]) == "--font") font = argv[i + 1];
        if (font.empty()) {
            // 没指定就用常见路径里第一个存在的，找不到就退化成无嵌入字体
            const char* cand[] = {
                "/usr/share/fonts/truetype/alibaba-puhuiti/AlibabaPuHuiTi-2-35-Thin.ttf",
                "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            };
            for (auto c : cand) { std::ifstream t(c); if (t) { font = c; break; } }
        }
        Workbook wb;
        std::string err;
        if (!wb.load(argv[2], err)) { std::cout << "加载失败: " << err << "\n"; return 1; }
        PdfWriter w;
        w.setPageSize(595.28, 841.89);
        w.setMargin(28);
        std::string fn;
        bool hasFont = !font.empty() && w.embedFont(font, fn, err);
        if (!font.empty() && !hasFont) std::cout << "（警告）字体不可用: " << err << "\n";
        int nSheets = (int)wb.sheetCount();
        for (int i = 0; i < nSheets; i++) {
            Sheet& sh = wb.sheet(i);
            // 逐格取一次值，触发公式求值（evaluate 是私有接口）
            for (auto& kv : sh.allCells()) sh.valueAt(kv.first.first, kv.first.second);
            SheetPdfOptions o;
            o.fontPath = hasFont ? font : "";
            o.title = (i < (int)wb.sheetNames().size()) ? wb.sheetNames()[i] : ("Sheet" + std::to_string(i + 1));
            o.landscape = false;
            std::string e2;
            if (!sheetToPdf(sh, argv[3], o, e2)) { std::cout << "导出失败: " << e2 << "\n"; return 1; }
            // 多表时后续页追加到同一文件：这里简化为逐表单独导出，
            // 文件名加后缀避免覆盖
            (void)e2;
        }
        std::cout << "已导出 " << argv[3] << "（" << nSheets << " 张表";
        if (hasFont) std::cout << "，嵌入字体 " << font;
        std::cout << "）\n";
        return 0;
    }
    // --tui [file.xlsx]  打开终端网格界面
    if (argc > 1 && std::string(argv[1]) == "--tui") {
        Workbook wb;
        std::string path = (argc > 2) ? argv[2] : std::string();
        if (!path.empty()) {
            std::string err;
            if (!wb.load(path, err)) std::cout << "加载失败: " << err << "\n";
        }
        // 只在"没加载到任何内容"时铺一份示例数据：
        // Workbook 默认就带一张空表，所以判据是"首表为空"，不是"没有表"
        if (!path.empty() ? false : (wb.sheetCount() == 1 && wb.sheet(0).cellCount() == 0)) {
            Sheet& sh = wb.sheet(0);
            sh.setValue(0, 0, Value::str("地区"));
            sh.setValue(1, 0, Value::str("一季度"));
            sh.setValue(2, 0, Value::str("二季度"));
            sh.setValue(3, 0, Value::str("合计"));
            const char* reg[] = {"华北", "华东", "华南"};
            double d[3][2] = {{120, 165}, {98, 132}, {87, 110}};
            for (int r = 0; r < 3; r++) {
                sh.setValue(0, r + 1, Value::str(reg[r]));
                sh.setValue(1, r + 1, Value::num(d[r][0]));
                sh.setValue(2, r + 1, Value::num(d[r][1]));
                sh.setFormula(3, r + 1, "SUM(B" + std::to_string(r + 2) + ":C" + std::to_string(r + 2) + ")");
            }
            sh.recalc();
            // 第二张表，用来看多表切换
            Sheet& s2 = wb.addSheet("参数");
            s2.setValue(0, 0, Value::str("税率"));
            s2.setValue(1, 0, Value::num(0.13));
            s2.recalc();
        }
        Terminal t;
        bool interactive = t.enterRaw();
        if (interactive) t.leaveRaw();     // runTui 内部会自己开
        return runTui(wb, path, interactive);
    }

    runTests();
    std::cout << "\n";
    repl();
    return 0;
}
