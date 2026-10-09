#include "refshift.hpp"
#include <cmath>
#include <sstream>

namespace xl {

// ---------------------------------------------------------------------------
// 引用文本化
// ---------------------------------------------------------------------------
static bool needsQuoting(const std::string& name) {
    if (name.empty()) return false;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '_' || c == '.') continue;
        if (c >= 0x80) continue;              // UTF-8 多字节（中文表名）可裸写
        return true;
    }
    return false;
}

std::string refToText(const RefPart& r) {
    std::ostringstream o;
    if (!r.sheet.empty()) {
        if (needsQuoting(r.sheet)) {
            o << '\'';
            for (char c : r.sheet) { o << c; if (c == '\'') o << '\''; }   // '' 转义
            o << '\'';
        } else {
            o << r.sheet;
        }
        o << '!';
    }
    if (r.colAbs) o << '$';
    o << colToName(r.col);
    if (r.rowAbs) o << '$';
    o << (r.row + 1);
    return o.str();
}

// ---------------------------------------------------------------------------
// 平移
// ---------------------------------------------------------------------------
ShiftResult shiftFormula(const std::string& formula, int dc, int dr,
                         int maxCol, int maxRow) {
    ShiftResult res;
    if (formula.empty()) { res.text = formula; return res; }

    Lexer lx(formula);
    std::string err;
    std::vector<Token> toks = lx.run(err);
    if (!err.empty()) { res.ok = false; res.error = err; res.text = formula; return res; }

    // 判断一个标识符是不是列名（A..XFD）
    auto colNameToIdx = [](const std::string& w, int& out) -> bool {
        if (w.empty() || w.size() > 3) return false;
        for (char c : w) if (!std::isalpha((unsigned char)c)) return false;
        int v = 0;
        for (char c : w) v = v * 26 + (std::toupper((unsigned char)c) - 'A' + 1);
        out = v - 1;
        return out >= 0 && out <= 16383;
    };

    std::string out;
    size_t cur = 0;
    for (size_t ti = 0; ti < toks.size(); ti++) {
        const Token& tk = toks[ti];

        // ---- 整列引用 B:C ----
        // 词法器把没有行号的 'B' 出成 Ident（parseCellRef 要求有行号），
        // 所以这里必须自己看 token 流：Ident 冒号 Ident 才是整列。
        // 少了这一步，填充 =SUM(B:B) 时列号就不会跟着走。
        if (tk.t == Tok::Ident && ti + 2 < toks.size() &&
            toks[ti + 1].t == Tok::Colon && toks[ti + 2].t == Tok::Ident) {
            int i0, i1;
            if (colNameToIdx(tk.text, i0) && colNameToIdx(toks[ti + 2].text, i1)) {
                out.append(formula, cur, tk.pos - cur);
                int n0 = i0 + dc, n1 = i1 + dc;
                if (n0 < 0 || n1 < 0 || n0 > maxCol || n1 > maxCol) {
                    out += "#REF!"; res.refErrors++;
                } else {
                    out += colToName(n0) + ":" + colToName(n1);
                }
                cur = toks[ti + 2].end;
                ti += 2;
                continue;
            }
        }

        // ---- 整行引用 1:1 ----
        // 同理：'1' 出的是 Num，只有看到 数字 冒号 数字 才能断定。
        if (tk.t == Tok::Num && ti + 2 < toks.size() &&
            toks[ti + 1].t == Tok::Colon && toks[ti + 2].t == Tok::Num) {
            double n0 = tk.num, n1 = toks[ti + 2].num;
            if (n0 == std::floor(n0) && n1 == std::floor(n1) && n0 >= 1 && n1 >= 1) {
                out.append(formula, cur, tk.pos - cur);
                int r0 = (int)n0 - 1 + dr, r1 = (int)n1 - 1 + dr;
                if (r0 < 0 || r1 < 0 || r0 > maxRow || r1 > maxRow) {
                    out += "#REF!"; res.refErrors++;
                } else {
                    out += std::to_string(r0 + 1) + ":" + std::to_string(r1 + 1);
                }
                cur = toks[ti + 2].end;
                ti += 2;
                continue;
            }
        }

        if (tk.t != Tok::Ref) continue;
        // 先把引用之前的原文照抄（含空白），再写重写后的引用
        out.append(formula, cur, tk.pos - cur);

        RefPart r = tk.ref;
        if (!r.colAbs) r.col += dc;
        if (!r.rowAbs) r.row += dr;

        bool bad = (r.col < 0 || r.col > maxCol || r.row < 0 || r.row > maxRow);
        if (bad) {
            out += "#REF!";
            res.refErrors++;
        } else {
            out += refToText(r);
        }
        cur = tk.end;
    }
    // 最后一个引用之后的尾巴
    if (cur < formula.size()) out.append(formula, cur, std::string::npos);
    res.text = out;
    return res;
}

