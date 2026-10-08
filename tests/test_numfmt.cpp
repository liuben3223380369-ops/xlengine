// 数字格式（number format）测试
//
// 这块的价值全在"用户看到的那一串字符对不对"，所以测试直接断言格式化结果。
// 最容易错的三处：
//   - m 在 h 之后是"分钟"，否则是"月"（Excel 的经典歧义）
//   - 末尾逗号是"除以 1000"，不是千分位
//   - 分号切段时，引号内和方括号内的分号不是分隔符
#include "numfmt.hpp"
#include "date.hpp"
#include <iostream>

using namespace xl;

static int P = 0, N = 0;
static void EQS(const std::string& g, const std::string& e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望='" << e << "' 实际='" << g << "'\n"; }
}
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}

// 用格式码格式化一个数字
static std::string f(const std::string& code, double v, bool& ok) {
    NumFmt nf; std::string err;
    ok = nf.parse(code, err);
    if (!ok) return "<ERR:" + err + ">";
    return nf.format(v);
}
static std::string f(const std::string& code, double v) { bool ok; return f(code, v, ok); }

int main() {
    // ------------------------------------------------------------------
    std::cout << "== 基础数字 ==\n";
    {
        EQS(f("0", 42), "42", "0 -> 42");
        EQS(f("0", 0), "0", "0 -> 0");
        EQS(f("0", -7), "-7", "0 负数");
        EQS(f("0.00", 3.14159), "3.14", "0.00 四舍五入");
        EQS(f("0.00", 3), "3.00", "0.00 补零");
        EQS(f("0.000", 1.5), "1.500", "0.000");
        EQS(f("#", 42), "42", "# -> 42");
        EQS(f("#.##", 3.14159), "3.14", "#.## 不补零");
        EQS(f("#.##", 3.1), "3.1", "#.## 尾部不补零");
        EQS(f("#.##", 3), "3", "#.## 整数不带小数点");
        // 模板位数不够时数字不能被截断
        EQS(f("0", 1234), "1234", "模板 0 显示 1234 不截断");
        EQS(f("0.0", 1.25), "1.3", "小数位不足时四舍五入");
        EQS(f("0.0", 1.251), "1.3", "1.251 -> 1.3");
    }

    // ------------------------------------------------------------------
    std::cout << "== 千分位与缩放 ==\n";
    {
        EQS(f("#,##0", 1234567), "1,234,567", "#,##0 千分位");
        EQS(f("#,##0", 123), "123", "#,##0 三位不分组");
        EQS(f("#,##0", 1234), "1,234", "#,##0 四位分组");
        EQS(f("#,##0.00", 1234.567), "1,234.57", "#,##0.00");
        // 末尾逗号 = 除以 1000
        EQS(f("#,##0,", 1234567), "1,235", "#,##0, 除以 1000");
        EQS(f("#,##0,,", 123456789), "123", "#,##0,, 除以 1e6");
        // 千分位与缩放逗号并存：#,##0, 的第一个逗号是千分位标记位置
        EQS(f("0", 1000), "1000", "0 无千分位");
    }

    // ------------------------------------------------------------------
    std::cout << "== 百分比与科学计数 ==\n";
    {
        EQS(f("0%", 0.125), "13%", "0% 四舍五入");
        EQS(f("0.0%", 0.125), "12.5%", "0.0%");
        EQS(f("0.00%", 0.1234), "12.34%", "0.00%");
        EQS(f("0%", 1), "100%", "0% 整数 1");
        EQS(f("0.00E+00", 1234.5), "1.23E+03", "科学计数");
        EQS(f("0.00E+00", 0.0001234), "1.23E-04", "科学计数负指数");
        // 尾数模板整数位超过 1 位时（##0.0E+0），Excel 会调整指数让尾数多占几位
        // （常见说法是 12.3E+3），但这一点我无法在本环境内用权威实现验证，
        // 所以这里只断言结构性质，不断言具体指数。
        {
            std::string r = f("##0.0E+0", 12345);
            OK(r.find('E') != std::string::npos, "##0.0E+0 含 E: " + r);
            // E 前后分别是尾数和指数，且量级自洽：尾数 * 10^指数 ≈ 12345
            size_t e = r.find('E');
            double mant = std::stod(r.substr(0, e));
            int exp = std::stoi(r.substr(e + 1));
            double back = mant * std::pow(10.0, exp);
            OK(std::fabs(back - 12345) < 12345 * 0.05, "还原后量级正确: " + r);
        }
    }

    // ------------------------------------------------------------------
    std::cout << "== 多段：正 / 负 / 零 ==\n";
    {
        EQS(f("0.00;-0.00;0", 5.5), "5.50", "第 1 段用于正数");
        EQS(f("0.00;-0.00;0", -5.5), "-5.50", "第 2 段用于负数");
        EQS(f("0.00;-0.00;0", 0), "0", "第 3 段用于零");
        // 括号表示负数
        EQS(f("#,##0;(#,##0)", -1234), "(1,234)", "括号负数");
        EQS(f("#,##0;(#,##0)", 1234), "1,234", "括号格式的正数");
        // 只有一段时它也用于负数
        EQS(f("0.00", -1.5), "-1.50", "单段覆盖负数");
        // 条件段
        EQS(f("[>100]\"大\";[>10]\"中\";\"小\"", 200), "大", "条件段 1");
        EQS(f("[>100]\"大\";[>10]\"中\";\"小\"", 50), "中", "条件段 2");
        EQS(f("[>100]\"大\";[>10]\"中\";\"小\"", 5), "小", "条件段 3");
        // 条件段优先于正负号选段
        EQS(f("[Red][<0]-0;0", -5), "-5", "带颜色的负数段");
        EQS(f("[Red][<0]-0;0", 5), "5", "带颜色的正数段");
    }

    // ------------------------------------------------------------------
    std::cout << "== 字面量与转义 ==\n";
    {
        EQS(f("0\" 元\"", 42), "42 元", "引号字面量");
        EQS(f("\"合计:\"0", 42), "合计:42", "前缀字面量");
        EQS(f("0\\%", 42), "42%", "转义百分号（不乘 100）");
        EQS(f("￥#,##0.00", 1234.5), "￥1,234.50", "货币符号");
        EQS(f("$#,##0", 1234), "$1,234", "美元符号");
        // 分号在引号内不是分隔符 —— 少了这条判断会被劈成两段
        EQS(f("0\"a;b\"", 5), "5a;b", "引号内的分号不分段");
        bool ok = false;
        std::string r = f("[Red]0;-0", -5, ok);
        OK(ok, "方括号内的分号不分段");
        EQS(r, "-5", "方括号段用于负数");
    }

    // ------------------------------------------------------------------
    std::cout << "== 日期 ==\n";
    {
        // 2026-09-27 的序列号
        double d2026 = ymdToSerial(2026, 9, 27);
        EQS(f("yyyy-mm-dd", d2026), "2026-09-27", "yyyy-mm-dd");
        EQS(f("yyyy/mm/dd", d2026), "2026/09/27", "yyyy/mm/dd");
        EQS(f("yy-m-d", d2026), "26-9-27", "yy-m-d 不补零");
        EQS(f("mm/dd/yyyy", d2026), "09/27/2026", "mm/dd/yyyy");
        EQS(f("d-mmm-yy", d2026), "27-Sep-26", "d-mmm-yy 月份缩写");
        EQS(f("mmmm d, yyyy", d2026), "September 27, 2026", "月份全名");
        EQS(f("ddd", d2026), "Sun", "ddd 星期缩写");
        EQS(f("dddd", d2026), "Sunday", "dddd 星期全名");
        EQS(f("yyyy年m月d日", d2026), "2026年9月27日", "中文日期");

        // 1900 假闰日：序列号 60 是 Excel 里不存在的 1900-02-29
        EQS(f("yyyy-mm-dd", 60), "1900-02-29", "假闰日 60");
        EQS(f("yyyy-mm-dd", 61), "1900-03-01", "61 是 3 月 1 日");
        EQS(f("yyyy-mm-dd", 1), "1900-01-01", "序列号 1");
    }

    // ------------------------------------------------------------------
    std::cout << "== 时间 ==\n";
    {
        // 0.5 = 12:00:00
        EQS(f("h:mm", 0.5), "12:00", "h:mm 正午");
        EQS(f("hh:mm:ss", 0.5), "12:00:00", "hh:mm:ss");
        EQS(f("h:mm AM/PM", 0.5), "12:00 PM", "AM/PM 正午");
        EQS(f("h:mm AM/PM", 0.25), "6:00 AM", "AM/PM 上午");
        EQS(f("h:mm am/pm", 0.75), "6:00 pm", "小写 am/pm");

        // m 的歧义：h 之后是分钟，否则是月
        EQS(f("h:mm", 0.5), "12:00", "h 后的 m 是分钟");
        EQS(f("mm:ss", 0.5), "00:00", "mm:ss 里 mm 是分钟");
        EQS(f("yyyy-mm", ymdToSerial(2026, 9, 27)), "2026-09", "yyyy 后的 mm 是月");
        EQS(f("m/d/yyyy", ymdToSerial(2026, 9, 27)), "9/27/2026", "m 在开头是月");

        // 累计时长 [h]
        EQS(f("[h]", 1.5), "36", "[h] 累计小时");
        EQS(f("[h]:mm", 1.5), "36:00", "[h]:mm");
        EQS(f("[m]", 1.0), "1440", "[m] 累计分钟");
        EQS(f("[s]", 1.0), "86400", "[s] 累计秒");
    }

    // ------------------------------------------------------------------
    std::cout << "== 文本段 ==\n";
    {
        NumFmt nf; std::string err;
        OK(nf.parse("\"单价:\"@", err), "纯文本格式可解析: " + err);
        EQS(nf.formatText("abc"), "单价:abc", "@ 替换");
        EQS(nf.format(123), "123", "数字走通用显示");
        // 数字格式的第 4 段
        NumFmt nf2;
        OK(nf2.parse("0.00;-0.00;0;\"文本:\"@", err), "四段格式可解析");
        EQS(nf2.formatText("x"), "文本:x", "第 4 段用于文本");
        EQS(nf2.format(1.5), "1.50", "数字仍走第 1 段");
        // 没有 @ 时文本原样
        NumFmt nf3;
        nf3.parse("0.00", err);
        EQS(nf3.formatText("abc"), "abc", "无 @ 段时原样返回");
    }

    // ------------------------------------------------------------------
    std::cout << "== 通用与其它值 ==\n";
    {
        NumFmt g; std::string err;
        OK(g.parse("General", err), "General 可解析");
        OK(g.general(), "General 标记为通用");
        EQS(g.format(1234.5678), "1234.5678", "General 原样");

        NumFmt gp;
        gp.parse("yyyy-mm-dd", err);
        OK(gp.isDateTime(), "日期格式被识别");
        OK(!gp.isPercent(), "日期格式不是百分比");
        NumFmt pp;
        pp.parse("0.00%", err);
        OK(pp.isPercent(), "百分比被识别");
        OK(!pp.isDateTime(), "百分比不是日期");

        // 快捷接口
        EQS(formatValueByCode(Value::num(1234.5678), "#,##0.00"), "1,234.57", "formatValueByCode 数字");
        EQS(formatValueByCode(Value::str("abc"), "0.00"), "abc", "formatValueByCode 文本");
        EQS(formatValueByCode(Value::boolean(true), "0.00"), "TRUE", "布尔不套数字格式");
        EQS(formatValueByCode(Value::error(Err::Div0), "0.00"), "#DIV/0!", "错误值原样");
        EQS(formatValueByCode(Value::empty(), "0.00"), "", "空值");

        // 非法格式码应报错而不是崩溃
        NumFmt bad; std::string be;
        OK(!bad.parse("[abc]0", be), "无法识别的方括号报错: " + be);
        NumFmt bad2; std::string be2;
        OK(!bad2.parse("0\"未闭合", be2), "引号未闭合报错: " + be2);
    }

    // ------------------------------------------------------------------
    std::cout << "== 内建格式 ID ==\n";
    {
        EQS(std::string(builtinNumFmt(0)), "General", "ID 0");
        EQS(std::string(builtinNumFmt(2)), "0.00", "ID 2");
        EQS(std::string(builtinNumFmt(3)), "#,##0", "ID 3");
        EQS(std::string(builtinNumFmt(9)), "0%", "ID 9");
        EQS(std::string(builtinNumFmt(14)), "mm-dd-yy", "ID 14");
        OK(builtinNumFmt(99) != nullptr && builtinNumFmt(99)[0] == '\0', "未知 ID 返回空");

        // 常用内建码都能解析
        for (int id : {1, 2, 3, 4, 9, 10, 11, 14, 15, 16, 17, 18, 19, 20, 21, 22, 45, 46, 48, 49}) {
            NumFmt nf; std::string err;
            const char* code = builtinNumFmt(id);
            if (!nf.parse(code, err)) {
                N++; std::cout << "  FAIL 内建 ID " << id << " (" << code << ") 解析失败: " << err << "\n";
            } else P++;
        }
        // 抽查几个的实际效果
        EQS(f(builtinNumFmt(3), 1234567), "1,234,567", "ID 3 效果");
        EQS(f(builtinNumFmt(10), 0.1234), "12.34%", "ID 10 效果");
        EQS(f(builtinNumFmt(14), ymdToSerial(2026, 9, 27)), "09-27-26", "ID 14 效果");
        EQS(f(builtinNumFmt(21), 0.5), "12:00:00", "ID 21 效果");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
