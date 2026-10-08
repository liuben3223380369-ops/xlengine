#include "parser.hpp"
#include <algorithm>

namespace xl {

class Parser {
public:
    Parser(std::vector<Token> toks) : t_(std::move(toks)) {}

    ParseResult parse() {
        // 允许前导 '='
        if (peek().t == Tok::Eq) adv();
        NodePtr n = parseExpr(false);
        if (!n) {
            if (depthFailed_)
                return {nullptr, "公式嵌套过深（超过 " + std::to_string(kMaxDepth) + " 层）"};
            return {nullptr, "解析失败"};
        }
        if (peek().t != Tok::End) return {nullptr, std::string("多余内容: ") + tokName(peek().t)};
        return {n, ""};
    }

private:
    std::vector<Token> t_;
    size_t p_ = 0;

    // 递归深度保护。
    //
    // 这是递归下降解析器的固有风险：每层嵌套消耗一次 C++ 调用栈，
    // 而嵌套层数完全由输入决定（"((((...1...))))" 有几千层就有几千帧）。
    // 没有限制的话，5000 层嵌套会直接栈溢出段错误 —— 不是报错，是进程崩溃。
    //
    // 实测：每层括号约消耗 8 个栈帧（经过 Expr/Compare/Concat/Add/Mul/Unary/Power/Primary
    // 整整一条链），1000 层约 8000 帧仍安全，5000 层就爆了。
    // 这里设 400 层上限，留足余量，超出则给出可读的错误信息。
    // 4000 层"递归调用"约等于 400~500 层括号嵌套。
    // 实测崩溃点在 5000 层括号（约 4 万次递归帧），4000 留了 10 倍余量；
    // 而 Excel 自身的函数嵌套上限是 64 层，这里远超实际需求。
    static const int kMaxDepth = 4000;
    int depth_ = 0;
    bool depthFailed_ = false;

    struct DepthGuard {
        Parser* p;
        bool ok;
        explicit DepthGuard(Parser* owner) : p(owner) {
            ok = (++p->depth_ <= kMaxDepth);
            if (!ok) p->depthFailed_ = true;
        }
        ~DepthGuard() { --p->depth_; }
        explicit operator bool() const { return ok; }
    };

    const Token& peek(size_t k = 0) const {
        static const Token end{};   // 默认即 Tok::End
        return (p_ + k < t_.size()) ? t_[p_ + k] : end;
    }
    void adv() { if (p_ < t_.size()) p_++; }
    bool at(Tok x) const { return peek().t == x; }
    bool eat(Tok x) { if (at(x)) { adv(); return true; } return false; }

    NodePtr parseExpr(bool allowUnion) {
        DepthGuard dg(this);
        if (!dg) return nullptr;
        NodePtr left = parseCompare();
        if (!left) return nullptr;
        if (allowUnion) {
            while (at(Tok::Comma)) {
                adv();
                NodePtr right = parseCompare();
                if (!right) return nullptr;
                left = makeUnion(left, right);
            }
        }
        return left;
    }

    NodePtr parseCompare() {
        DepthGuard dg(this);
        if (!dg) return nullptr;
        NodePtr left = parseConcat();
        if (!left) return nullptr;
        while (at(Tok::Eq) || at(Tok::Ne) || at(Tok::Lt) || at(Tok::Gt) || at(Tok::Le) || at(Tok::Ge)) {
            Tok op = peek().t; adv();
            NodePtr right = parseConcat();
            if (!right) return nullptr;
            BinOp b;
            switch (op) {
                case Tok::Eq: b = BinOp::Eq; break;
                case Tok::Ne: b = BinOp::Ne; break;
                case Tok::Lt: b = BinOp::Lt; break;
                case Tok::Gt: b = BinOp::Gt; break;
                case Tok::Le: b = BinOp::Le; break;
                default:      b = BinOp::Ge; break;
            }
            left = makeBinary(b, left, right);
        }
        return left;
    }

    NodePtr parseConcat() {
        DepthGuard dg(this);
        if (!dg) return nullptr;
        NodePtr left = parseAdd();
        if (!left) return nullptr;
        while (at(Tok::Concat)) {
            adv();
            NodePtr right = parseAdd();
            if (!right) return nullptr;
            left = makeBinary(BinOp::Concat, left, right);
        }
        return left;
    }

    NodePtr parseAdd() {
        DepthGuard dg(this);
        if (!dg) return nullptr;
        NodePtr left = parseMul();
        if (!left) return nullptr;
        while (at(Tok::Plus) || at(Tok::Minus)) {
            BinOp b = at(Tok::Plus) ? BinOp::Add : BinOp::Sub;
            adv();
            NodePtr right = parseMul();
            if (!right) return nullptr;
            left = makeBinary(b, left, right);
        }
        return left;
    }

