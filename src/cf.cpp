#include "cf.hpp"
#include "sheet.hpp"
#include "refshift.hpp"
#include "style.hpp"
#include <algorithm>
#include <cmath>

namespace xl {

// ---------------------------------------------------------------------------
// 类型转换
// ---------------------------------------------------------------------------
const char* cfTypeToOoxml(CfType t) {
    switch (t) {
        case CfType::CellIs:         return "cellIs";
        case CfType::Expression:     return "expression";
        case CfType::Top10:          return "top10";
        case CfType::AboveAverage:   return "aboveAverage";
        case CfType::Duplicate:      return "duplicateValues";
        case CfType::Unique:         return "uniqueValues";
    }
    return "cellIs";
}

const char* cfOperatorToOoxml(CfOperator op) {
    switch (op) {
        case CfOperator::Between:            return "between";
        case CfOperator::NotBetween:         return "notBetween";
        case CfOperator::Equal:              return "equal";
        case CfOperator::NotEqual:           return "notEqual";
        case CfOperator::GreaterThan:        return "greaterThan";
        case CfOperator::LessThan:           return "lessThan";
        case CfOperator::GreaterThanOrEqual: return "greaterThanOrEqual";
        case CfOperator::LessThanOrEqual:    return "lessThanOrEqual";
        case CfOperator::None:               return "";
    }
    return "";
}

bool ooxmlToCfType(const std::string& s, CfType& out) {
    if (s == "cellIs")         { out = CfType::CellIs; return true; }
    if (s == "expression")     { out = CfType::Expression; return true; }
    if (s == "top10")          { out = CfType::Top10; return true; }
    if (s == "aboveAverage")   { out = CfType::AboveAverage; return true; }
    if (s == "duplicateValues"){ out = CfType::Duplicate; return true; }
    if (s == "uniqueValues")   { out = CfType::Unique; return true; }
    return false;
}

bool ooxmlToCfOperator(const std::string& s, CfOperator& out) {
    if (s == "between")            { out = CfOperator::Between; return true; }
    if (s == "notBetween")         { out = CfOperator::NotBetween; return true; }
    if (s == "equal")              { out = CfOperator::Equal; return true; }
    if (s == "notEqual")           { out = CfOperator::NotEqual; return true; }
    if (s == "greaterThan")        { out = CfOperator::GreaterThan; return true; }
    if (s == "lessThan")           { out = CfOperator::LessThan; return true; }
    if (s == "greaterThanOrEqual") { out = CfOperator::GreaterThanOrEqual; return true; }
    if (s == "lessThanOrEqual")    { out = CfOperator::LessThanOrEqual; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// dxfs 去重
// ---------------------------------------------------------------------------
int DxfSet::indexFor(const CfStyle& st) {
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].fontColor == st.fontColor && items[i].fillColor == st.fillColor &&
            items[i].bold == st.bold && items[i].italic == st.italic)
            return (int)i;
    items.push_back(st);
    return (int)items.size() - 1;
}

int DxfSet::indexForConst(const CfStyle& st) const {
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].fontColor == st.fontColor && items[i].fillColor == st.fillColor &&
            items[i].bold == st.bold && items[i].italic == st.italic)
            return (int)i;
    return -1;
}

// ---------------------------------------------------------------------------
// 判定
// ---------------------------------------------------------------------------

// 在区域内按偏移量求值一条规则公式。
// 公式是相对区域左上角书写的，所以对第 k 个格子要先平移相对引用。
static Value evalRuleFormula(Sheet& sh, const std::string& f,
                             int anchorC, int anchorR, int col, int row) {
    if (f.empty()) return Value::error(Err::Value);
    int dc = col - anchorC, dr = row - anchorR;
    std::string text = f;
    if (dc != 0 || dr != 0) {
        ShiftResult sr = shiftFormula(f, dc, dr);
        // 平移失败（词法/语法问题）时退回原文：判定退化成"按左上角语义"，
        // 但至少不会因为一次平移失败就让整条规则对所有格子失效
        text = sr.error.empty() ? sr.text : f;
    }
    return sh.evalExprAt(text, col, row);
}

// 公式结果是否视为"真"。Excel 里非零数字为真，错误值不算命中。
static bool truthy(const Value& v) {
    if (v.isError()) return false;
    if (v.isNum())  return v.n != 0;
    if (v.isBool()) return v.b;
    if (v.isStr())  return false;      // 文本不参与真值判定
    return false;
}

