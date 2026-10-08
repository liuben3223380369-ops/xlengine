// 全函数冒烟测试
//
// 为什么要单独有这一组：
// 项目声称 495 个函数，但精确语义测试只覆盖了其中一部分（约六成）。
// 未被覆盖的那些函数里，曾经藏着真 bug —— VDB 就是这么发现的：
// 它声明最少 4 参、实现却访问第 5 个，只传 4 参时越界读直接段错误，
// 而这个函数此前没有任何测试，所以一直没暴露。
//
// 这一组不验证"算得对不对"（那是各组语义测试的事），只验证
// "注册了就真的能用"：能被解析、求值不崩溃、不返回未实现类错误。
// 它的价值是把"函数表里有名字"和"实际可调用"这两件事钉在一起。
//
// 另外这里还断言"不存在重复注册"，以及两起历史事故的数值回归 ——
// 见文件末尾的说明。
//
// 参数用多组典型值逐个试，命中任意一组就算通过 —— 不同函数签名差异很大，
// 用单一参数集覆盖不了。
#include "sheet.hpp"
#include "functions.hpp"
#include "value.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

using namespace xl;

static int P = 0, N = 0;
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}

// 从 xl --funcs 的输出读函数表。这样新增函数会自动进入冒烟范围，
// 不需要手工维护一份名单（名单会漏，而且漏了不会有任何提示）。
static bool readFuncList(const std::string& path, std::vector<std::string>& out,
                         std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "打不开函数表: " + path; return false; }
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) out.push_back(line);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "用法: " << argv[0] << " <函数表文件>\n";
        return 2;
    }
    std::vector<std::string> names, errs;
    std::string e;
    if (!readFuncList(argv[1], names, e)) { std::cout << "  FAIL " << e << "\n"; return 1; }

    // 覆盖常见签名：数字、文本、区域、数组、布尔、多参
    const char* argSets[] = {
        "", "1", "1,2", "1,2,3", "1,2,3,4", "1,2,3,4,5",
        "\"a\"", "\"a\",1", "\"a\",\"b\"",
        "A1", "A1:A3", "A1:B2",
        "{1,2,3}", "{1,2},2",
        "2,3", "0.5", "10,20,30"
    };
    const int nSets = (int)(sizeof(argSets) / sizeof(argSets[0]));

    for (const std::string& fn : names) {
        bool anyOk = false;
        for (int i = 0; i < nSets && !anyOk; i++) {
            Sheet sh;
            sh.setValue(0, 0, Value::num(10));
            sh.setValue(1, 0, Value::num(20));
            sh.setValue(2, 0, Value::num(30));
            sh.setValue(0, 1, Value::num(40));
            sh.setValue(1, 1, Value::num(50));
            sh.setValue(2, 1, Value::num(60));
            std::string pe = sh.setFormula(8, 8, fn + "(" + argSets[i] + ")");
            if (!pe.empty()) continue;                 // 元数/类型不符，换下一组
            sh.recalc();
            std::string t = sh.display(8, 8);
            if (t == "#NAME?") continue;               // 等于没注册
            if (t.find("未实现") != std::string::npos) continue;
            anyOk = true;
        }
        OK(anyOk, "函数无法调用: " + fn);
    }

    // ------------------------------------------------------------------
    // 重复注册必须为 0。
    //
    // 这是审计时发现的真实事故留下来的防线：同一个函数名在两个 .cpp 里
    // 各注册一次时，谁生效取决于 C++ 未规定的静态初始化顺序。曾因此出现
    // PERMUTATIONA 恒返回 0、F.DIST(...,FALSE) 恒返回 0 —— 两者都能通过
    // 冒烟测试（不崩溃、不报未实现），只有语义断言能抓到。
    // ------------------------------------------------------------------
    {
        const std::vector<std::string>& dup = duplicateRegistrations();
        if (!dup.empty()) {
            std::string joined;
            for (const auto& d : dup) joined += (joined.empty() ? "" : ", ") + d;
            std::cout << "     重复注册 " << dup.size() << " 个: " << joined << "\n";
        }
        OK(dup.empty(), "不存在重复注册的函数（否则生效者取决于未规定的初始化顺序）");
    }

    // ------------------------------------------------------------------
    // 两起事故的回归断言：只验证"能调用"抓不到它们，必须比对数值
    // ------------------------------------------------------------------
    {
        // PERMUTATIONA(n,k) = n^k。曾因残桩恒返回 0
        Sheet sh; sh.setName("R");
        sh.setFormula(0, 0, "PERMUTATIONA(3,2)"); sh.recalc();
        OK(sh.valueAt(0,0).isNum() && std::fabs(sh.valueAt(0,0).n - 9.0) < 1e-9,
           "PERMUTATIONA(3,2)=9（回归：曾恒返回 0）");
        sh.setFormula(0, 0, "PERMUTATIONA(2,10)"); sh.recalc();
        OK(sh.valueAt(0,0).isNum() && std::fabs(sh.valueAt(0,0).n - 1024.0) < 1e-9,
           "PERMUTATIONA(2,10)=1024");
    }
    {
        // F.DIST(x,df1,df2,cumulative)：FALSE 应返回概率密度，不是 0
        Sheet sh; sh.setName("R2");
        sh.setFormula(0, 0, "F.DIST(2,5,5,FALSE)"); sh.recalc();
        Value v = sh.valueAt(0,0);
        OK(v.isNum() && v.n > 0.01, "F.DIST(2,5,5,FALSE) 返回概率密度（回归：曾返回 0）");
        sh.setFormula(0, 0, "F.DIST(2,5,5,TRUE)"); sh.recalc();
        Value c = sh.valueAt(0,0);
        OK(c.isNum() && c.n > 0.5 && c.n < 1.0, "F.DIST(2,5,5,TRUE) 返回累积概率 0..1");
    }

    // 输出格式与其它测试组保持一致（以"通过 N 项"开头），
    // 否则 Makefile 里的汇总 grep 会漏掉这一组
    std::cout << "通过 " << P << " 项，失败 " << N << " 项"
              << "（全函数冒烟，共 " << names.size() << " 个）\n";
    return N ? 1 : 0;
}