ShiftResult shiftFormulaAt(const std::string& formula, int axis, int at, int delta,
                           int maxCol, int maxRow) {
    ShiftResult res;
    if (formula.empty() || delta == 0) { res.text = formula; return res; }

    Lexer lx(formula);
    std::string err;
    std::vector<Token> toks = lx.run(err);
    if (!err.empty()) { res.ok = false; res.error = err; res.text = formula; return res; }

    std::string out;
    size_t cur = 0;
    for (const Token& tk : toks) {
        if (tk.t != Tok::Ref) continue;
        out.append(formula, cur, tk.pos - cur);

        RefPart r = tk.ref;
        int pos = (axis == 0) ? r.col : r.row;
        bool abs = (axis == 0) ? r.colAbs : r.rowAbs;

        // 删除时，落在被删区间里的引用要变成 #REF!，而不是简单平移
        bool removed = false;
        if (delta < 0 && pos >= at && pos < at - delta) removed = true;
        // 插入时位置 >= at 才后移；删除时位置 >= at 才前移
        bool shouldMove = (delta > 0) ? (pos >= at) : (pos >= at);

        if (!abs && shouldMove && !removed) {
            if (axis == 0) r.col += delta; else r.row += delta;
        }

        bool bad = removed || r.col < 0 || r.col > maxCol || r.row < 0 || r.row > maxRow;
        if (bad) {
            out += "#REF!";
            res.refErrors++;
        } else {
            out += refToText(r);
        }
        cur = tk.end;
    }
    if (cur < formula.size()) out.append(formula, cur, std::string::npos);
    res.text = out;
    return res;
}