// 收集区域内可参与数值比较的值（跳过空、文本、错误、逻辑值）
static void collectNumeric(Sheet& sh, const std::array<int, 4>& rc,
                           std::vector<std::pair<std::pair<int, int>, double>>& out) {
    for (int r = rc[1]; r <= rc[3]; r++)
        for (int c = rc[0]; c <= rc[2]; c++) {
            Value v = sh.valueAt(c, r);
            if (!v.isNum()) continue;
            double d = v.n;
            if (!std::isfinite(d)) continue;
            out.push_back({{c, r}, d});
        }
}

// 统计区域内每个值的出现次数（用于 Duplicate / Unique）。
// 按"显示文本"归一：数值 1 与文本 "1" 在 Excel 的条件格式里视为不同，
// 但数字 1.0 与 1 应视为相同 —— 用 valueToText 的结果做键正好符合这个语义。
static void countValues(Sheet& sh, const std::array<int, 4>& rc,
                        std::map<std::pair<int, int>, int>& counts) {
    std::map<std::string, int> tally;
    std::vector<std::pair<std::pair<int, int>, std::string>> cells;
    for (int r = rc[1]; r <= rc[3]; r++)
        for (int c = rc[0]; c <= rc[2]; c++) {
            Value v = sh.valueAt(c, r);
            if (v.isEmpty()) continue;             // 空格子不参与
            std::string k = valueToText(v);
            cells.push_back({{c, r}, k});
            tally[k]++;
        }
    for (auto& kv : cells) counts[kv.first] = tally[kv.second];
}

