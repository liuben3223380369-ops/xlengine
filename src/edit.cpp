// ---------------------------------------------------------------------------
// 结构性编辑：插入 / 删除行与列
// ---------------------------------------------------------------------------
// 这是"编辑一张表"最后一块拼图。填充柄只改内容，插入行列要挪动整张表：
//
//   1. 单元格内容（值 + 公式）整体下移 / 右移
//   2. 公式里的引用按阈值重写 —— 不是整体平移
//   3. 数字格式码、视觉样式跟着走
//   4. 合并区域、条件格式、数据验证区域、批注、图表锚点都要跟着动
//
// 第 2 条最容易写错。在第 3 行插入 1 行时：
//     =A1+1     不动（在插入点之前）
//     =A3+1     -> =A4+1（被挤下去了）
//     =$A$3+1   不动（绝对引用不随插入移动）
//
// 第 4 条最容易漏。漏掉的表现是"数据对了，但合并区/批注停在原地"，
// 而且不报错 —— 只有打开文件才能发现。
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "edit.hpp"
#include "sheet.hpp"
#include "xlsx.hpp"
#include "refshift.hpp"
#include "cf.hpp"
#include "dv.hpp"
#include "note.hpp"
#include "image.hpp"
#include "chart.hpp"

namespace xl {

namespace {

// 把 map 里 key 的某一维 >= at 的项整体后移 delta 位。
// 必须先收集再重建：边遍历边改 map 会读到刚写进去的项，导致连锁移动。
template <typename T>
void shiftMapDim(std::map<std::pair<int, int>, T>& m, int axis, int at, int delta) {
    std::vector<std::pair<std::pair<int, int>, T>> moved;
    std::vector<std::pair<int, int>> erased;
    for (auto& kv : m) {
        int pos = axis == 0 ? kv.first.first : kv.first.second;
        if (delta < 0 && pos >= at && pos < at - delta) {
            erased.push_back(kv.first);              // 落在被删区间里
        } else if (pos >= at) {
            auto key = kv.first;
            if (axis == 0) key.first += delta; else key.second += delta;
            moved.push_back({key, std::move(kv.second)});
            erased.push_back(kv.first);
        }
    }
    for (auto& k : erased) m.erase(k);
    for (auto& kv : moved) m[kv.first] = std::move(kv.second);
}

// 区域的四个边界按阈值平移。区域整体被删除时返回 false。
bool shiftRect(int& c0, int& r0, int& c1, int& r1, int axis, int at, int delta) {
    int* lo = axis == 0 ? &c0 : &r0;
    int* hi = axis == 0 ? &c1 : &r1;
    if (delta < 0 && *lo >= at && *hi < at - delta) return false;   // 整个区域被删掉
    if (*lo >= at) *lo += delta;
    if (*hi >= at) *hi += delta;
    if (*hi < *lo) *hi = *lo;
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Sheet 层：内容 + 引用
// ---------------------------------------------------------------------------
void Sheet::insertRows(int at, int count) { shiftContent(1, at, count); }
void Sheet::insertCols(int at, int count) { shiftContent(0, at, count); }
void Sheet::deleteRows(int at, int count) { shiftContent(1, at, -count); }
void Sheet::deleteCols(int at, int count) { shiftContent(0, at, -count); }

void Sheet::shiftContent(int axis, int at, int delta) {
    if (delta == 0 || at < 0) return;
    if (axis == 1 && at > maxRow) return;
    if (axis == 0 && at > maxCol) return;

    // 1) 单元格：先整体挪位置
    shiftMapDim(cells_, axis, at, delta);
    shiftMapDim(numFmts_, axis, at, delta);
    shiftMapDim(styles_, axis, at, delta);

    // 2) 公式引用重写。
    //
    //    这里遍历的是**已经移动过**的格子，但重写规则要按**插入发生的位置 at**
    //    判断，而不是按格子当前位置 —— 引用指向的是"逻辑位置"，
    //    插入点之前的就是之前，之后的就是之后，跟格子本身挪到哪儿无关。
    //
    //    每个公式只重写一次。若按格子新位置再判一次，
    //    已经后移的格子会被二次平移。
    std::vector<std::pair<std::pair<int, int>, std::string>> rewritten;
    for (auto& kv : cells_) {
        if (!kv.second.hasFormula || kv.second.formula.empty()) continue;
        ShiftResult sr = shiftFormulaAt(kv.second.formula, axis, at, delta, maxCol, maxRow);
        if (sr.ok && sr.text != kv.second.formula)
            rewritten.push_back({kv.first, sr.text});
    }
    for (auto& rw : rewritten) {
        CellRec* c = find(rw.first.first, rw.first.second);
        if (!c) continue;
        setFormula(rw.first.first, rw.first.second, rw.second);
    }

    // 3) 缓存全废：位置全变了，依赖图里的坐标也已失效
    markAllDirty();
}

// ---------------------------------------------------------------------------
// Workbook 层：合并 / 条件格式 / 数据验证 / 批注 / 图表 / 布局
// ---------------------------------------------------------------------------
void Workbook::insertRows(int sheetIdx, int at, int count) {
    structuralEdit(sheetIdx, 1, at, count);
}
void Workbook::insertCols(int sheetIdx, int at, int count) {
    structuralEdit(sheetIdx, 0, at, count);
}
void Workbook::deleteRows(int sheetIdx, int at, int count) {
    structuralEdit(sheetIdx, 1, at, -count);
}
void Workbook::deleteCols(int sheetIdx, int at, int count) {
    structuralEdit(sheetIdx, 0, at, -count);
}

void Workbook::structuralEdit(int sheetIdx, int axis, int at, int delta) {
    if (sheetIdx < 0 || sheetIdx >= (int)sheets_.size() || delta == 0 || at < 0) return;

    // 内容
    if (sheets_[(size_t)sheetIdx].sheet)
        sheets_[(size_t)sheetIdx].sheet->shiftContent(axis, at, delta);

    // 合并区域
    if (sheetIdx < (int)merges_.size()) {
        std::vector<std::array<int, 4>> kept;
        for (auto m : merges_[(size_t)sheetIdx]) {
            int c0 = m[0], r0 = m[1], c1 = m[2], r1 = m[3];
            if (shiftRect(c0, r0, c1, r1, axis, at, delta)) kept.push_back({c0, r0, c1, r1});
        }
        merges_[(size_t)sheetIdx] = kept;
    }

    // 条件格式：区域是 rects 数组（一个 cf 可以覆盖多块）
    if (sheetIdx < (int)cfs_.size()) {
        for (auto& cf : cfs_[(size_t)sheetIdx]) {
            std::vector<std::array<int, 4>> kept;
            for (auto m : cf.rects) {
                int c0 = m[0], r0 = m[1], c1 = m[2], r1 = m[3];
                if (shiftRect(c0, r0, c1, r1, axis, at, delta)) kept.push_back({c0, r0, c1, r1});
            }
            cf.rects = kept;
        }
        // 区域全被删掉的规则就没有意义了
        auto& v = cfs_[(size_t)sheetIdx];
        v.erase(std::remove_if(v.begin(), v.end(),
                               [](const ConditionalFormat& c) { return c.rects.empty(); }),
                v.end());
    }

    // 数据验证：同样是 rects 数组
    if (sheetIdx < (int)dvs_.size()) {
        for (auto& dv : dvs_[(size_t)sheetIdx]) {
            std::vector<std::array<int, 4>> kept;
            for (auto m : dv.rects) {
                int c0 = m[0], r0 = m[1], c1 = m[2], r1 = m[3];
                if (shiftRect(c0, r0, c1, r1, axis, at, delta)) kept.push_back({c0, r0, c1, r1});
            }
            dv.rects = kept;
        }
        auto& v = dvs_[(size_t)sheetIdx];
        v.erase(std::remove_if(v.begin(), v.end(),
                               [](const DataValidation& d) { return d.rects.empty(); }),
                v.end());
    }

    // 行高 / 列宽：这一维要跟着挪
    if (sheetIdx < (int)layouts_.size()) {
        SheetLayout& lay = layouts_[(size_t)sheetIdx];
        std::map<int, double>& dim = axis == 0 ? lay.colWidths : lay.rowHeights;
        std::vector<std::pair<int, double>> moved;
        std::vector<int> erased;
        for (auto& kv : dim) {
            if (delta < 0 && kv.first >= at && kv.first < at - delta) {
                erased.push_back(kv.first);
            } else if (kv.first >= at) {
                moved.push_back({kv.first + delta, kv.second});
                erased.push_back(kv.first);
            }
        }
        for (int k : erased) dim.erase(k);
        for (auto& kv : moved) dim[kv.first] = kv.second;
    }

    // 批注：按格存，跟着走
    if (sheetIdx < (int)notes_.size()) {
        std::vector<CellNote> kept;
        for (auto& n : notes_[(size_t)sheetIdx]) {
            CellNote c = n;
            int pos = axis == 0 ? c.col : c.row;
            if (delta < 0 && pos >= at && pos < at - delta) continue;
            if (pos >= at) { if (axis == 0) c.col += delta; else c.row += delta; }
            kept.push_back(c);
        }
        notes_[(size_t)sheetIdx] = kept;
    }

    // 图表锚点（Chart 自带 anchor）
    if (sheetIdx < (int)charts_.size()) {
        for (auto& ch : charts_[(size_t)sheetIdx]) {
            shiftRect(ch.anchor.fromCol, ch.anchor.fromRow,
                      ch.anchor.toCol, ch.anchor.toRow, axis, at, delta);
        }
    }

    // 图片锚点
    if (sheetIdx < (int)images_.size()) {
        for (auto& im : images_[(size_t)sheetIdx]) {
            shiftRect(im.fromCol, im.fromRow, im.toCol, im.toRow, axis, at, delta);
        }
    }
}

// 取消合并：命中指定区域的合并块全部移除
bool Workbook::unmerge(int sheetIdx, int c0, int r0, int c1, int r1) {
    if (sheetIdx < 0 || sheetIdx >= (int)merges_.size()) return false;
    auto& v = merges_[(size_t)sheetIdx];
    size_t before = v.size();
    std::vector<std::array<int, 4>> kept;
    for (auto& m : v) {
        // 与该区域有交集就移除
        bool overlap = !(m[2] < c0 || m[0] > c1 || m[3] < r0 || m[1] > r1);
        if (!overlap) kept.push_back(m);
    }
    v = kept;
    return v.size() != before;
}

}  // namespace xl
