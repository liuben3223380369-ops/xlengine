#pragma once
// ---------------------------------------------------------------------------
// 数字格式（Excel number format）
//
// 这是"像不像 Excel"最关键的一块，也是此前的空白：
// 单元格里存的是 DATE(2026,9,27) = 46262，用户看到 46262 是没法用的。
// 必须靠格式码把它显示成 2026-09-27。
//
// 格式码结构（最多 4 段，分号分隔）：
//     正数 ; 负数 ; 零 ; 文本
// 只有 1 段时它适用于所有数；2 段时第 1 段用于正数与零、第 2 段用于负数。
//
// 占位符：
//   0      强制数位，不足补 0
//   #      可选数位，不补
//   ?      可选数位，补空格（用于小数点对齐）
//   . ,    小数点 / 千分位；末尾的逗号表示除以 1000
//   %      数值乘 100
//   E+ e+  科学计数法
//   yyyy yy m mm mmm mmmm d dd ddd dddd      日期
//   h hh m mm s ss AM/PM                     时间（m 紧跟 h 之后是"分钟"）
//   [h] [m] [s]                              累计时长（不取模）
//   @      文本占位
//   \x     字面量 x
//   "..."  字面文本
//   *c     重复 c 填满列宽
//   _c     跳过一个 c 的宽度
//   [Red]  颜色（本模块解析但不着色，交给上层）
//   [>0]   条件段
//
// 已知边界：
//   - 颜色与填充/跳过宽度只解析不渲染（无列宽信息，且不影响数值正确性）
//   - 泰文/印度等 locale 相关格式码未实现
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include "value.hpp"

namespace xl {

// 内建格式 ID。Excel 的 numFmtId 0..49 是预定义的，写 xlsx 时直接引用，
// 不需要在 styles.xml 里声明 numFmt 元素。
const char* builtinNumFmt(int id);

class NumFmt {
public:
    // 解析格式码。失败时 err 非空。空串 / "General" 视为通用格式。
    bool parse(const std::string& code, std::string& err);

    const std::string& code() const { return code_; }
    bool general() const { return general_; }
    // 段里含日期或时间占位符 —— 数值应按序列号解释
    bool isDateTime() const { return isDateTime_; }
    bool isPercent() const { return isPercent_; }

    // 格式化数值
    std::string format(double v) const;
    // 格式化文本（走 @ 段；没有 @ 段时原样返回）
    std::string formatText(const std::string& s) const;
    // 格式化布尔（Excel 里 TRUE 不套数字格式，原样输出）
    std::string formatBool(bool b) const;

    // 取该段声明的颜色（如 "Red"），没有则空
    std::string colorOf(double v) const;

    struct Tok {
        enum K {
            Lit,        // 字面文本
            Zero, Hash, Ques,           // 0 # ?
            Dot, Comma, Pct,            // . , %
            Sci,                        // E+ / e+
            Year, Month, Day,           // yyyy / mm / dd …
            Hour, Minute, Second,       // hh / mm / ss
            ElapsedH, ElapsedM, ElapsedS,   // [h] [m] [s]
            AmPm,
            At,         // @
            Fill, Skip  // *x _x
        } k = Lit;
        std::string lit;      // Lit 的文本；Sci 存 "E+" / "e-"
        int count = 1;        // 重复次数（yyyy=4）
    };

    struct Section {
        std::vector<Tok> toks;
        std::string color;
        bool hasCond = false;
        int condOp = 0;               // 1:< 2:<= 3:> 4:>= 5:= 6:<>
        double condVal = 0;
        int scaleCommas = 0;          // 末尾逗号个数（每个除以 1000）
        bool hasComma = false;        // 千分位
        bool hasPercent = false;
        bool isDate = false, isTime = false;
        bool hasAmPm = false;
    };

private:
    std::string code_;
    bool general_ = true;
    bool isDateTime_ = false;
    bool isPercent_ = false;
    std::vector<Section> secs_;       // 0..3：正 / 负 / 零 / 文本

    const Section* pick(double v) const;
    static std::string renderSection(const Section& s, double v);
    static std::string renderNumeric(const Section& s, double v, int& sciExp);
    static std::string renderDateTime(const Section& s, double v);
};

// 一步到位的便捷接口
std::string formatValue(const Value& v, const NumFmt& fmt);
// 直接给格式码（内部会缓存解析结果，避免每个单元格重复解析）
std::string formatValueByCode(const Value& v, const std::string& code);

} // namespace xl
