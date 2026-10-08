#pragma once
// ---------------------------------------------------------------------------
// 公式词法分析：把 "=SUM(A1:B2, 1)" 变成 token 流。
// 这里要处理的 Excel 特有东西：绝对引用 $A$1、跨表引用 Sheet1!A1、
// 带空格的表名 'My Sheet'!A1、错误字面量 #DIV/0!、交集空格、隐式交集 @。
// ---------------------------------------------------------------------------
#include <string>
#include <limits>
#include <stdexcept>
#include <vector>
#include <cctype>
#include "value.hpp"

namespace xl {

enum class Tok {
    End, Num, Str, Ident, Ref, Func,
    Plus, Minus, Mul, Div, Pow, Concat,
    Eq, Ne, Lt, Gt, Le, Ge,
    LParen, RParen, Comma, Colon, Percent, Intersect, At,
    LBrace, RBrace, Semicolon, ErrLit, Bool
};

inline const char* tokName(Tok t) {
    switch (t) {
        case Tok::End: return "End";       case Tok::Num: return "Num";
        case Tok::Str: return "Str";       case Tok::Ident: return "Ident";
        case Tok::Ref: return "Ref";       case Tok::Func: return "Func";
        case Tok::Plus: return "+";        case Tok::Minus: return "-";
        case Tok::Mul: return "*";         case Tok::Div: return "/";
        case Tok::Pow: return "^";         case Tok::Concat: return "&";
        case Tok::Eq: return "=";          case Tok::Ne: return "<>";
        case Tok::Lt: return "<";          case Tok::Gt: return ">";
        case Tok::Le: return "<=";         case Tok::Ge: return ">=";
        case Tok::LParen: return "(";      case Tok::RParen: return ")";
        case Tok::Comma: return ",";       case Tok::Colon: return ":";
        case Tok::Percent: return "%";     case Tok::Intersect: return "<space>";
        case Tok::At: return "@";          case Tok::LBrace: return "{";
        case Tok::RBrace: return "}";      case Tok::Semicolon: return ";";
        case Tok::ErrLit: return "ErrLit"; case Tok::Bool: return "Bool";
    }
    return "?";
}

struct RefPart {
    std::string sheet;
    int col = 0;      // 0-based
    int row = 0;      // 0-based
    bool colAbs = false, rowAbs = false;
};

struct Token {
    Tok t = Tok::End;
    double num = 0.0;
    std::string text;                 // 标识符 / 字符串 / 表名
    RefPart ref;                      // Ref token 的解析结果
    bool boolVal = false;
    Err errVal = Err::Null;
    size_t pos = 0;
    size_t end = 0;                   // 结束位置（开区间）。目前只有 Ref 会填，
                                      // 引用重写需要它 —— 见 refshift.hpp
};

// 解析 "A1" / "$A$1" / "AB12" 形式
inline bool parseCellRef(const std::string& s, size_t& i, RefPart& r) {
    size_t start = i;
    if (i < s.size() && s[i] == '$') { r.colAbs = true; i++; }
    size_t colStart = i;
    while (i < s.size() && std::isalpha((unsigned char)s[i])) i++;
    if (i == colStart) { i = start; return false; }
    std::string cs = s.substr(colStart, i - colStart);
    if (i < s.size() && s[i] == '$') { r.rowAbs = true; i++; }
    size_t rowStart = i;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) i++;
    if (i == rowStart) { i = start; return false; }
    // 不能紧跟字母（避免把函数名 "A1B" 当引用）
    if (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.' ||
                         s[i] == '!' || s[i] == '(')) {
        // 形如 Sheet2!A1 -> 表名；形如 DAYS360( / LOG10( -> 函数名而非单元格引用
        i = start; return false;
    }
    long col = 0;
    for (char c : cs) col = col * 26 + (std::toupper((unsigned char)c) - 'A' + 1);
    r.col = (int)(col - 1);
    r.row = (int)(std::stol(s.substr(rowStart, i - rowStart)) - 1);
    return true;
}

class Lexer {
public:
    explicit Lexer(std::string src) : s_(std::move(src)) {}

    std::vector<Token> run(std::string& errMsg) {
        std::vector<Token> out;
        while (true) {
            size_t before = i_;
            Token tk = next(errMsg);
            if (!errMsg.empty()) return {};
            // 双保险：任何 token 都必须让位置前进。
            // 词法器分支众多，漏掉一处就会变成死循环 + 内存爆炸，
            // 这里统一兜底 —— 停滞即视为无法识别的字符。
            if (i_ == before && tk.t != Tok::End) {
                errMsg = std::string("无法识别的字符 '") + (before < s_.size() ? s_[before] : '?') + "'";
                return {};
            }
            out.push_back(tk);
            if (tk.t == Tok::End) break;
        }
        return out;
    }

private:
    std::string s_;
    size_t i_ = 0;
    bool prevOperand_ = false;      // 上一个 token 是否可作操作数结尾

