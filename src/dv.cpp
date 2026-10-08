#include "dv.hpp"
#include "sheet.hpp"
#include "refshift.hpp"
#include <algorithm>
#include <cmath>

namespace xl {

// ---------------------------------------------------------------------------
// 枚举互转
// ---------------------------------------------------------------------------
const char* dvTypeToOoxml(DvType t) {
    switch (t) {
        case DvType::None:       return "none";
        case DvType::Whole:      return "whole";
        case DvType::Decimal:    return "decimal";
        case DvType::List:       return "list";
        case DvType::Date:       return "date";
        case DvType::Time:       return "time";
        case DvType::TextLength: return "textLength";
        case DvType::Custom:     return "custom";
    }
    return "none";
}

const char* dvOperatorToOoxml(DvOperator o) {
    switch (o) {
        case DvOperator::Between:            return "between";
        case DvOperator::NotBetween:         return "notBetween";
        case DvOperator::Equal:              return "equal";
        case DvOperator::NotEqual:           return "notEqual";
        case DvOperator::GreaterThan:        return "greaterThan";
        case DvOperator::LessThan:           return "lessThan";
        case DvOperator::GreaterThanOrEqual: return "greaterThanOrEqual";
        case DvOperator::LessThanOrEqual:    return "lessThanOrEqual";
        case DvOperator::None:               return "";
    }
    return "";
}

const char* dvErrorStyleToOoxml(DvErrorStyle s) {
    switch (s) {
        case DvErrorStyle::Stop:        return "stop";
        case DvErrorStyle::Warning:     return "warning";
        case DvErrorStyle::Information: return "information";
    }
    return "stop";
}

bool ooxmlToDvType(const std::string& s, DvType& out) {
    if (s == "none")       { out = DvType::None; return true; }
    if (s == "whole")      { out = DvType::Whole; return true; }
    if (s == "decimal")    { out = DvType::Decimal; return true; }
    if (s == "list")       { out = DvType::List; return true; }
    if (s == "date")       { out = DvType::Date; return true; }
    if (s == "time")       { out = DvType::Time; return true; }
    if (s == "textLength") { out = DvType::TextLength; return true; }
    if (s == "custom")     { out = DvType::Custom; return true; }
    return false;
}

bool ooxmlToDvOperator(const std::string& s, DvOperator& out) {
    if (s == "between")            { out = DvOperator::Between; return true; }
    if (s == "notBetween")         { out = DvOperator::NotBetween; return true; }
    if (s == "equal")              { out = DvOperator::Equal; return true; }
    if (s == "notEqual")           { out = DvOperator::NotEqual; return true; }
    if (s == "greaterThan")        { out = DvOperator::GreaterThan; return true; }
    if (s == "lessThan")           { out = DvOperator::LessThan; return true; }
    if (s == "greaterThanOrEqual") { out = DvOperator::GreaterThanOrEqual; return true; }
    if (s == "lessThanOrEqual")    { out = DvOperator::LessThanOrEqual; return true; }
    return false;
}

bool ooxmlToDvErrorStyle(const std::string& s, DvErrorStyle& out) {
    if (s == "stop")        { out = DvErrorStyle::Stop; return true; }
    if (s == "warning")     { out = DvErrorStyle::Warning; return true; }
    if (s == "information") { out = DvErrorStyle::Information; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// 序列（list）的拆分与拼装
// ---------------------------------------------------------------------------
std::vector<std::string> splitListFormula(const std::string& f) {
    std::string s = f;
    // Excel 写的是 "a,b,c"（带引号），也可能直接是区域引用 A1:A5。
    // 先剥掉首尾成对引号；区域引用不含引号，会原样走切分逻辑。
    while (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);

    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string joinListFormula(const std::vector<std::string>& items) {
    std::string s;
    for (size_t i = 0; i < items.size(); i++) {
        if (i) s += ",";
        s += items[i];
    }
    return "\"" + s + "\"";      // Excel 要求带引号
}

// ---------------------------------------------------------------------------
// 判定
// ---------------------------------------------------------------------------

const DataValidation* dvForCell(const std::vector<DataValidation>& list, int col, int row) {
    for (const DataValidation& dv : list)
        for (const auto& r : dv.rects)
            if (col >= r[0] && col <= r[2] && row >= r[1] && row <= r[3])
                return &dv;
    return nullptr;
}

// 按偏移量求值规则的边界公式（与条件格式同一套语义）
static Value evalDvFormula(Sheet& sh, const std::string& f,
                           int anchorC, int anchorR, int col, int row) {
    if (f.empty()) return Value::error(Err::Value);
    int dc = col - anchorC, dr = row - anchorR;
    std::string text = f;
    if (dc != 0 || dr != 0) {
        ShiftResult sr = shiftFormula(f, dc, dr);
        text = sr.error.empty() ? sr.text : f;
    }
    return sh.evalExprAt(text, col, row);
}

// 取出规则的"锚点"：第一个区域的左上角
static void anchorOf(const DataValidation& dv, int& c, int& r) {
    if (dv.rects.empty()) { c = 0; r = 0; return; }
    c = dv.rects[0][0];
    r = dv.rects[0][1];
}

static bool numOf(const Value& v, double& out) {
    if (v.isNum()) { out = v.n; return std::isfinite(out); }
    if (v.isBool()) { out = v.b ? 1 : 0; return true; }
    return false;
}

DvVerdict checkDataValidation(Sheet& sh, const DataValidation& dv, int col, int row,
                              const Value& v) {
    DvVerdict out;
    out.stop = (dv.errorStyle == DvErrorStyle::Stop);

    auto fail = [&](const std::string& why) {
        out.ok = false;
        out.message = dv.error.empty() ? why : dv.error;
        return out;
    };

    // 空格子：allowBlank 为真则放行
    if (v.isEmpty()) {
        if (dv.allowBlank) return out;
        return fail("不能为空");
    }
    // 任何规则都不接受错误值
    if (v.isError()) return fail("单元格是错误值");

    int ac = 0, ar = 0;
    anchorOf(dv, ac, ar);

    switch (dv.type) {
        case DvType::None:
            return out;

        case DvType::Custom: {
            Value r = evalDvFormula(sh, dv.formula1, ac, ar, col, row);
            // 非零即通过。错误值视为不通过，而不是"通过"
            bool pass = r.isNum() ? (r.n != 0) : (r.isBool() && r.b);
            return pass ? out : fail("不满足自定义条件");
        }

        case DvType::List: {
            // 候选项可能来自 formula1 的逗号串，也可能来自区域引用
            std::vector<std::string> items = dv.listItems;
            if (items.empty() && !dv.formula1.empty()) {
                // 区域引用：取该区域所有非空值作为候选
                Value rv = evalDvFormula(sh, dv.formula1, ac, ar, ac, ar);
                std::string f = dv.formula1;
                if (!f.empty() && f.front() != '"') {
                    // 形如 A1:A5 —— 走引用求值，把每个格子的文本当候选项
                    Value arr = sh.evalExprAt(f, ac, ar);
                    (void)rv;
                    if (arr.isArray() && arr.arr) {
                        for (auto& rowVals : *arr.arr)
                            for (auto& cv : rowVals)
                                if (!cv.isEmpty()) items.push_back(valueToText(cv));
                    }
                } else {
                    items = splitListFormula(dv.formula1);
                }
            }
            if (items.empty()) items = splitListFormula(dv.formula1);

            std::string want = valueToText(v);
            for (auto& it : items)
                if (it == want) return out;
            return fail("不在允许的列表内");
        }

        case DvType::Whole:
        case DvType::Decimal: {
            double d = 0;
            if (!numOf(v, d)) return fail("不是数值");
            if (dv.type == DvType::Whole) {
                // 整数判定：必须用 trunc 后的值与原值比较，
                // 不能只判 fmod(d,1)==0 —— 对很大的数 fmod 精度不够
                if (std::fabs(d - std::trunc(d)) > 1e-9) return fail("必须是整数");
            }
            double lo = 0, hi = 0;
            if (!numOf(evalDvFormula(sh, dv.formula1, ac, ar, col, row), lo))
                return fail("下界不是数值");
            if (dv.op == DvOperator::Between || dv.op == DvOperator::NotBetween) {
                if (!numOf(evalDvFormula(sh, dv.formula2, ac, ar, col, row), hi))
                    return fail("上界不是数值");
                bool in = (d >= lo && d <= hi);
                bool pass = (dv.op == DvOperator::Between) ? in : !in;
                return pass ? out : fail("不在允许的数值范围内");
            }
            bool pass;
            switch (dv.op) {
                case DvOperator::Equal:              pass = (d == lo); break;
                case DvOperator::NotEqual:           pass = (d != lo); break;
                case DvOperator::GreaterThan:        pass = (d > lo);  break;
                case DvOperator::LessThan:           pass = (d < lo);  break;
                case DvOperator::GreaterThanOrEqual: pass = (d >= lo); break;
                case DvOperator::LessThanOrEqual:    pass = (d <= lo); break;
                default:                             pass = true;      break;
            }
            return pass ? out : fail("不满足数值条件");
        }

        case DvType::TextLength: {
            // 长度按显示文本的码点数算（不是字节数）——
            // 中文若按字节算，一个字会变成 3，规则形同虚设
            std::string t = valueToText(v);
            size_t n = 0;
            for (size_t i = 0; i < t.size();) {
                unsigned char c = (unsigned char)t[i];
                size_t adv = 1;
                if (c >= 0xF0) adv = 4; else if (c >= 0xE0) adv = 3;
                else if (c >= 0xC0) adv = 2;
                i += adv; n++;
            }
            double d = (double)n;
            double lo = 0, hi = 0;
            if (!numOf(evalDvFormula(sh, dv.formula1, ac, ar, col, row), lo))
                return fail("长度下界不是数值");
            if (dv.op == DvOperator::Between || dv.op == DvOperator::NotBetween) {
                if (!numOf(evalDvFormula(sh, dv.formula2, ac, ar, col, row), hi))
                    return fail("长度上界不是数值");
                bool in = (d >= lo && d <= hi);
                bool pass = (dv.op == DvOperator::Between) ? in : !in;
                return pass ? out : fail("文本长度不在允许范围内");
            }
            bool pass;
            switch (dv.op) {
                case DvOperator::Equal:              pass = (d == lo); break;
                case DvOperator::NotEqual:           pass = (d != lo); break;
                case DvOperator::GreaterThan:        pass = (d > lo);  break;
                case DvOperator::LessThan:           pass = (d < lo);  break;
                case DvOperator::GreaterThanOrEqual: pass = (d >= lo); break;
                case DvOperator::LessThanOrEqual:    pass = (d <= lo); break;
                default:                             pass = true;      break;
            }
            return pass ? out : fail("文本长度不满足条件");
        }

        case DvType::Date:
        case DvType::Time: {
            // 日期/时间在单元格里存的是序列号，所以退化成数值比较。
            // 真正的日历语义需要把输入解析成序列号，界面层暂不支持 ——
            // 这里至少保证"数值区间"这类规则能正常工作。
            double d = 0;
            if (!numOf(v, d)) return fail(dv.type == DvType::Date ? "不是日期序列号" : "不是时间序列号");
            double lo = 0, hi = 0;
            if (!numOf(evalDvFormula(sh, dv.formula1, ac, ar, col, row), lo))
                return fail("下界不是数值");
            if (dv.op == DvOperator::Between || dv.op == DvOperator::NotBetween) {
                if (!numOf(evalDvFormula(sh, dv.formula2, ac, ar, col, row), hi))
                    return fail("上界不是数值");
                bool in = (d >= lo && d <= hi);
                bool pass = (dv.op == DvOperator::Between) ? in : !in;
                return pass ? out : fail("不在允许的时间范围内");
            }
            bool pass;
            switch (dv.op) {
                case DvOperator::Equal:              pass = (d == lo); break;
                case DvOperator::NotEqual:           pass = (d != lo); break;
                case DvOperator::GreaterThan:        pass = (d > lo);  break;
                case DvOperator::LessThan:           pass = (d < lo);  break;
                case DvOperator::GreaterThanOrEqual: pass = (d >= lo); break;
                case DvOperator::LessThanOrEqual:    pass = (d <= lo); break;
                default:                             pass = true;      break;
            }
            return pass ? out : fail("不满足时间条件");
        }
    }
    return out;
}

} // namespace xl
