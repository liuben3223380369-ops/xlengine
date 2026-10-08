// 引用重写与填充测试
//
// 这里的坑都不是"算错"，而是"悄悄指向了别的格子"：
//   - 字符串字面量里的 "A1" 不是引用，平移了就毁了公式
//   - $ 只锁被标记的那一边
//   - 越界必须变成 #REF!，不能夹取到边界（夹取会静默指向错误单元格）
//   - 区域两端各自平移
//   - 填充时若边读边写，会读到自己刚写入的结果
#include "refshift.hpp"
#include "sheet.hpp"
#include <iostream>

using namespace xl;

static int P = 0, N = 0;
static void EQS(const std::string& g, const std::string& e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望='" << e << "' 实际='" << g << "'\n"; }
}
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}
static void EQI(int g, int e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望=" << e << " 实际=" << g << "\n"; }
}

// 平移后取文本（断言不越界、不失败）
static std::string sh(const std::string& f, int dc, int dr) {
    ShiftResult r = shiftFormula(f, dc, dr);
    if (!r.ok) return "<ERR:" + r.error + ">";
    return r.text;
}

int main() {
    // ------------------------------------------------------------------
    std::cout << "== 基本平移 ==\n";
    {
        EQS(sh("A1", 1, 0), "B1", "右移一列");
        EQS(sh("A1", 0, 1), "A2", "下移一行");
        EQS(sh("A1", 2, 3), "C4", "同时平移");
        EQS(sh("A1", 0, 0), "A1", "零偏移不变");
        EQS(sh("B2", -1, -1), "A1", "负偏移");
        EQS(sh("Z10", 1, 0), "AA10", "跨 Z->AA 进位");
    }

    // ------------------------------------------------------------------
    std::cout << "== $ 绝对引用 ==\n";
    {
        EQS(sh("$A$1", 5, 5), "$A$1", "全绝对不动");
        EQS(sh("$A1", 1, 1), "$A2", "列绝对：只动行");
        EQS(sh("A$1", 1, 1), "B$1", "行绝对：只动列");
        EQS(sh("$A1", 9, 0), "$A1", "列绝对时列偏移无效");
        EQS(sh("A$1", 0, 9), "A$1", "行绝对时行偏移无效");
        // 区域里两端各自独立
        EQS(sh("$A$1:B2", 1, 1), "$A$1:C3", "混合区域");
        EQS(sh("A1:$B$2", 1, 1), "B2:$B$2", "区域右端绝对");
    }

    // ------------------------------------------------------------------
    std::cout << "== 区域与函数 ==\n";
    {
        EQS(sh("SUM(A1:B2)", 0, 1), "SUM(A2:B3)", "区域两端都平移");
        EQS(sh("SUM(A1:B2)", 1, 0), "SUM(B1:C2)", "区域右移");
        EQS(sh("SUM(A1,A2,A3)", 0, 1), "SUM(A2,A3,A4)", "多个参数");
        EQS(sh("VLOOKUP($A1,B:C,2,0)", 1, 1), "VLOOKUP($A2,C:D,2,0)", "嵌套与整列引用");
        EQS(sh("SUM(A1:B2)*2+MAX(C3:D4)", 1, 2), "SUM(B3:C4)*2+MAX(D5:E6)", "复合表达式");
        // 整行整列
        EQS(sh("SUM(1:1)", 0, 1), "SUM(2:2)", "整行引用");
    }

    // ------------------------------------------------------------------
    std::cout << "== 不该被平移的东西 ==\n";
    {
        // 字符串字面量里的 A1 不是引用
        EQS(sh("\"A1\"", 1, 1), "\"A1\"", "字符串里的 A1 不动");
        EQS(sh("IF(A1>0,\"B2\",\"C3\")", 1, 1), "IF(B2>0,\"B2\",\"C3\")", "字符串与引用混合");
        EQS(sh("CONCAT(\"A\", \"1\")", 1, 1), "CONCAT(\"A\", \"1\")", "纯字符串");
        // 数字、布尔
        EQS(sh("1+2", 1, 1), "1+2", "常量表达式");
        EQS(sh("TRUE", 1, 1), "TRUE", "布尔字面量");
        // 函数名不能被当成引用
        EQS(sh("LOG10(5)", 1, 1), "LOG10(5)", "函数名不动");
        EQS(sh("DAYS360(A1,B1)", 0, 1), "DAYS360(A2,B2)", "DAYS360 是函数名但参数要平移");
        // 错误字面量
        EQS(sh("NA()", 1, 1), "NA()", "无参函数");
    }

    // ------------------------------------------------------------------
    std::cout << "== 跨表引用 ==\n";
    {
        EQS(sh("Sheet1!A1", 1, 0), "Sheet1!B1", "带表名平移");
        EQS(sh("Sheet1!$A$1", 1, 1), "Sheet1!$A$1", "跨表绝对引用");
        EQS(sh("Sheet1!A1:B2", 0, 1), "Sheet1!A2:B3", "跨表区域");
        EQS(sh("SUM(Sheet1!A1,Sheet2!B2)", 1, 1), "SUM(Sheet1!B2,Sheet2!C3)", "多个跨表引用");
        // 带空格的表名必须保留引号
        EQS(sh("'My Sheet'!A1", 1, 0), "'My Sheet'!B1", "带空格表名保留引号");
        // 中文表名裸写（Excel 允许，且我们的词法器支持）
        {
            ShiftResult r = shiftFormula("数据!A1", 1, 0);
            OK(r.ok, "中文表名可解析");
            EQS(r.text, "数据!B1", "中文表名平移");
        }
        // 表名里含引号要转义
        EQS(sh("'It''s'!A1", 1, 0), "'It''s'!B1", "表名内引号转义");
    }

    // ------------------------------------------------------------------
    std::cout << "== 越界 ==\n";
    {
        // 上/左越界 -> #REF!，不能夹取到 A1（否则会静默指向错误单元格）
        ShiftResult r1 = shiftFormula("A1", -1, 0);
        EQS(r1.text, "#REF!", "左越界变 #REF!");
        EQI(r1.refErrors, 1, "记录 1 处越界");
        ShiftResult r2 = shiftFormula("A1", 0, -1);
        EQS(r2.text, "#REF!", "上越界变 #REF!");
        // 部分越界：只坏掉的那一个
        EQS(sh("SUM(A1,B5)", 0, -3), "SUM(#REF!,B2)", "部分越界只影响其中之一");
        // 自定义边界
        ShiftResult r3 = shiftFormula("A1", 5, 0, 2, 10);
        EQS(r3.text, "#REF!", "超出自定义列上限");
        EQS(shiftFormula("A1", 2, 0, 2, 10).text, "C1", "在自定义边界内正常");
        // 越界不影响整体 ok（这是数据问题，不是语法问题）
        OK(r1.ok, "越界时 ok 仍为 true");
    }

    // ------------------------------------------------------------------
    std::cout << "== 语法错误 ==\n";
    {
        ShiftResult r = shiftFormula("SUM(", 1, 1);
        // '(' 后缺内容：词法层面未必报错，这里只要求不崩且原样或报错二者之一
        OK(!r.text.empty(), "畸形公式不崩溃");
        OK(hasRelativeRef("A1"), "hasRelativeRef 认出相对引用");
        OK(!hasRelativeRef("$A$1"), "全绝对不算相对引用");
        OK(hasRelativeRef("$A1"), "半绝对仍算相对引用");
        OK(!hasRelativeRef("SUM(1,2)"), "纯常量没有引用");
        OK(!hasRelativeRef("\"A1\""), "字符串里的 A1 不算引用");
    }

    // ------------------------------------------------------------------
    std::cout << "== 向下填充 ==\n";
    {
        Sheet s;
        s.setFormula(0, 0, "B1*2");        // A1 = B1*2
        s.setValue(1, 0, Value::num(10));  // B1 = 10
        s.setValue(1, 1, Value::num(20));  // B2 = 20
        s.setValue(1, 2, Value::num(30));  // B3 = 30
        s.recalc();
        EQS(s.display(0, 0), "20", "A1 = B1*2 = 20");

        // 选 A1:A3，用首行向下填充
        FillResult out;
        fillRect(s, 0, 0, 0, 2, false, out);
        EQI(out.written, 2, "写了 2 格");
        EQI(out.refErrors, 0, "无越界");
        OK(out.error.empty(), "无错误: " + out.error);
        EQS(s.display(0, 1), "40", "A2 = B2*2 = 40（引用已下移）");
        EQS(s.display(0, 2), "60", "A3 = B3*2 = 60");

        // 公式文本确实被重写了，不是复制
        std::string f1, f2;
        s.cellFormula(0, 1, f1);
        s.cellFormula(0, 2, f2);
        EQS(f1, "B2*2", "A2 的公式是 B2*2");
        EQS(f2, "B3*2", "A3 的公式是 B3*2");
    }

    // ------------------------------------------------------------------
    std::cout << "== 向右填充 ==\n";
    {
        Sheet s;
        s.setFormula(0, 0, "A2*3");        // A1 = A2*3
        s.setValue(0, 1, Value::num(5));   // A2 = 5
        s.setValue(1, 1, Value::num(6));   // B2 = 6
        s.setValue(2, 1, Value::num(7));   // C2 = 7
        s.recalc();

        FillResult out;
        fillRect(s, 0, 0, 2, 0, true, out);
        EQI(out.written, 2, "写了 2 格");
        EQS(s.display(1, 0), "18", "B1 = B2*3 = 18");
        EQS(s.display(2, 0), "21", "C1 = C2*3 = 21");
    }

    // ------------------------------------------------------------------
    std::cout << "== 填充的边界情形 ==\n";
    {
        // 源是常量：填充只是复制，不涉及引用
        Sheet s;
        s.setValue(0, 0, Value::num(7));
        FillResult out;
        fillRect(s, 0, 0, 0, 3, false, out);
        EQS(s.display(0, 1), "7", "常量填充复制到 A2");
        EQS(s.display(0, 3), "7", "常量填充复制到 A4");

        // 绝对引用在填充时保持不动 —— 这是 $ 存在的意义
        Sheet s2;
        s2.setValue(5, 0, Value::num(100));      // F1 = 100
        s2.setFormula(0, 0, "$F$1*2");           // A1 = $F$1*2
        s2.setFormula(0, 1, "$F$1*3");           // 预置 A2
        s2.recalc();
        EQS(s2.display(0, 0), "200", "A1 = $F$1*2");
        FillResult o2;
        fillRect(s2, 0, 0, 0, 1, false, o2);
        std::string f;
        s2.cellFormula(0, 1, f);
        EQS(f, "$F$1*2", "填充后绝对引用仍是 $F$1");
        EQS(s2.display(0, 1), "200", "A2 也是 200");

        // 半绝对：列锁行移
        Sheet s3;
        s3.setValue(0, 0, Value::num(1));
        s3.setFormula(1, 0, "$A1+1");
        s3.recalc();
        FillResult o3;
        fillRect(s3, 1, 0, 1, 2, false, o3);
        std::string g1, g2;
        s3.cellFormula(1, 1, g1);
        s3.cellFormula(1, 2, g2);
        EQS(g1, "$A2+1", "列绝对行相对 -> $A2");
        EQS(g2, "$A3+1", "再下一行 -> $A3");

        // 填充到越界位置会产生 #REF!
        Sheet s4;
        s4.setFormula(0, 1, "A1");      // A2 = A1
        s4.recalc();
        FillResult o4;
        // 源在 A2(r=1)，目标从 A1(r=0) 起：dr = -1 -> A2 上方越界
        o4 = fillRange(s4, FillRequest{0, 1, 0, 1, 0, 0, 1, 1});
        EQI(o4.refErrors, 1, "越界产生 1 处 #REF!");
        std::string bad;
        s4.cellFormula(0, 0, bad);
        EQS(bad, "#REF!", "越界格子的公式是 #REF!");
        EQS(s4.display(0, 0), "#REF!", "求值也是 #REF!");
    }

    // ------------------------------------------------------------------
    std::cout << "== fillRange 平铺 ==\n";
    {
        // 源 1x2，目标 2x4：按取模平铺
        Sheet s;
        s.setFormula(0, 0, "A10+1");
        s.setFormula(1, 0, "A10+2");
        s.setValue(0, 9, Value::num(50));   // A10 = 50
        s.recalc();
        EQS(s.display(0, 0), "51", "A1 = 51");
        EQS(s.display(1, 0), "52", "B1 = 52");

        FillResult out = fillRange(s, FillRequest{0, 0, 1, 0, 0, 2, 2, 4});
        OK(out.error.empty(), "无错误: " + out.error);
        EQI(out.written, 8, "写 2x4 = 8 格");
        // 第 3 行（r=2）对应源 r=0，dr=2
        std::string f;
        s.cellFormula(0, 2, f);
        EQS(f, "A12+1", "平铺后第 3 行引用 A12");
        s.setValue(0, 11, Value::num(5));   // A12 = 5
        s.recalc();
        EQS(s.display(0, 2), "6", "A3 = A12+1 = 6");
    }


    // ------------------------------------------------------------------
    std::cout << "== 整列 / 整行引用求值 ==\n";
    {
        Sheet s;
        s.setValue(1, 0, Value::num(5));    // B1
        s.setValue(1, 1, Value::num(6));    // B2
        s.setValue(1, 5, Value::num(9));    // B6（中间空着）
        s.setValue(2, 0, Value::num(7));    // C1
        s.recalc();

        std::string err;
        err = s.setFormula(0, 0, "SUM(B:B)");
        OK(err.empty(), "SUM(B:B) 可解析: " + err);
        s.recalc();
        EQS(s.display(0, 0), "20", "SUM(B:B) = 5+6+9 = 20");

        err = s.setFormula(0, 1, "COUNTA(B:B)");
        OK(err.empty(), "COUNTA(B:B) 可解析");
        s.recalc();
        EQS(s.display(0, 1), "3", "COUNTA(B:B) = 3");

        err = s.setFormula(0, 2, "COUNT(B:B)");
        s.recalc();
        EQS(s.display(0, 2), "3", "COUNT(B:B) = 3");

        // 整列区域 B:C
        err = s.setFormula(0, 3, "SUM(B:C)");
        OK(err.empty(), "SUM(B:C) 可解析: " + err);
        s.recalc();
        EQS(s.display(0, 3), "27", "SUM(B:C) = 20+7 = 27");

        // 整行。公式放 F6 —— 放进第 1 行就是自引用，引擎会正确报 #CALC!，
        // 但这里要测的是求值，不是循环检测。
        err = s.setFormula(5, 5, "SUM(1:1)");
        OK(err.empty(), "SUM(1:1) 可解析: " + err);
        s.recalc();
        // 第 1 行：A1=20, B1=5, C1=7，D1..F1 空 -> 32
        EQS(s.display(5, 5), "32", "SUM(1:1) = A1+B1+C1 = 32");

        // 反过来验证循环检测是真在工作：把整行公式放进它自己所属的那一行
        Sheet circ;
        circ.setValue(1, 0, Value::num(4));
        std::string e4 = circ.setFormula(0, 0, "SUM(1:1)");
        OK(e4.empty(), "自引用整行公式可解析");
        circ.recalc();
        EQS(circ.display(0, 0), "#CALC!", "整行自引用报 #CALC!");

        // 空白表上的整列引用不能崩、也不能返回空数组。
        // 公式放 B1：放 A1 的话它就落在 A:A 里，成了自引用。
        Sheet empty;
        std::string e2 = empty.setFormula(1, 0, "COUNTA(A:A)");
        OK(e2.empty(), "空表 COUNTA(A:A) 可解析");
        empty.recalc();
        EQS(empty.display(1, 0), "0", "空表 COUNTA(A:A) = 0");

        // 非法形式不应被误判成整行引用
        Sheet s3;
        std::string e3 = s3.setFormula(0, 0, "SUM(1.5:2)");
        OK(!e3.empty(), "小数行号被拒绝: " + e3);
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