    NodePtr parseMul() {
        DepthGuard dg(this);
        if (!dg) return nullptr;
        NodePtr left = parsePow();
        if (!left) return nullptr;
        while (at(Tok::Mul) || at(Tok::Div)) {
            BinOp b = at(Tok::Mul) ? BinOp::Mul : BinOp::Div;
            adv();
            NodePtr right = parsePow();
            if (!right) return nullptr;
            left = makeBinary(b, left, right);
        }
        return left;
    }

    // Excel 的 ^ 是**左结合**，而且是唯一一个与常规数学约定相反的结合性：
    //
    //   2^3^2    = (2^3)^2 = 64     ← 不是 2^(3^2)=512
    //   2^2^3    = (2^2)^3 = 64
    //
    // 原先按右结合递归 parsePow()，得到 512 和 256。
    //
    // 同时必须保留另一条 Excel 规则：**一元负号优先于 ^**
    //
    //   -2^2 = (-2)^2 = 4
    //
    // 所以左右两侧都用 parseUnary()（它能吃掉前导负号），再用循环实现左结合。
    // 这样三条规则都能同时成立（实测对照 LibreOffice）：
    //   2^-3^2  = (2^(-3))^2  = 0.015625   一元负号只绑到紧邻的 3
    //   -2^3^2  = ((-2)^3)^2  = 64         负号先绑到 2
    //   2^3^-2  = (2^3)^(-2)  = 0.015625
    NodePtr parsePow() {
        DepthGuard dg(this);
        if (!dg) return nullptr;
        NodePtr left = parseUnary();
        if (!left) return nullptr;
        while (at(Tok::Pow)) {
            adv();
            NodePtr right = parseUnary();     // 不是 parsePow —— 那样会变右结合
            if (!right) return nullptr;
            left = makeBinary(BinOp::Pow, left, right);
        }
        return left;
    }

    NodePtr parseUnary() {
        DepthGuard dg(this);
        if (!dg) return nullptr;
        if (at(Tok::Minus)) { adv(); NodePtr a = parseUnary(); return a ? makeUnary(UnOp::Neg, a) : nullptr; }
        if (at(Tok::Plus))  { adv(); NodePtr a = parseUnary(); return a ? makeUnary(UnOp::Plus, a) : nullptr; }
        if (at(Tok::At))    { adv(); return parseUnary(); }   // 隐式交集，简化为透传
        return parseIntersect();
    }

    NodePtr parseIntersect() {
        NodePtr left = parsePostfix();
        if (!left) return nullptr;
        while (at(Tok::Intersect)) {
            adv();
            NodePtr right = parsePostfix();
            if (!right) return nullptr;
            left = makeIntersect(left, right);
        }
        return left;
    }

    NodePtr parsePostfix() {
        NodePtr left = parseRange();
        if (!left) return nullptr;
        while (at(Tok::Percent)) { adv(); left = makeUnary(UnOp::Percent, left); }
        return left;
    }

    // "B" 是不是一个合法的列名（A..XFD）
    static bool isColName(const std::string& w) {
        if (w.empty() || w.size() > 3) return false;
        for (char c : w) if (!std::isalpha((unsigned char)c)) return false;
        int v = 0;
        for (char c : w) v = v * 26 + (std::toupper((unsigned char)c) - 'A' + 1);
        return v >= 1 && v <= 16384;
    }

    // 整列引用 B:C / 整行引用 1:1。
    // 不能靠词法器：'B' 没有行号，parseCellRef 会拒绝它，出的是 Ident；
    // 而 '1' 出的是 Num。只有拿到 token 流、看到中间的冒号才能断定是区域。
    NodePtr parseWholeRange() {
        size_t p = p_;
        auto at_ = [&](size_t k, Tok t) { return p + k < t_.size() && t_[p + k].t == t; };
        if (at_(0, Tok::Ident) && at_(1, Tok::Colon) && at_(2, Tok::Ident)) {
            const std::string& w0 = t_[p].text;
            const std::string& w1 = t_[p + 2].text;
            if (!isColName(w0) || !isColName(w1)) return nullptr;
            auto colOf = [](const std::string& w) {
                int v = 0;
                for (char c : w) v = v * 26 + (std::toupper((unsigned char)c) - 'A' + 1);
                return v - 1;
            };
            p_ += 3;
            NodePtr a = makeRef("", colOf(w0), 0, false, false); a->wholeCol = true;
            NodePtr b = makeRef("", colOf(w1), 0, false, false); b->wholeCol = true;
            return makeRange(a, b);
        }
        if (at_(0, Tok::Num) && at_(1, Tok::Colon) && at_(2, Tok::Num)) {
            double n0 = t_[p].num, n1 = t_[p + 2].num;
            if (n0 != std::floor(n0) || n1 != std::floor(n1)) return nullptr;
            if (n0 < 1 || n1 < 1 || n0 > 1048576 || n1 > 1048576) return nullptr;
            p_ += 3;
            NodePtr a = makeRef("", 0, (int)n0 - 1, false, false); a->wholeRow = true;
            NodePtr b = makeRef("", 0, (int)n1 - 1, false, false); b->wholeRow = true;
            return makeRange(a, b);
        }
        return nullptr;
    }

