#include "numfmt.hpp"
#include "date.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <map>

namespace xl {

// ---------------------------------------------------------------------------
// Excel 内建格式 ID（0..49）
// ---------------------------------------------------------------------------
const char* builtinNumFmt(int id) {
    switch (id) {
        case 0:  return "General";
        case 1:  return "0";
        case 2:  return "0.00";
        case 3:  return "#,##0";
        case 4:  return "#,##0.00";
        case 5:  return "$#,##0_);($#,##0)";
        case 6:  return "$#,##0_);[Red]($#,##0)";
        case 7:  return "$#,##0.00_);($#,##0.00)";
        case 8:  return "$#,##0.00_);[Red]($#,##0.00)";
        case 9:  return "0%";
        case 10: return "0.00%";
        case 11: return "0.00E+00";
        case 12: return "# ?\?/?";
        case 13: return "# ?\?/??";
        case 14: return "mm-dd-yy";
        case 15: return "d-mmm-yy";
        case 16: return "d-mmm";
        case 17: return "mmm-yy";
        case 18: return "h:mm AM/PM";
        case 19: return "h:mm:ss AM/PM";
        case 20: return "h:mm";
        case 21: return "h:mm:ss";
        case 22: return "m/d/yy h:mm";
        case 37: return "#,##0_);(#,##0)";
        case 38: return "#,##0_);[Red](#,##0)";
        case 39: return "#,##0.00_);(#,##0.00)";
        case 40: return "#,##0.00_);[Red](#,##0.00)";
        case 41: return "_(* #,##0_);_(* (#,##0);_(* \"-\"_);_(@_)";
        case 42: return "_(\"$\"* #,##0_);_(\"$\"* (#,##0);_(\"$\"* \"-\"_);_(@_)";
        case 43: return "_(* #,##0.00_);_(* (#,##0.00);_(* \"-\"??_);_(@_)";
        case 44: return "_(\"$\"* #,##0.00_);_(\"$\"* (#,##0.00);_(\"$\"* \"-\"??_);_(@_)";
        case 45: return "mm:ss";
        case 46: return "[h]:mm:ss";
        case 47: return "mmss.0";
        case 48: return "##0.0E+0";
        case 49: return "@";
        default: return "";
    }
}

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------
namespace {

// 按顶层分号切段。注意：引号内和方括号内的分号不是分隔符 ——
// 少了这条判断，`"a;b"` 或 `[Red];-0` 会被错误地劈成两段。
std::vector<std::string> splitSections(const std::string& code) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false, inBracket = false;
    for (size_t i = 0; i < code.size(); i++) {
        char c = code[i];
        if (c == '"')  { inQuote = !inQuote; cur += c; continue; }
        if (c == '[')  { inBracket = true;  cur += c; continue; }
        if (c == ']')  { inBracket = false; cur += c; continue; }
        if (c == ';' && !inQuote && !inBracket) { out.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    out.push_back(cur);
    return out;
}

bool isColorName(const std::string& s, std::string& canon) {
    static const std::map<std::string, std::string> kColors = {
        {"black", "Black"}, {"blue", "Blue"}, {"cyan", "Cyan"}, {"green", "Green"},
        {"magenta", "Magenta"}, {"red", "Red"}, {"white", "White"}, {"yellow", "Yellow"},
    };
    std::string low;
    for (char c : s) low += (char)std::tolower((unsigned char)c);
    auto it = kColors.find(low);
    if (it == kColors.end()) return false;
    canon = it->second;
    return true;
}

// 数一个字符从 i 开始的连续重复次数
int runLength(const std::string& s, size_t i, char c) {
    int n = 0;
    while (i + n < s.size() && s[i + n] == c) n++;
    return n;
}

} // namespace

bool NumFmt::parse(const std::string& code, std::string& err) {
    err.clear();
    code_ = code;
    secs_.clear();
    general_ = true;
    isDateTime_ = false;
    isPercent_ = false;

    if (code.empty()) return true;
    // "General" / "@" 之外的任何内容都说明这不是通用格式
    std::string low;
    for (char c : code) low += (char)std::tolower((unsigned char)c);
    if (low == "general") { general_ = true; return true; }
    general_ = false;

    std::vector<std::string> raw = splitSections(code);
    if (raw.size() > 4) raw.resize(4);

    for (size_t si = 0; si < raw.size(); si++) {
        const std::string& src = raw[si];
        Section sec;

        // m 的歧义必须在解析前就定下来："h:mm" 里是分钟，"yyyy-mm" 里是月。
        // 只靠"前面出现过 h"判断是不够的 —— "mm:ss"（没有 h）里 mm 也是分钟。
        // 所以先扫一遍整段，按有没有时间类占位符来定性。
        bool ctxTime = false;
        {
            bool inQ = false, inB = false;
            for (size_t k = 0; k < src.size(); k++) {
                char ch = src[k];
                if (ch == '"') { inQ = !inQ; continue; }
                if (inQ) continue;
                if (ch == '[') { inB = true; continue; }
                if (ch == ']') { inB = false; continue; }
                if (inB) {
                    if (ch == 'h' || ch == 'H' || ch == 'm' || ch == 'M' || ch == 's' || ch == 'S')
                        ctxTime = true;
                    continue;
                }
                if (ch == '\\') { k++; continue; }
                if (ch == 'h' || ch == 'H' || ch == 's' || ch == 'S') ctxTime = true;
                if ((ch == 'A' || ch == 'a') && k + 4 < src.size()) {
                    std::string w = src.substr(k, 5);
                    std::string up;
                    for (char x : w) up += (char)std::toupper((unsigned char)x);
                    if (up == "AM/PM") ctxTime = true;
                }
            }
        }
        bool seenHour = false;
        size_t i = 0;

        while (i < src.size()) {
            char c = src[i];

            // [ ... ]：颜色 / 条件 / 累计时长
            if (c == '[') {
                size_t j = src.find(']', i);
                if (j == std::string::npos) { err = "方括号未闭合"; return false; }
                std::string body = src.substr(i + 1, j - i - 1);
                i = j + 1;
                std::string col;
                if (isColorName(body, col)) { sec.color = col; continue; }
                if (!body.empty() && (body[0] == 'h' || body[0] == 'H')) {
                    Tok t; t.k = Tok::ElapsedH; t.count = 1;
                    if (body.size() > 1) { err = "暂不支持 [" + body + "]"; return false; }
                    sec.toks.push_back(t); sec.isTime = true; continue;
                }
                if (!body.empty() && (body[0] == 'm' || body[0] == 'M')) {
                    Tok t; t.k = Tok::ElapsedM; sec.toks.push_back(t); sec.isTime = true; continue;
                }
                if (!body.empty() && (body[0] == 's' || body[0] == 'S')) {
                    Tok t; t.k = Tok::ElapsedS; sec.toks.push_back(t); sec.isTime = true; continue;
                }
                // 条件：[>0] [<=100] [=5] [<>3]
                int op = 0; size_t k = 0;
                if (body.compare(0, 2, "<=") == 0) { op = 2; k = 2; }
                else if (body.compare(0, 2, ">=") == 0) { op = 4; k = 2; }
                else if (body.compare(0, 2, "<>") == 0) { op = 6; k = 2; }
                else if (body[0] == '<') { op = 1; k = 1; }
                else if (body[0] == '>') { op = 3; k = 1; }
                else if (body[0] == '=') { op = 5; k = 1; }
                if (op == 0) { err = "无法识别的方括号内容: [" + body + "]"; return false; }
                try {
                    sec.condVal = std::stod(body.substr(k));
                } catch (...) { err = "条件值不是数字: [" + body + "]"; return false; }
                sec.hasCond = true; sec.condOp = op;
                continue;
            }

            // 转义：\x
            if (c == '\\') {
                if (i + 1 >= src.size()) { err = "转义符后缺字符"; return false; }
                Tok t; t.k = Tok::Lit; t.lit = std::string(1, src[i + 1]);
                sec.toks.push_back(t); i += 2; continue;
            }
            // 字面文本："..."
            if (c == '"') {
                size_t j = src.find('"', i + 1);
                if (j == std::string::npos) { err = "引号未闭合"; return false; }
                Tok t; t.k = Tok::Lit; t.lit = src.substr(i + 1, j - i - 1);
                sec.toks.push_back(t); i = j + 1; continue;
            }
            // 填充 / 跳宽：*x _x
            if (c == '*' || c == '_') {
                if (i + 1 >= src.size()) { err = "缺少填充字符"; return false; }
                Tok t; t.k = (c == '*') ? Tok::Fill : Tok::Skip; t.lit = std::string(1, src[i + 1]);
                sec.toks.push_back(t); i += 2; continue;
            }
            // 文本占位
            if (c == '@') { Tok t; t.k = Tok::At; sec.toks.push_back(t); i++; continue; }
            if (c == '%') {
                Tok t; t.k = Tok::Pct; sec.toks.push_back(t);
                sec.hasPercent = true; isPercent_ = true; i++; continue;
            }
            if (c == '.') { Tok t; t.k = Tok::Dot; sec.toks.push_back(t); i++; continue; }
            if (c == ',') { Tok t; t.k = Tok::Comma; sec.toks.push_back(t); i++; continue; }
            if (c == '0' || c == '#' || c == '?') {
                int n = 1;
                while (i + n < src.size() &&
                       (src[i + n] == '0' || src[i + n] == '#' || src[i + n] == '?')) n++;
                // 三种占位符混写时逐个处理，保持各自的补位语义
                for (int k = 0; k < n; k++) {
                    Tok t;
                    t.k = (src[i + k] == '0') ? Tok::Zero
                        : (src[i + k] == '#') ? Tok::Hash : Tok::Ques;
                    sec.toks.push_back(t);
                }
                i += n; continue;
            }
            // 科学计数 E+ / E- / e+ / e-
            if (c == 'E' || c == 'e') {
                if (i + 1 < src.size() && (src[i + 1] == '+' || src[i + 1] == '-')) {
                    Tok t; t.k = Tok::Sci; t.lit = std::string(1, c) + src[i + 1];
                    sec.toks.push_back(t); i += 2; continue;
                }
            }
            // AM/PM
            if ((c == 'A' || c == 'a') && i + 4 < src.size()) {
                std::string w = src.substr(i, 5);
                std::string up;
                for (char ch : w) up += (char)std::toupper((unsigned char)ch);
                if (up == "AM/PM") {
                    Tok t; t.k = Tok::AmPm;
                    t.lit = (c == 'A') ? "AM/PM" : "am/pm";
                    sec.toks.push_back(t); sec.isTime = true; sec.hasAmPm = true; i += 5; continue;
                }
            }
            // y / d：总是日期
            if (c == 'y' || c == 'Y') {
                int n = runLength(src, i, c);
                Tok t; t.k = Tok::Year; t.count = n; sec.toks.push_back(t);
                sec.isDate = true; i += (size_t)n; continue;
            }
            if (c == 'd' || c == 'D') {
                int n = runLength(src, i, c);
                Tok t; t.k = Tok::Day; t.count = n > 4 ? 4 : n; sec.toks.push_back(t);
                sec.isDate = true; i += (size_t)n; continue;
            }
            // h：时间
            if (c == 'h' || c == 'H') {
                int n = runLength(src, i, c);
                Tok t; t.k = Tok::Hour; t.count = n > 2 ? 2 : n;
                sec.toks.push_back(t); sec.isTime = true; seenHour = true; i += (size_t)n; continue;
            }
            // s：秒（只有在时间上下文里才是秒）
            if (c == 's' || c == 'S') {
                int n = runLength(src, i, c);
                Tok t; t.k = Tok::Second; t.count = n > 2 ? 2 : n;
                sec.toks.push_back(t); sec.isTime = true; i += (size_t)n; continue;
            }
            // m：前面出现过 h 就是分钟，否则是月 —— Excel 的经典歧义规则
            if (c == 'm' || c == 'M') {
                int n = runLength(src, i, c);
                Tok t;
                // 有时间上下文（h / s / AM/PM / [h]）就是分钟；否则是月。
                // 二者都没有时按 Excel 的默认：当作月。
                if (ctxTime || seenHour) { t.k = Tok::Minute; t.count = n > 2 ? 2 : n; sec.isTime = true; }
                else { t.k = Tok::Month; t.count = n > 4 ? 4 : n; sec.isDate = true; }
                sec.toks.push_back(t); i += (size_t)n; continue;
            }
            // 其它字符：字面量
            {
                Tok t; t.k = Tok::Lit; t.lit = std::string(1, c);
                sec.toks.push_back(t); i++; continue;
            }
        }

        // 日期/时间格式里的逗号是普通标点，不是千分位标记。
        // "mmmm d, yyyy" 若把逗号当千分位吃掉，输出会变成 "September 27 2026"。
        if (sec.isDate || sec.isTime) {
            for (Tok& t : sec.toks)
                if (t.k == Tok::Comma) { t.k = Tok::Lit; t.lit = ","; }
        }

        // 末尾的逗号是"除以 1000"，不是千分位分隔符。
        // 必须从后往前数：#,##0, 里的第一个逗号才是千分位。
        size_t tail = sec.toks.size();
        while (tail > 0 && sec.toks[tail - 1].k == Tok::Comma) { tail--; sec.scaleCommas++; }
        sec.toks.resize(tail);
        for (const Tok& t : sec.toks)
            if (t.k == Tok::Comma) sec.hasComma = true;

        if (sec.isDate || sec.isTime) isDateTime_ = true;
        secs_.push_back(sec);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 选段
// ---------------------------------------------------------------------------
const NumFmt::Section* NumFmt::pick(double v) const {
    if (secs_.empty()) return nullptr;

    // 条件段优先：Excel 先扫带条件的段
    for (const Section& s : secs_) {
        if (!s.hasCond) continue;
        bool hit = false;
        switch (s.condOp) {
            case 1: hit = (v <  s.condVal); break;
            case 2: hit = (v <= s.condVal); break;
            case 3: hit = (v >  s.condVal); break;
            case 4: hit = (v >= s.condVal); break;
            case 5: hit = (v == s.condVal); break;
            case 6: hit = (v != s.condVal); break;
            default: break;
        }
        if (hit) return &s;
    }
    // 无条件的段按 正 / 负 / 零 取用。
    // 只有一段时它适用于所有数 —— 包括负数（此时负号来自数值本身，段里通常含 -）。
    std::vector<const Section*> plain;
    for (const Section& s : secs_) if (!s.hasCond) plain.push_back(&s);
    if (plain.empty()) return &secs_[0];
    if (plain.size() == 1) return plain[0];
    if (v > 0)  return plain[0];
    if (v < 0)  return plain[1];
    return plain.size() > 2 ? plain[2] : plain[0];
}

// ---------------------------------------------------------------------------
// 数值段渲染
// ---------------------------------------------------------------------------
namespace {

// 把 v 按整数/小数占位符模板渲染出来
// intToks / fracToks 是模板，neg 表示要去掉符号（符号由模板里的字面量或前置 - 给出）
// 按模板渲染整数 + 小数部分。
//
// 两个不能用直觉写的地方：
//
// 1. 整数部分必须从右往左分配数字。'#' 是可选位，只有更高位存在有效数字时才该出现；
//    从左往右写会把 "#,##0" 显示 5 变成 "005"。
// 2. 字面量不能一律塞到最前面。"合计:"0 显示 42 若按"补高位再拼字面量"处理，
//    会得到 "4合计:2" —— 多余的高位数字必须插在第一个数字占位符之前。
std::string applyDigitTemplate(const std::vector<NumFmt::Tok>& intToks,
                               const std::vector<NumFmt::Tok>& fracToks,
                               bool comma, const std::string& digits,
                               int intLen, int fracLen) {
    std::string intDigits = digits.substr(0, (size_t)intLen);
    std::string fracDigits = digits.substr((size_t)intLen, (size_t)fracLen);

    // 模板里根本没有数字占位符（例如条件段 [>100]"大"）：只输出字面量，不输出数字。
    bool anyPlaceholder = false;
    for (const auto& t : intToks)  if (t.k == NumFmt::Tok::Zero || t.k == NumFmt::Tok::Hash || t.k == NumFmt::Tok::Ques) anyPlaceholder = true;
    for (const auto& t : fracToks) if (t.k == NumFmt::Tok::Zero || t.k == NumFmt::Tok::Hash || t.k == NumFmt::Tok::Ques) anyPlaceholder = true;
    if (!anyPlaceholder) {
        std::string only;
        for (const auto& t : intToks) if (t.k == NumFmt::Tok::Lit) only += t.lit;
        for (const auto& t : fracToks) if (t.k == NumFmt::Tok::Lit) only += t.lit;
        return only;
    }

    // ---- 整数部分：从右往左给占位符分配数字 ----
    std::vector<int> tmplIdx;                 // 占位符在 intToks 中的下标
    for (size_t i = 0; i < intToks.size(); i++) {
        const auto& t = intToks[i];
        if (t.k == NumFmt::Tok::Zero || t.k == NumFmt::Tok::Hash || t.k == NumFmt::Tok::Ques)
            tmplIdx.push_back((int)i);
    }
    int nTmpl = (int)tmplIdx.size();
    std::vector<char> outChar((size_t)nTmpl, '\0');
    {
        int ti = nTmpl - 1;
        int di = (int)intDigits.size() - 1;
        while (ti >= 0) {
            const auto& t = intToks[(size_t)tmplIdx[(size_t)ti]];
            if (di >= 0) { outChar[(size_t)ti] = intDigits[(size_t)di]; di--; }
            else if (t.k == NumFmt::Tok::Zero) outChar[(size_t)ti] = '0';
            else if (t.k == NumFmt::Tok::Ques) outChar[(size_t)ti] = ' ';
            else outChar[(size_t)ti] = '\0';      // '#' 且无数字 -> 不输出
            ti--;
        }
        // 模板位数不够（"0" 显示 1234）：剩余高位原样保留
        if (di >= 0) {
            std::vector<char> hi((size_t)(di + 1) + outChar.size());
            for (int k = 0; k <= di; k++) hi[(size_t)k] = intDigits[(size_t)k];
            for (size_t k = 0; k < outChar.size(); k++) hi[(size_t)(di + 1) + k] = outChar[k];
            outChar = hi;
            // 模板最左的占位符变成"已有数字"，重新对齐：把多余位并到最前
            // 此时 outChar 的前 di+1 个就是高位数字
        }
    }

    // 总数字位数（用于千分位分组）
    int nOut = 0;
    for (char c : outChar) if (c != '\0') nOut++;

    // ---- 从左往右拼装：多余高位 -> 模板顺序（含字面量） ----
    std::string built;
    int emitted = 0;
    auto emitDigit = [&](char d) {
        built += d;
        emitted++;
        if (comma && emitted < nOut && (nOut - emitted) % 3 == 0) built += ',';
    };
    {
        // outChar 前 extraN 个是"模板放不下的高位数字"，后面才是模板占位符的结果。
        // 高位必须插在第一个数字占位符处：直接放整串最前面的话，
        // "合计:"0 显示 42 会变成 "4合计:2"。
        int total = (int)outChar.size();
        int extraN = total - nTmpl;
        bool extraDone = (extraN <= 0);
        int oc = 0;
        for (size_t ti = 0; ti < intToks.size(); ti++) {
            const auto& t = intToks[ti];
            if (t.k == NumFmt::Tok::Lit) { built += t.lit; continue; }
            if (t.k == NumFmt::Tok::Comma) continue;      // 千分位由 emitDigit 统一插入
            if (t.k == NumFmt::Tok::Zero || t.k == NumFmt::Tok::Hash || t.k == NumFmt::Tok::Ques) {
                if (!extraDone) {
                    for (int k = 0; k < extraN; k++) emitDigit(outChar[(size_t)k]);
                    extraDone = true;
                }
                size_t idx = (size_t)(extraN + oc);
                if (idx < outChar.size() && outChar[idx] != '\0') emitDigit(outChar[idx]);
                oc++;
                continue;
            }
        }
    }

    // ---- 小数部分：从左往右；尾部为 0 的 '#' 位不输出 ----
    std::string frac;
    if (!fracToks.empty()) {
        int nFrac = 0;
        for (const auto& t : fracToks)
            if (t.k == NumFmt::Tok::Zero || t.k == NumFmt::Tok::Hash || t.k == NumFmt::Tok::Ques) nFrac++;
        std::vector<char> fc((size_t)nFrac, '\0');
        int fi = 0;
        for (size_t i = 0; i < fracToks.size(); i++) {
            const auto& t = fracToks[i];
            if (t.k != NumFmt::Tok::Zero && t.k != NumFmt::Tok::Hash && t.k != NumFmt::Tok::Ques) continue;
            if (fi < (int)fracDigits.size()) fc[(size_t)fi] = fracDigits[(size_t)fi];
            else if (t.k == NumFmt::Tok::Zero) fc[(size_t)fi] = '0';
            else if (t.k == NumFmt::Tok::Ques) fc[(size_t)fi] = ' ';
            else fc[(size_t)fi] = '0';       // '#' 先占位，下面再决定去留
            fi++;
        }
        // 从右往左：'#' 位为 '0' 则不输出，直到遇到非零数字或强制位
        std::vector<bool> keep((size_t)nFrac, true);
        for (int k = nFrac - 1; k >= 0; k--) {
            const auto* tp = &fracToks[0];
            // 找到第 k 个占位符的类型
            int cnt = -1; NumFmt::Tok::K kind = NumFmt::Tok::Hash;
            for (size_t j = 0; j < fracToks.size(); j++) {
                if (fracToks[j].k == NumFmt::Tok::Zero || fracToks[j].k == NumFmt::Tok::Hash ||
                    fracToks[j].k == NumFmt::Tok::Ques) {
                    cnt++;
                    if (cnt == k) { kind = fracToks[j].k; break; }
                }
            }
            (void)tp;
            if (kind == NumFmt::Tok::Hash && fc[(size_t)k] == '0') keep[(size_t)k] = false;
            else break;
        }
        bool any = false;
        fi = 0;
        for (size_t i = 0; i < fracToks.size(); i++) {
            const auto& t = fracToks[i];
            if (t.k == NumFmt::Tok::Lit) { frac += t.lit; continue; }
            if (t.k != NumFmt::Tok::Zero && t.k != NumFmt::Tok::Hash && t.k != NumFmt::Tok::Ques) continue;
            if (keep[(size_t)fi] && fc[(size_t)fi] != '\0') { frac += fc[(size_t)fi]; any = true; }
            fi++;
        }
        if (any) built += '.' + frac;
    }
    return built;
}

// 把非负数值按 decimals 位四舍五入，拆成数字串 + 整数位数
std::string roundToDigits(double v, int decimals, int& intLen) {
    bool neg = v < 0;
    double a = std::fabs(v);
    // 先放大再取整，避免 pow 精度问题导致的 0.005 舍入错误
    double scale = std::pow(10.0, decimals);
    double scaled = a * scale;
    // 浮点误差补偿：0.5 边界用相对 epsilon
    double r = std::floor(scaled + 0.5 + 1e-9);
    if (std::fabs(scaled + 0.5 - r) < 1e-9 && std::fabs(scaled - std::floor(scaled) - 0.5) < 1e-9) {
        // 恰好 .5：Excel 用四舍五入（远离零）
        r = std::floor(scaled + 0.5);
    }
    unsigned long long whole = (unsigned long long)r;
    std::string s = std::to_string(whole);
    if (decimals == 0) { intLen = (int)s.size(); return (neg ? "-" : "") + s; }
    while ((int)s.size() < decimals + 1) s.insert(s.begin(), '0');
    intLen = (int)s.size() - decimals;
    return (neg ? "-" : "") + s;
}

} // namespace

std::string NumFmt::renderNumeric(const Section& s, double v, int& sciExp) {
    sciExp = 0;

    // 符号：段里若已写了 '-' 或用括号包住，就由段负责符号；否则负数自动前置 '-'。
    // 少了这条判断会得到 "--5.50"（段里的 - 加自动的 -）或 "-(1,234)"。
    bool hasMinusLit = false, hasParen = false;
    for (const Tok& t : s.toks) {
        if (t.k != Tok::Lit) continue;
        if (t.lit.find('-') != std::string::npos) hasMinusLit = true;
        if (t.lit.find('(') != std::string::npos || t.lit.find(')') != std::string::npos) hasParen = true;
    }
    bool autoMinus = (v < 0) && !hasMinusLit && !hasParen;

    // 百分比
    double val = std::fabs(v);
    int pctCount = 0;
    for (const Tok& t : s.toks) if (t.k == Tok::Pct) pctCount++;
    for (int i = 0; i < pctCount; i++) val *= 100.0;
    // 缩放：末尾逗号
    for (int i = 0; i < s.scaleCommas; i++) val /= 1000.0;

    // 科学计数？
    size_t sciAt = 0;
    bool hasSci = false;
    for (size_t i = 0; i < s.toks.size(); i++)
        if (s.toks[i].k == Tok::Sci) { hasSci = true; sciAt = i; break; }

    if (hasSci) {
        if (val == 0) { sciExp = 0; }
        else {
            double lg = std::log10(std::fabs(val));
            sciExp = (int)std::floor(lg);
            double mant = val / std::pow(10.0, sciExp);
            // 修正：尾数四舍五入到 10 时会进位（如 9.99 -> 10.0）
            // 这里按尾数模板位数取整后判断
            val = mant;
        }
    }

    // 拆出整数/小数模板
    std::vector<Tok> intToks, fracToks;
    bool inFrac = false;
    for (size_t i = 0; i < s.toks.size(); i++) {
        const Tok& t = s.toks[i];
        if (hasSci && i >= sciAt) break;
        if (t.k == Tok::Dot) { inFrac = true; continue; }
        if (t.k == Tok::Pct) continue;
        if (inFrac) fracToks.push_back(t); else intToks.push_back(t);
    }
    int decimals = 0;
    for (const Tok& t : fracToks)
        if (t.k == Tok::Zero || t.k == Tok::Hash || t.k == Tok::Ques) decimals++;

    int intLen = 0;
    std::string digits = roundToDigits(val, decimals, intLen);
    std::string out;
    if (autoMinus) out += '-';
    out += applyDigitTemplate(intToks, fracToks, s.hasComma, digits, intLen, decimals);
    // 百分号
    for (const Tok& t : s.toks) if (t.k == Tok::Pct) out += '%';

    // 科学计数的指数部分
    if (hasSci) {
        const Tok& st = s.toks[sciAt];
        out += st.lit[0];                       // E 或 e
        char sign = st.lit[1];                  // + 或 -
        int e = sciExp;
        std::string es = std::to_string(std::abs(e));
        // 指数模板位数：Sci 之后的 0/# 个数
        int expTmpl = 0;
        for (size_t i = sciAt + 1; i < s.toks.size(); i++)
            if (s.toks[i].k == Tok::Zero || s.toks[i].k == Tok::Hash) expTmpl++;
        while ((int)es.size() < expTmpl) es.insert(es.begin(), '0');
        if (e < 0) out += '-';
        else if (sign == '+') out += '+';
        else out += '-';                        // "E-" 时正数也写 '-'? Excel 行为：E- 只在负指数显示 -
        if (e >= 0 && sign == '-') { /* E- 且指数为正 -> 不写符号 */ }
        else if (e < 0) { /* 已写 - */ }
        if (!(e >= 0 && sign == '-')) out += es;
        else out += es;
    }
    return out;
}

// ---------------------------------------------------------------------------
// 日期时间渲染
// ---------------------------------------------------------------------------
namespace {

const char* kMonthNames[] = {"Jan","Feb","Mar","Apr","May","Jun",
                             "Jul","Aug","Sep","Oct","Nov","Dec"};
const char* kMonthFull[]  = {"January","February","March","April","May","June",
                             "July","August","September","October","November","December"};
const char* kDayNames[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* kDayFull[]    = {"Sunday","Monday","Tuesday","Wednesday",
                             "Thursday","Friday","Saturday"};

int weekdayFromSerial(double serial) {
    long long s = (long long)std::floor(serial);
    // Excel 口径：序列号 1（1900-01-01）是星期日。
    // 假闰日（序列号 60）不影响星期 —— 它本来就是"多出来的一天"，
    // 从 1 到 61 恰好 60 天，星期日 + 60 ≡ 星期四，与真实的 1900-03-01 一致。
    long long w = (s - 1) % 7;
    if (w < 0) w += 7;
    return (int)w;   // 0 = 星期日
}

std::string padNum(long long v, int width) {
    std::string s = std::to_string(v);
    while ((int)s.size() < width) s.insert(s.begin(), '0');
    return s;
}

} // namespace

std::string NumFmt::renderDateTime(const Section& s, double v) {
    YMD ymd = serialToYMD(v);
    double frac = v - std::floor(v);
    // 一天的总秒数，按四舍五入取到秒，避免 0.9999999 这类误差
    long long totalSec = (long long)std::floor(frac * 86400.0 + 0.5);
    int hh = (int)(totalSec / 3600);
    int mi = (int)((totalSec % 3600) / 60);
    int se = (int)(totalSec % 60);
    // 注意：totalSec 只来自小数部分，天然 < 24 小时；
    // 整天数由日期占位符负责，[h] 这类累计时长单独按 v*24 计算。

    std::string out;
    for (const Tok& t : s.toks) {
        switch (t.k) {
            case Tok::Year:
                if (t.count <= 2) out += padNum(ymd.y % 100, 2);
                else out += padNum(ymd.y, std::max(2, t.count));
                break;
            case Tok::Month: {
                int m = ymd.m;
                if (t.count <= 2) out += padNum(m, t.count);
                else if (t.count == 3) out += kMonthNames[m - 1];
                else out += kMonthFull[m - 1];
                break;
            }
            case Tok::Day: {
                if (t.count <= 2) out += padNum(ymd.d, t.count);
                else if (t.count == 3) out += kDayNames[weekdayFromSerial(v)];
                else out += kDayFull[weekdayFromSerial(v)];
                break;
            }
            case Tok::Hour:
                // 段里有 AM/PM 时按 12 小时制，且 0 点显示为 12
                if (s.hasAmPm) {
                    int h12 = hh % 12;
                    if (h12 == 0) h12 = 12;
                    out += padNum(h12, t.count);
                } else {
                    out += padNum(hh % 24, t.count);
                }
                break;
            case Tok::Minute:
                out += padNum(mi, t.count);
                break;
            case Tok::Second:
                out += padNum(se, t.count);
                break;
            case Tok::AmPm: {
                bool pm = (hh % 24) >= 12;
                std::string w = (t.lit == "AM/PM") ? (pm ? "PM" : "AM")
                                                   : (pm ? "pm" : "am");
                out += w;
                break;
            }
            case Tok::ElapsedH:
                out += std::to_string((long long)std::floor(v * 24.0));
                break;
            case Tok::ElapsedM:
                out += std::to_string((long long)std::floor(v * 1440.0));
                break;
            case Tok::ElapsedS:
                out += std::to_string((long long)std::floor(v * 86400.0));
                break;
            case Tok::Lit:
                out += t.lit;
                break;
            default:
                break;
        }
    }
    return out;
}

std::string NumFmt::renderSection(const Section& s, double v) {
    if (s.isDate || s.isTime) return renderDateTime(s, v);
    int sciExp = 0;
    return renderNumeric(s, v, sciExp);
}

std::string NumFmt::format(double v) const {
    const Section* s = pick(v);
    if (!s) return numToText(v);
    if (std::isnan(v)) return "#NUM!";
    // 文本段（只有 @）不该用来格式化数字
    bool onlyAt = true;
    for (const Tok& t : s->toks)
        if (t.k != Tok::At && t.k != Tok::Lit) { onlyAt = false; break; }
    if (onlyAt && !s->toks.empty()) {
        for (const Tok& t : s->toks)
            if (t.k == Tok::At) return numToText(v);
    }
    return renderSection(*s, v);
}

std::string NumFmt::formatText(const std::string& text) const {
    // 文本走最后一段（第 4 段），没有就原样返回
    for (size_t i = secs_.size(); i > 0; i--) {
        const Section& s = secs_[i - 1];
        for (const Tok& t : s.toks) {
            if (t.k == Tok::At) {
                std::string out;
                for (const Tok& u : s.toks) {
                    if (u.k == Tok::At) out += text;
                    else if (u.k == Tok::Lit) out += u.lit;
                    // Fill / Skip 需要列宽，这里忽略
                }
                return out;
            }
        }
    }
    return text;
}

std::string NumFmt::formatBool(bool b) const { return b ? "TRUE" : "FALSE"; }

std::string NumFmt::colorOf(double v) const {
    const Section* s = pick(v);
    return s ? s->color : std::string();
}

// ---------------------------------------------------------------------------
// 便捷接口
// ---------------------------------------------------------------------------
std::string formatValue(const Value& v, const NumFmt& fmt) {
    if (v.isError())  return errText(v.e);
    if (v.isEmpty())  return "";
    if (v.isBool())   return fmt.formatBool(v.b);
    if (v.isStr())    return fmt.formatText(v.s);
    if (v.isNum())    return fmt.format(v.n);
    if (v.isArray())  return valueToText(v);
    return valueToText(v);
}

std::string formatValueByCode(const Value& v, const std::string& code) {
    // 格式码解析不便宜，而同一列往往共用同一个码 —— 做个小缓存
    static std::map<std::string, NumFmt> cache;
    static std::map<std::string, std::string> errCache;
    if (code.empty()) return valueToText(v);
    auto it = cache.find(code);
    if (it == cache.end()) {
        NumFmt f; std::string err;
        if (!f.parse(code, err)) {
            errCache[code] = err;
            return valueToText(v);          // 解析失败就退回通用显示
        }
        it = cache.emplace(code, std::move(f)).first;
    }
    return formatValue(v, it->second);
}

} // namespace xl
