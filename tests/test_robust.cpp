// 鲁棒性测试（畸形输入不得导致崩溃）
//
// 与功能测试的区别：功能测试喂正确输入、验证结果；这一组专门喂**错误输入**，
// 只验证一件事 —— 程序能否安全失败，而不是崩溃。
//
// 这一组里每一个用例都对应一个真实修掉的崩溃：
//   - 深层括号嵌套        -> 解析器爆栈（段错误）
//   - "1E400"             -> stod 抛 out_of_range，未捕获导致 terminate
//   - "=A1:XFD1048576"    -> 区域展开 170 亿个元素，OOM 被杀
//   - "%%$"               -> 词法器位置停滞，死循环产出空 token，OOM 被杀
//   - 变异过的 xlsx        -> XML 深层嵌套，parseNode 爆栈（段错误）
//   - 压缩包炸弹           -> 解压无上限，几百 KB 吃满内存
//
// 断言方式是"能走到最后一行"：如果任何一处崩溃，进程根本到不了汇总输出。
#include "sheet.hpp"
#include "value.hpp"
#include "numfmt.hpp"
#include "xlsx.hpp"
#include "xml.hpp"
#include <iostream>
#include <fstream>
#include <random>
#include <vector>
#include <string>

using namespace xl;

static int P = 0, N = 0;
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}