    NodePtr parseRange() {
        NodePtr whole = parseWholeRange();
        if (whole) return whole;
        NodePtr left = parsePrimary();
        if (!left) return nullptr;
        while (at(Tok::Colon)) {
            adv();
            NodePtr right = parsePrimary();
            if (!right) return nullptr;
            if (left->kind != NodeKind::CellRef || right->kind != NodeKind::CellRef)
                return nullptr;                       // 区域两端必须是引用
            left = makeRange(left, right);
        }
        return left;
    }

    NodePtr parsePrimary() {
        Token tk = peek();
        switch (tk.t) {
            case Tok::Num:  adv(); return makeNum(tk.num);
            case Tok::Str:  adv(); return makeStr(tk.text);
            case Tok::Bool:
                adv();
                // Excel 里 TRUE / FALSE 既能当字面量，也能写成 TRUE() / FALSE()。
                // 词法器把它们统一成 Bool token，所以这里要补上"后跟空括号"的形式 ——
                // 否则函数表里列了它们、却写 TRUE() 会报"多余内容: ("。
                if (peek().t == Tok::LParen) {
                    adv();
                    // 不接受参数：TRUE(1) 是错的，返回 nullptr 让上层报解析失败
                    if (peek().t != Tok::RParen) return nullptr;
                    adv();
                }
                return makeBool(tk.boolVal);
            case Tok::ErrLit: adv(); return makeErr(tk.errVal);
            case Tok::Ref: {
                adv();
                NodePtr n = makeRef(tk.ref.sheet, tk.ref.col, tk.ref.row, tk.ref.colAbs, tk.ref.rowAbs);
                return n;
            }
            case Tok::Func: {
                adv();
                std::string name = tk.text;
                if (!eat(Tok::LParen)) return nullptr;
                std::vector<NodePtr> args;
                if (!at(Tok::RParen)) {
                    while (true) {
                        // Excel 允许空参数占位：`SORT(A1:A3,,-1)`、
                        // `VLOOKUP(x,A:B,2,)`、`OFFSET(A1,1,,3,2)`。
                        // 原先遇到 `,,` 会因 parseExpr 返回空而整式解析失败，
                        // 于是这些写法全部不可用。
                        if (at(Tok::Comma) || at(Tok::RParen)) {
                            auto n = std::make_shared<Node>();
                            n->kind = NodeKind::Missing;
                            args.push_back(n);
                        } else {
                            NodePtr a = parseExpr(false);  // 参数内逗号=参数分隔
                            if (!a) return nullptr;
                            args.push_back(a);
                        }
                        if (eat(Tok::Comma)) continue;
                        break;
                    }
                }
                if (!eat(Tok::RParen)) return nullptr;
                return makeCall(name, args);
            }
            case Tok::LParen: {
                adv();
                NodePtr inner = parseExpr(true);           // 括号内逗号=区域联合
                if (!inner) return nullptr;
                if (!eat(Tok::RParen)) return nullptr;
                return inner;
            }
            case Tok::LBrace: {
                adv();
                std::vector<NodePtr> rows;
                while (true) {
                    std::vector<NodePtr> cells;
                    while (true) {
                        NodePtr c = parseExpr(false);
                        if (!c) return nullptr;
                        cells.push_back(c);
                        if (eat(Tok::Comma)) continue;
                        break;
                    }
                    rows.push_back(makeArray(cells));
                    if (eat(Tok::Semicolon)) continue;
                    break;
                }
                if (!eat(Tok::RBrace)) return nullptr;
                return makeArray(rows);
            }
            case Tok::Ident:
                // 裸标识符：先在 LET/LAMBDA 作用域里查；查不到时由求值阶段给 #NAME?
                adv();
                return makeName(tk.text);
            default:
                return nullptr;
        }
    }
};

ParseResult parseFormula(const std::string& text) {
    std::string lexErr;
    Lexer lx(text);
    auto toks = lx.run(lexErr);
    if (!lexErr.empty()) return {nullptr, lexErr};
    Parser ps(std::move(toks));
    return ps.parse();
}

} // namespace xl