    static bool isOperandStart(char c) {
        return std::isalnum((unsigned char)c) || c == '$' || c == '(' ||
               c == '-' || c == '"' || c == '\'' || c == '{';
    }

    // 空白既可能是分隔符，也可能是交集运算符（A1:C1 A1:B1）
    // 返回 true 表示这里的空格应解释为交集运算符
    bool handleWhitespace() {
        while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) {
            size_t j = i_;
            while (j < s_.size() && std::isspace((unsigned char)s_[j])) j++;
            if (prevOperand_ && j < s_.size() && isOperandStart(s_[j])) { i_ = j; return true; }
            i_ = j;
        }
        return false;
    }

    bool matchErrLiteral(Err& e) {
        if (i_ >= s_.size() || s_[i_] != '#') return false;
        static const struct { const char* text; Err e; } tbl[] = {
            {"#DIV/0!", Err::Div0}, {"#VALUE!", Err::Value}, {"#REF!", Err::Ref},
            {"#NAME?", Err::Name},  {"#NUM!", Err::Num},     {"#N/A", Err::NA},
            {"#NULL!", Err::Null},  {"#SPILL!", Err::Spill}, {"#CALC!", Err::Calc},
        };
        std::string rest = s_.substr(i_, 10);
        for (auto& x : tbl) {
            size_t len = std::string(x.text).size();
            if (rest.size() >= len && rest.compare(0, len, x.text) == 0) {
                // #N/A 后面不能再跟字母
                e = x.e; i_ += len; return true;
            }
        }
        return false;
    }

    Token next(std::string& errMsg) {
        if (handleWhitespace()) {
            Token it; it.t = Tok::Intersect; it.pos = i_; it.end = i_;
            prevOperand_ = false;
            return it;
        }
        Token tk = nextRaw(errMsg);
        // end 统一在这里填：逐个 return 去补迟早会漏掉某一个分支，
        // 而引用重写依赖它（漏了就会把原文重复抄一遍）。
        tk.end = (tk.end != 0) ? tk.end : i_;
        prevOperand_ = (tk.t == Tok::Num || tk.t == Tok::Str || tk.t == Tok::Ref ||
                        tk.t == Tok::Bool || tk.t == Tok::ErrLit || tk.t == Tok::RParen);
        return tk;
    }

    Token nextRaw(std::string& errMsg) {
        Token tk;
        tk.pos = i_;
        if (i_ >= s_.size()) { tk.t = Tok::End; return tk; }
        char c = s_[i_];

        Err e;
        if (matchErrLiteral(e)) { tk.t = Tok::ErrLit; tk.errVal = e; return tk; }

        // 带引号的表名 'Sheet 1'!A1
        if (c == '\'') {
            size_t j = i_ + 1;
            std::string name;
            while (j < s_.size()) {
                if (s_[j] == '\'') {
                    if (j + 1 < s_.size() && s_[j+1] == '\'') { name.push_back('\''); j += 2; continue; }
                    break;
                }
                name.push_back(s_[j++]);
            }
            if (j < s_.size() && s_[j] == '\'' && j + 1 < s_.size() && s_[j+1] == '!') {
                i_ = j + 2;
                RefPart r; r.sheet = name;
                if (!parseCellRef(s_, i_, r)) { errMsg = "无效单元格引用"; return tk; }
                tk.t = Tok::Ref; tk.ref = r; tk.end = i_; return tk;
            }
            // 不是表名，退回普通处理
        }

        // 数字
        if (std::isdigit((unsigned char)c) || (c == '.' && i_ + 1 < s_.size() && std::isdigit((unsigned char)s_[i_+1]))) {
            size_t start = i_;
            bool seenDot = false, seenE = false;
            while (i_ < s_.size()) {
                char d = s_[i_];
                if (std::isdigit((unsigned char)d)) { i_++; continue; }
                if (d == '.' && !seenDot && !seenE) { seenDot = true; i_++; continue; }
                if ((d == 'e' || d == 'E') && !seenE) {
                    seenE = true; i_++;
                    if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) i_++;
                    continue;
                }
                break;
            }
            tk.t = Tok::Num;
            // std::stod 在指数越界时会抛 std::out_of_range ——
            // "1E400"（上溢）和 "1E-320"（下溢）都会。不捕获的话，
            // 单元格里输入 =1E400 直接 terminate，整个进程崩掉。
            //
            // 按 Excel 的语义饱和：上溢 -> 无穷（后续运算自然得到 #NUM!），
            // 下溢 -> 0。这样既不会崩，也和用户预期一致。
            {
                const std::string lit = s_.substr(start, i_ - start);
                try {
                    tk.num = std::stod(lit);
                } catch (const std::out_of_range&) {
                    bool negative = !lit.empty() && lit[0] == '-';
                    std::string low;
                    for (char ch : lit) low += (char)std::tolower((unsigned char)ch);
                    bool underflow = low.find("e-") != std::string::npos;
                    tk.num = underflow ? 0.0
                           : (negative ? -std::numeric_limits<double>::infinity()
                                       :  std::numeric_limits<double>::infinity());
                } catch (...) {
                    tk.num = std::numeric_limits<double>::quiet_NaN();
                }
            }
            return tk;
        }

        // 字符串 "..."（"" 表示转义引号）
        if (c == '"') {
            i_++;
            std::string v;
            while (i_ < s_.size()) {
                if (s_[i_] == '"') {
                    if (i_ + 1 < s_.size() && s_[i_+1] == '"') { v.push_back('"'); i_ += 2; continue; }
                    i_++; break;
                }
                v.push_back(s_[i_++]);
            }
            tk.t = Tok::Str; tk.text = v; return tk;
        }

        // 标识符 / 函数 / 引用 / 布尔
        // 起始字符允许 UTF-8 多字节：中文表名（如 数据!A1）在 Excel 里合法且无需引号
        if (std::isalpha((unsigned char)c) || c == '_' || c == '$' || c == '\\'
            || (unsigned char)c >= 0x80) {
            size_t start = i_;
            // 先试：不带表名的单元格引用
            RefPart r0;
            size_t save = i_;
            if (parseCellRef(s_, i_, r0)) {
                // 检查是否是 "A1:B2" 的左半 —— 交给 parser，这里就出 Ref
                tk.t = Tok::Ref; tk.ref = r0; tk.end = i_; return tk;
            }
            i_ = save;
            // 试：表名!引用
            while (i_ < s_.size() && (std::isalnum((unsigned char)s_[i_]) || s_[i_] == '_'
                                      || s_[i_] == '.' || (unsigned char)s_[i_] >= 0x80)) i_++;
            std::string word = s_.substr(start, i_ - start);
            if (i_ < s_.size() && s_[i_] == '!') {
                i_++;
                RefPart r; r.sheet = word;
                if (parseCellRef(s_, i_, r)) { tk.t = Tok::Ref; tk.ref = r; tk.end = i_; return tk; }
                errMsg = "表名后不是有效单元格引用";
                return tk;
            }
            // 布尔字面量
            std::string up = word;
            std::transform(up.begin(), up.end(), up.begin(), ::toupper);
            if (up == "TRUE" || up == "FALSE") {
                tk.t = Tok::Bool; tk.boolVal = (up == "TRUE"); return tk;
            }
            // 位置必须前进。
            //
            // '$' 和 '\\' 能进入这个分支（起始字符允许它们），但后续
            // while 只吃字母/数字/下划线/点 —— 于是 word 为空、i_ 原地不动。
            // run() 的循环会不停拿到同一个 Ident("")，token 向量无限增长，
            // 内存耗尽后被 OOM 杀掉（公式 "%%$" 就能触发）。
            //
            // 裸 '$' / '\\' 不是合法引用，这里把它当成一个单字符标识符，
            // 让解析器给出正常错误，而不是让进程消失。
            if (i_ == start) { i_++; word = std::string(1, c); }

            // 函数名（后面紧跟左括号）
            if (i_ < s_.size() && s_[i_] == '(') { tk.t = Tok::Func; tk.text = word; return tk; }
            tk.t = Tok::Ident; tk.text = word; return tk;
        }

        // 运算符
        auto two = [&](char a, Tok ta, char b, Tok tb) -> bool {
            if (c == a) {
                if (i_ + 1 < s_.size() && s_[i_+1] == b) { i_ += 2; tk.t = tb; }
                else { i_++; tk.t = ta; }
                return true;
            }
            return false;
        };
        if (two('<', Tok::Lt, '>', Tok::Ne)) return tk;   // <> 要先于 <=
        if (two('<', Tok::Lt, '=', Tok::Le)) return tk;
        if (two('>', Tok::Gt, '=', Tok::Ge)) return tk;
        switch (c) {
            case '+': i_++; tk.t = Tok::Plus;  return tk;
            case '-': i_++; tk.t = Tok::Minus; return tk;
            case '*': i_++; tk.t = Tok::Mul;   return tk;
            case '/': i_++; tk.t = Tok::Div;   return tk;
            case '^': i_++; tk.t = Tok::Pow;   return tk;
            case '&': i_++; tk.t = Tok::Concat;return tk;
            case '=': i_++; tk.t = Tok::Eq;    return tk;
            case '(': i_++; tk.t = Tok::LParen;return tk;
            case ')': i_++; tk.t = Tok::RParen;return tk;
            case ',': i_++; tk.t = Tok::Comma; return tk;
            case ':': i_++; tk.t = Tok::Colon; return tk;
            case '%': i_++; tk.t = Tok::Percent; return tk;
            case '@': i_++; tk.t = Tok::At;    return tk;
            case '{': i_++; tk.t = Tok::LBrace;return tk;
            case '}': i_++; tk.t = Tok::RBrace;return tk;
            case ';': i_++; tk.t = Tok::Semicolon; return tk;
        }
        errMsg = std::string("无法识别的字符 '") + c + "'";
        return tk;
    }
};

} // namespace xl