int main() {
    // ------------------------------------------------------------------
    std::cout << "== 数值极值（不得崩溃、不得漏出 NaN/inf） ==\n";
    {
        const char* exprs[] = {
            "1E400", "-1E400", "1E-320", "1E-400", "1E308*10", "-1E308*10",
            "1/0", "0/0", "SQRT(-1)", "LOG(0)", "LN(-1)", "999^999",
            "FACT(171)", "FACT(172)", "(1E308+1E308)-1E308", "1E400+1",
            "ABS(1E400)", "SUM(1E400,1)", "-1E-320", "0*1E400"
        };
        for (const char* e : exprs) {
            Sheet sh;
            std::string err = sh.setFormula(0, 0, e);
            if (!err.empty()) continue;
            sh.recalc();
            std::string t = sh.display(0, 0);
            // 安全 = 错误值，或可解析为有限数
            bool isErr = t.find('#') != std::string::npos;
            bool isNum = !t.empty() && t.find_first_not_of("-0123456789.eE+") == std::string::npos;
            OK(isErr || isNum, std::string("极值安全: ") + e + " -> " + t);
        }
    }

    // ------------------------------------------------------------------
    std::cout << "== 深层嵌套（必须限制或安全拒绝，不得爆栈） ==\n";
    {
        // 括号
        for (int d : {100, 400, 1000, 5000, 50000}) {
            std::string e(d, '(');
            e += "1";
            e += std::string(d, ')');
            Sheet sh;
            std::string err = sh.setFormula(0, 0, e);
            if (!err.empty()) continue;                 // 安全拒绝，OK
            sh.recalc();
            sh.display(0, 0);
        }
        OK(true, "括号嵌套至 50,000 层未崩溃");

        // XML
        for (int d : {100, 500, 10000, 200000}) {
            std::string x;
            for (int i = 0; i < d; i++) x += "<a>";
            x += "t";
            for (int i = 0; i < d; i++) x += "</a>";
            std::string err;
            XmlNode n = xmlParse(x, err);
            (void)n;
        }
        OK(true, "XML 嵌套至 200,000 层未崩溃");

        // 深层嵌套的函数调用
        for (int d : {50, 500, 5000}) {
            std::string e;
            for (int i = 0; i < d; i++) e += "IFERROR(";
            e += "1";
            for (int i = 0; i < d; i++) e += ",0)";
            Sheet sh;
            std::string err = sh.setFormula(0, 0, e);
            if (!err.empty()) continue;
            sh.recalc();
            sh.display(0, 0);
        }
        OK(true, "函数嵌套至 5,000 层未崩溃");
    }

    // ------------------------------------------------------------------
    std::cout << "== 超大区域（不得 OOM） ==\n";
    {
        const char* big[] = {
            "A1:XFD1048576", "A1:XFD1048576+0", "SUM(A1:XFD1048576)",
            "A:A", "1:1", "SUM(A:A)", "ROWS(A:A)", "A1:B2",
            "A1:XFD1048576 A1:B2"
        };
        for (const char* e : big) {
            Sheet sh;
            sh.setValue(0, 0, Value::num(1));
            std::string err = sh.setFormula(0, 0, e);
            if (!err.empty()) continue;
            sh.recalc();
            sh.display(0, 0);
        }
        OK(true, "超大区域引用未导致 OOM");
    }

    // ------------------------------------------------------------------
    std::cout << "== 词法器死循环（位置必须前进） ==\n";
    {
        // 曾经触发死循环的输入：'$' 与 '\\' 能进入标识符分支却不消耗字符
        const char* tricky[] = {
            "%%$", "$", "\\", "$$", "\\\\", "$A$", "A$", "$1", "%%",
            "@@", "$$$", "\\$", "'", "!'", "A!", "!A", "._", "__"
        };
        for (const char* e : tricky) {
            Sheet sh;
            std::string err = sh.setFormula(0, 0, e);
            if (!err.empty()) continue;
            sh.recalc();
            sh.display(0, 0);
        }
        OK(true, "特殊字符组合未导致死循环");
    }

    // ------------------------------------------------------------------
    std::cout << "== 随机公式模糊测试（100,000 条） ==\n";
    {
        std::mt19937 rng(20260927);
        const char* atoms[] = {
            "(", ")", "+", "-", "*", "/", "^", "&", "=", ",", ";", " ", ":",
            "A1", "B2:B9", "'Sheet 1'!A1", "SUM", "IF", "VLOOKUP", "\"txt", "\"",
            "#REF!", "#DIV/0!", "@", "{1,2}", "}", "{", "TRUE", "FALSE", "1E999",
            "0x", "..", "%%", "$A$1", "[1]", "\t", "\n", "IFERROR", "LAMBDA",
            "LET", "x", "_", "1.2.3", "--", "++", "((", "))", "\\", "$", "!",
            "'", "A1:XFD1048576", "A:A", "1:1", "#NULL!", "中文", "0.5", "1e-400"
        };
        const int na = (int)(sizeof(atoms) / sizeof(atoms[0]));
        long accepted = 0, rejected = 0;
        for (int iter = 0; iter < 100000; iter++) {
            std::string e;
            int len = 1 + rng() % 14;
            for (int i = 0; i < len; i++) e += atoms[rng() % na];
            Sheet sh;
            sh.setValue(0, 0, Value::num(1));
            std::string err = sh.setFormula(0, 0, e);
            if (!err.empty()) { rejected++; continue; }
            sh.recalc();
            sh.display(0, 0);
            accepted++;
        }
        OK(true, "随机公式未导致崩溃");
        std::cout << "     接受 " << accepted << " 条，拒绝 " << rejected << " 条\n";
    }

    // ------------------------------------------------------------------
    std::cout << "== 随机格式码（20,000 个） ==\n";
    {
        std::mt19937 rng(4242);
        const char* fa[] = {"0","#","?",".",",","%","E","+","-","[","]","\"",
                            "\\","*","_",";","@","yyyy","mm","dd","h","s",
                            "Red","DBNum"};
        const int nf = (int)(sizeof(fa) / sizeof(fa[0]));
        for (int k = 0; k < 20000; k++) {
            std::string f;
            int len = rng() % 12;
            for (int i = 0; i < len; i++) f += fa[rng() % nf];
            NumFmt n;
            std::string err;
            if (n.parse(f, err)) {
                n.format(1234.5); n.format(-1);
                n.format(0);      n.formatText("x");
            }
        }
        OK(true, "随机格式码未导致崩溃");
    }

    // ------------------------------------------------------------------
    std::cout << "== 损坏的 xlsx（截断 + 字节变异） ==\n";
    {
        // 样本不存在时自己造一个，否则全新克隆的仓库里这一组会静默跳过，
        // 看起来"通过"其实什么都没测 —— 这类假通过比失败更危险。
        std::vector<std::string> owned;
        {
            std::ifstream probe("out/示例.xlsx", std::ios::binary);
            if (!probe) {
                Workbook w; Sheet& s0 = w.sheet(0);
                s0.setName("样本");
                for (int r = 0; r < 200; r++)
                    for (int c = 0; c < 10; c++)
                        s0.setValue(c, r, Value::num(r * 10 + c));
                s0.setFormula(9, 0, "SUM(A1:J1)");
                s0.recalc();
                std::string e;
                w.save("/tmp/xl_robust_seed.xlsx", e);
                owned.push_back("/tmp/xl_robust_seed.xlsx");
            }
        }
        std::vector<std::string> list = {"out/示例.xlsx", "out/带图表.xlsx",
                                         "out/样式示例.xlsx", "out/数字格式.xlsx",
                                         "out/多表.xlsx"};
        for (const std::string& f : owned) list.push_back(f);

        int tested = 0;
        std::mt19937 rng(777);
        for (const std::string& fs : list) {
            const char* fp = fs.c_str();
            std::ifstream in(fp, std::ios::binary);
            if (!in) continue;
            std::vector<char> raw((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
            // 截断
            for (double frac : {0.05, 0.2, 0.5, 0.8, 0.95}) {
                size_t cut = (size_t)(raw.size() * frac);
                {
                    std::ofstream o("/tmp/xl_trunc.bin", std::ios::binary);
                    o.write(raw.data(), (std::streamsize)cut);
                }
                Workbook wb; std::string err;
                wb.load("/tmp/xl_trunc.bin", err);
                tested++;
            }
            // 变异
            for (int k = 0; k < 60; k++) {
                std::vector<char> m = raw;
                int nflip = 1 + (int)(rng() % 32);
                for (int j = 0; j < nflip; j++)
                    m[rng() % m.size()] = (char)(rng() % 256);
                {
                    std::ofstream o("/tmp/xl_mut.bin", std::ios::binary);
                    o.write(m.data(), (std::streamsize)m.size());
                }
                Workbook wb; std::string err;
                wb.load("/tmp/xl_mut.bin", err);
                tested++;
            }
        }
        OK(tested > 0, "损坏文件加载未导致崩溃（且确实执行了 " + std::to_string(tested) + " 次）");
        std::cout << "     共 " << tested << " 次损坏文件加载\n";
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