bool hasRelativeRef(const std::string& formula) {
    Lexer lx(formula);
    std::string err;
    std::vector<Token> toks = lx.run(err);
    if (!err.empty()) return false;
    for (const Token& tk : toks)
        if (tk.t == Tok::Ref && (!tk.ref.colAbs || !tk.ref.rowAbs)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// 填充
// ---------------------------------------------------------------------------
void fillRect(Sheet& sh, int c0, int r0, int c1, int r1, bool byColumn, FillResult& out) {
    if (c1 < c0 || r1 < r0) { out.error = "区域无效"; return; }
    int srcCols = c1 - c0 + 1, srcRows = r1 - r0 + 1;

    // 源快照：填充过程中会写单元格，边读边写会读到自己刚写的结果
    struct Src { bool has = false; bool hasFormula = false; std::string formula; Value value; };
    std::vector<std::vector<Src>> src((size_t)srcRows, std::vector<Src>((size_t)srcCols));
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            const Sheet::CellRec* rec = sh.find(c, r);
            Src& s = src[(size_t)(r - r0)][(size_t)(c - c0)];
            if (!rec) continue;
            s.has = true;
            s.hasFormula = rec->hasFormula;
            s.formula = rec->formula;
            s.value = rec->value;
        }

    // byColumn=true：源是首列，向右铺；否则源是首行，向下铺
    int span = byColumn ? (c1 - c0) : (r1 - r0);      // 源长度
    int steps = byColumn ? 0 : 0;                      // 占位，下面按方向算
    (void)span; (void)steps;

    if (byColumn) {
        // 首列 -> 向右铺满其余列
        for (int r = r0; r <= r1; r++) {
            const Src& s = src[(size_t)(r - r0)][0];
            for (int c = c0 + 1; c <= c1; c++) {
                int dc = c - c0, dr = 0;
                if (!s.has) { sh.eraseCell(c, r); continue; }
                if (s.hasFormula) {
                    ShiftResult sr = shiftFormula(s.formula, dc, dr);
                    if (!sr.ok) { out.error = sr.error; return; }
                    if (sr.refErrors) out.refErrors++;
                    std::string e = sh.setFormula(c, r, sr.text);
                    if (!e.empty()) { out.error = e; return; }
                } else {
                    sh.setValue(c, r, s.value);
                }
                out.written++;
            }
        }
    } else {
        // 首行 -> 向下铺满其余行
        for (int c = c0; c <= c1; c++) {
            const Src& s = src[0][(size_t)(c - c0)];
            for (int r = r0 + 1; r <= r1; r++) {
                int dc = 0, dr = r - r0;
                if (!s.has) { sh.eraseCell(c, r); continue; }
                if (s.hasFormula) {
                    ShiftResult sr = shiftFormula(s.formula, dc, dr);
                    if (!sr.ok) { out.error = sr.error; return; }
                    if (sr.refErrors) out.refErrors++;
                    std::string e = sh.setFormula(c, r, sr.text);
                    if (!e.empty()) { out.error = e; return; }
                } else {
                    sh.setValue(c, r, s.value);
                }
                out.written++;
            }
        }
    }
    sh.recalc();
}

FillResult fillRange(Sheet& sh, const FillRequest& req) {
    FillResult out;
    int srcCols = req.srcC1 - req.srcC0 + 1;
    int srcRows = req.srcR1 - req.srcR0 + 1;
    if (srcCols <= 0 || srcRows <= 0 || req.repeatCols <= 0 || req.repeatRows <= 0) {
        out.error = "区域尺寸无效"; return out;
    }

    // 源快照（同上，避免边读边写）
    struct Src { bool has = false; bool hasFormula = false; std::string formula; Value value; };
    std::vector<std::vector<Src>> src((size_t)srcRows, std::vector<Src>((size_t)srcCols));
    for (int r = 0; r < srcRows; r++)
        for (int c = 0; c < srcCols; c++) {
            const Sheet::CellRec* rec = sh.find(req.srcC0 + c, req.srcR0 + r);
            Src& s = src[(size_t)r][(size_t)c];
            if (!rec) continue;
            s.has = true; s.hasFormula = rec->hasFormula; s.formula = rec->formula; s.value = rec->value;
        }

    for (int r = 0; r < req.repeatRows; r++)
        for (int c = 0; c < req.repeatCols; c++) {
            const Src& s = src[(size_t)(r % srcRows)][(size_t)(c % srcCols)];
            int tc = req.dstC0 + c, tr = req.dstR0 + r;
            int dc = tc - req.srcC0, dr = tr - req.srcR0;
            if (!s.has) { sh.eraseCell(tc, tr); continue; }
            if (s.hasFormula) {
                ShiftResult sr = shiftFormula(s.formula, dc, dr);
                if (!sr.ok) { out.error = sr.error; return out; }
                if (sr.refErrors) out.refErrors++;
                std::string e = sh.setFormula(tc, tr, sr.text);
                if (!e.empty()) { out.error = e; return out; }
            } else {
                sh.setValue(tc, tr, s.value);
            }
            out.written++;
        }
    sh.recalc();
    return out;
}

} // namespace xl
