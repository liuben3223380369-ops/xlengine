#include "precedent.hpp"
#include "sheet.hpp"

namespace xl {
namespace {

// 递归遍历 AST，收集这条公式读了哪些东西。
//
// 三种产出：
//   1. 精确单元格（CellRef）-> 直接建依赖边
//   2. 区域（Range / Union / Intersect）-> 裁剪后展开成一批单元格
//   3. 定义名称（Name）-> 记名字，名称变更时整条公式重算
//
// 区域裁剪：整列引用（B:B）语义上是 1048576 行，不可能全展开。
// 与 eval 保持一致，裁剪到工作表已用区域 —— 否则依赖边数量会爆炸，
// 而且 eval 本来也只读到已用区域，多建的边是纯浪费。
void walk(const NodePtr& n, PrecedentOut& out, const Sheet* sh) {
    if (!n) return;
    switch (n->kind) {
        case NodeKind::CellRef: {
            int col = n->col, row = n->row;
            if (n->wholeCol || n->wholeRow) {
                int c0, r0, c1, r1;
                if (sh && sh->usedRange(n->sheet, c0, r0, c1, r1)) {
                    if (n->wholeCol) { r0 = 0; r1 = sh->maxRow > 4095 ? 4095 : sh->maxRow; }
                    if (n->wholeRow) { c0 = 0; c1 = sh->maxCol > 255 ? 255 : sh->maxCol; }
                    out.addRect(col, row, n->wholeCol ? col : c1,
                                n->wholeRow ? row : r1, sh);
                }
                // 已用区域未知时退化为保守：标记为不可精确展开
                else out.conservative = true;
                return;
            }
            if (n->sheet.empty()) out.cells.push_back(cellKey(col, row));
            else out.addSheet(n->sheet);
            return;
        }
        case NodeKind::Range: {
            // 两端都是 CellRef；整列/整行由各自的 CellRef 分支处理
            if (n->a && n->b) {
                int c0 = n->a->col, r0 = n->a->row, c1 = n->b->col, r1 = n->b->row;
                if (n->a->wholeCol || n->a->wholeRow || n->b->wholeCol || n->b->wholeRow) {
                    int u0, v0, u1, v1;
                    if (sh && sh->usedRange(n->a->sheet, u0, v0, u1, v1)) {
                        if (n->a->wholeCol) { r0 = v0; r1 = v1; }
                        if (n->a->wholeRow) { c0 = u0; c1 = u1; }
                        out.addRect(c0, r0, c1, r1, sh);
                    } else out.conservative = true;
                    return;
                }
                if (n->a->sheet.empty()) {
                    if (std::min(c0, c1) == std::max(c0, c1) && std::min(r0, r1) == std::max(r0, r1))
                        out.cells.push_back(cellKey(c0, r0));
                    else
                        out.addRect(std::min(c0, c1), std::min(r0, r1),
                                    std::max(c0, c1), std::max(r0, r1), sh);
                } else out.addSheet(n->a->sheet);
                return;
            }
            walk(n->a, out, sh); walk(n->b, out, sh);
            return;
        }
        case NodeKind::Name:
            out.names.push_back(n->text);
            return;
        case NodeKind::Union:
        case NodeKind::Intersect:
            walk(n->a, out, sh); walk(n->b, out, sh);
            return;
        case NodeKind::Unary:
            walk(n->a, out, sh);
            return;
        case NodeKind::Binary:
            walk(n->a, out, sh); walk(n->b, out, sh);
            return;
        case NodeKind::Call:
            for (auto& k : n->kids) walk(k, out, sh);
            return;
        case NodeKind::ArrayLit:
            for (auto& r : n->kids)
                for (auto& e : r->kids) walk(e, out, sh);
            return;
        case NodeKind::LambdaDefNode:
            // lambda 体里的引用要在"调用时"才确定位置，
            // 保守标记即可 —— 这类公式本来就少见
            out.conservative = true;
            return;
        default:
            return;
    }
}

} // namespace

void PrecedentOut::addRect(int c0, int r0, int c1, int r1, const Sheet* sh) {
    long long rows = (long long)r1 - r0 + 1;
    long long cols = (long long)c1 - c0 + 1;
    if (rows <= 0 || cols <= 0) return;
    long long total = rows * cols;
    // 展开上限。超过就保守重算 —— 与其为一条公式建百万条边拖垮整张表，
    // 不如让它每次都重算（重算一条的代价远小于边表膨胀的代价）。
    if (total > kMaxExpand || cells.size() + (size_t)total > kMaxTotal) {
        conservative = true;
        return;
    }
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            cells.push_back(cellKey(c, r));
    (void)sh;
}

void PrecedentOut::addSheet(const std::string& s) {
    bool dup = false;
    for (const std::string& x : sheets) if (x == s) { dup = true; break; }
    if (!dup) sheets.push_back(s);
}

void collectPrecedents(const NodePtr& ast, const Sheet* sh, PrecedentOut& out) {
    walk(ast, out, sh);
}

} // namespace xl