void resolveCfStyles(Sheet& sh, const std::vector<ConditionalFormat>& cfs,
                     std::map<std::pair<int, int>, CfStyle>& out) {
    out.clear();

    for (const ConditionalFormat& cf : cfs) {
        if (cf.rects.empty() || cf.rules.empty()) continue;

        // 规则按 priority 升序处理：数字越小优先级越高
        std::vector<const CfRule*> ordered;
        for (const CfRule& r : cf.rules) ordered.push_back(&r);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const CfRule* a, const CfRule* b) { return a->priority < b->priority; });

        for (const std::array<int, 4>& rc : cf.rects) {
            // 每块区域先把"需要全局统计"的量算好，避免逐格重复统计
            // Top10 的阈值 / AboveAverage 的均值 / Duplicate 的计数
            std::map<const CfRule*, double> thresholds;
            std::map<const CfRule*, double> averages;
            std::map<const CfRule*, std::map<std::pair<int, int>, int>> dupCounts;

            for (const CfRule* rule : ordered) {
                if (rule->type == CfType::Top10) {
                    std::vector<std::pair<std::pair<int, int>, double>> vals;
                    collectNumeric(sh, rc, vals);
                    if (vals.empty()) { thresholds[rule] = 0; continue; }
                    int n = rule->rank;
                    if (rule->percent) {
                        // rank 是百分比：取总数 * rank% 个（至少 1 个）
                        n = (int)std::llround((double)vals.size() * rule->rank / 100.0);
                        if (n < 1) n = 1;
                    }
                    if (n > (int)vals.size()) n = (int)vals.size();
                    if (n < 1) n = 1;
                    std::vector<double> sorted;
                    for (auto& kv : vals) sorted.push_back(kv.second);
                    std::sort(sorted.begin(), sorted.end());
                    // 第 n 大 / 第 n 小的边界值（含并列）
                    thresholds[rule] = rule->bottom ? sorted[(size_t)n - 1]
                                                    : sorted[sorted.size() - (size_t)n];
                } else if (rule->type == CfType::AboveAverage) {
                    std::vector<std::pair<std::pair<int, int>, double>> vals;
                    collectNumeric(sh, rc, vals);
                    if (vals.empty()) { averages[rule] = 0; continue; }
                    double sum = 0;
                    for (auto& kv : vals) sum += kv.second;
                    averages[rule] = sum / (double)vals.size();
                } else if (rule->type == CfType::Duplicate || rule->type == CfType::Unique) {
                    std::map<std::pair<int, int>, int> cnt;
                    countValues(sh, rc, cnt);
                    dupCounts[rule] = std::move(cnt);
                }
            }

            // 逐格判定
            for (int r = rc[1]; r <= rc[3]; r++) {
                for (int c = rc[0]; c <= rc[2]; c++) {
                    Value cellVal = sh.valueAt(c, r);
                    for (const CfRule* rule : ordered) {
                        bool hit = false;
                        switch (rule->type) {
                            case CfType::CellIs: {
                                if (rule->formulas.empty() || rule->op == CfOperator::None) break;
                                Value f1 = evalRuleFormula(sh, rule->formulas[0], rc[0], rc[1], c, r);
                                if (rule->op == CfOperator::Between || rule->op == CfOperator::NotBetween) {
                                    if (rule->formulas.size() < 2) break;
                                    Value f2 = evalRuleFormula(sh, rule->formulas[1], rc[0], rc[1], c, r);
                                    int c1 = 0, c2 = 0; Value e;
                                    if (!compareValues(cellVal, f1, c1, e)) break;
                                    if (!compareValues(cellVal, f2, c2, e)) break;
                                    bool inRange = (c1 >= 0 && c2 <= 0);
                                    hit = (rule->op == CfOperator::Between) ? inRange : !inRange;
                                } else {
                                    int cmp = 0; Value e;
                                    // 错误值不与任何东西相等，直接不命中
                                    if (!compareValues(cellVal, f1, cmp, e)) break;
                                    switch (rule->op) {
                                        case CfOperator::Equal:              hit = (cmp == 0); break;
                                        case CfOperator::NotEqual:           hit = (cmp != 0); break;
                                        case CfOperator::GreaterThan:        hit = (cmp > 0);  break;
                                        case CfOperator::LessThan:           hit = (cmp < 0);  break;
                                        case CfOperator::GreaterThanOrEqual: hit = (cmp >= 0); break;
                                        case CfOperator::LessThanOrEqual:    hit = (cmp <= 0); break;
                                        default: break;
                                    }
                                }
                                break;
                            }
                            case CfType::Expression: {
                                if (rule->formulas.empty()) break;
                                Value f = evalRuleFormula(sh, rule->formulas[0], rc[0], rc[1], c, r);
                                hit = truthy(f);
                                break;
                            }
                            case CfType::Top10: {
                                if (!cellVal.isNum()) break;
                                double d = cellVal.n;
                                if (!std::isfinite(d)) break;
                                auto it = thresholds.find(rule);
                                if (it == thresholds.end()) break;
                                hit = rule->bottom ? (d <= it->second) : (d >= it->second);
                                break;
                            }
                            case CfType::AboveAverage: {
                                if (!cellVal.isNum()) break;
                                double d = cellVal.n;
                                if (!std::isfinite(d)) break;
                                auto it = averages.find(rule);
                                if (it == averages.end()) break;
                                hit = rule->above ? (d > it->second) : (d < it->second);
                                break;
                            }
                            case CfType::Duplicate:
                            case CfType::Unique: {
                                auto it = dupCounts.find(rule);
                                if (it == dupCounts.end()) break;
                                auto cIt = it->second.find({c, r});
                                if (cIt == it->second.end()) break;      // 空格子不参与
                                int n = cIt->second;
                                hit = (rule->type == CfType::Duplicate) ? (n > 1) : (n == 1);
                                break;
                            }
                        }

                        if (!hit) continue;

                        // 命中：叠加样式。多条规则命中时后者覆盖前者的具体字段，
                        // 但已设过的字段不被"未设"覆盖（与 Excel 的叠加效果一致）
                        CfStyle& dst = out[{c, r}];
                        if (!rule->style.fontColor.empty()) dst.fontColor = rule->style.fontColor;
                        if (!rule->style.fillColor.empty()) dst.fillColor = rule->style.fillColor;
                        if (rule->style.bold)   dst.bold = true;
                        if (rule->style.italic) dst.italic = true;

                        if (rule->stopIfTrue) break;
                    }
                }
            }
        }
    }
}

bool cfStyleAt(Sheet& sh, const std::vector<ConditionalFormat>& cfs,
               int col, int row, CfStyle& out) {
    std::map<std::pair<int, int>, CfStyle> all;
    resolveCfStyles(sh, cfs, all);
    auto it = all.find({col, row});
    if (it == all.end()) return false;
    out = it->second;
    return true;
}

} // namespace xl
