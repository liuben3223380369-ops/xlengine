#include "sheet.hpp"
#include "xlsx.hpp"
#include "numfmt.hpp"
#include <climits>
#include <algorithm>
#include <sstream>

namespace xl {

std::string colToName(int col) {
    std::string s;
    col++;                                   // 转成 1-based
    while (col > 0) {
        int rem = (col - 1) % 26;
        s.insert(s.begin(), (char)('A' + rem));
        col = (col - 1) / 26;
    }
    return s;
}

std::string addrToStr(int col, int row) {
    return colToName(col) + std::to_string(row + 1);
}

Sheet::CellRec* Sheet::find(int col, int row) {
    auto it = cells_.find({col, row});
    return it == cells_.end() ? nullptr : &it->second;
}
const Sheet::CellRec* Sheet::find(int col, int row) const {
    auto it = cells_.find({col, row});
    return it == cells_.end() ? nullptr : &it->second;
}

void Sheet::setValue(int col, int row, const Value& v) {
    // 写入超出原已用区域时，那些"按已用区域裁剪过"的整列/整行依赖会失效。
    // 保守做法：把这些公式全都标脏。增长不常发生，代价可以接受。
    int u0, v0, u1, v1;
    if (usedRange("", u0, v0, u1, v1) && (col > u1 || row > v1 || col < u0 || row < v0))
        deps_.markAllConservative();

    touchUsed(col, row);
    CellRec& c = cells_[{col, row}];
    c.hasFormula = false;
    c.formula.clear();
    c.ast = nullptr;
    c.value = v;
    c.evaluated = true;
    // 常量格本身不需要重算，但它可能是别人的前驱 —— 必须传播
    uint64_t k = cellKey(col, row);
    deps_.markDirty(k);
    deps_.propagate({k});
}

void Sheet::eraseCell(int col, int row) {
    uint64_t k = cellKey(col, row);
    // 摘除依赖边要放在传播之后：先传播让后继标脏，再删掉自己，
    // 顺序反了的话后继会漏标，读到残留的旧值。
    deps_.propagate({k});
    deps_.removeCell(k);
    cells_.erase({col, row});
    numFmts_.erase({col, row});
    styles_.erase({col, row});
    uValid_ = false;                     // 缩小必须重算，不能增量维护
    circular_.erase({col, row});
    visiting_.erase({col, row});
}

std::string Sheet::setFormula(int col, int row, const std::string& text) {
    ParseResult pr = parseFormula(text);
    if (!pr.ok()) return pr.error.empty() ? "公式解析失败" : pr.error;
    touchUsed(col, row);
    CellRec& c = cells_[{col, row}];
    c.hasFormula = true;
    c.formula = text;
    c.ast = pr.node;
    c.evaluated = false;
    c.value = Value::empty();

    // 建依赖图。放在写 cells_ 之后 —— 依赖提取要查已用区域，
    // 这张表自身也算一个格子。
    PrecedentOut out;
    collectPrecedents(pr.node, this, out);
    DepInfo info;
    info.conservative = out.conservative;
    info.names = out.names;
    info.sheets = out.sheets;
    deps_.setDeps(cellKey(col, row), out.cells, info);
    // 依赖变了，后继也要重算（移除的旧边可能让某些格子不再依赖它）
    deps_.propagate({cellKey(col, row)});
    return "";
}

bool Sheet::computeUsed(int& c0, int& r0, int& c1, int& r1) const {
    if (cells_.empty()) { uValid_ = false; return false; }
    c0 = INT_MAX; r0 = INT_MAX; c1 = INT_MIN; r1 = INT_MIN;
    for (auto& kv : cells_) {
        c0 = std::min(c0, kv.first.first);  c1 = std::max(c1, kv.first.first);
        r0 = std::min(r0, kv.first.second); r1 = std::max(r1, kv.first.second);
    }
    uC0_ = c0; uR0_ = r0; uC1_ = c1; uR1_ = r1;
    uValid_ = true;
    return true;
}

bool Sheet::usedRange(const std::string& sheet, int& c0, int& r0, int& c1, int& r1) const {
    // 跨表的已用区域要问工作簿；单表场景下 sheet 为空或就是自己。
    const Sheet* target = this;
    if (!sheet.empty() && sheet != name_ && owner_) {
        // 由 owner 转发（lookupCrossSheet 同款做法，避免 sheet.cpp 依赖 Workbook 布局）
        extern bool workbookUsedRange(void* wb, const std::string& name,
                                      int& c0, int& r0, int& c1, int& r1);
        if (!workbookUsedRange(owner_, sheet, c0, r0, c1, r1)) return false;
        return true;
    }
    if (target->cells_.empty()) { target->uValid_ = false; return false; }
    if (target->uValid_) {
        c0 = target->uC0_; r0 = target->uR0_; c1 = target->uC1_; r1 = target->uR1_;
        return true;
    }
    return target->computeUsed(c0, r0, c1, r1);
}

void Sheet::clear() {
    cells_.clear();
    numFmts_.clear();
    styles_.clear();
    circular_.clear();
    visiting_.clear();
    deps_.clear();
    uValid_ = false;
}

void Sheet::recalc() {
    for (auto& kv : cells_) {
        if (kv.second.hasFormula) { kv.second.evaluated = false; kv.second.value = Value::empty(); }
    }
    circular_.clear();
    std::vector<std::pair<int,int>> keys;
    for (auto& kv : cells_) if (kv.second.hasFormula) keys.push_back(kv.first);
    for (auto& k : keys) evaluate(k.first, k.second);
}

void Sheet::invalidateDirty() {
    for (uint64_t k : deps_.dirty()) {
        int col, row;
        cellUnkey(k, col, row);
        CellRec* c = find(col, row);
        if (c && c->hasFormula) { c->evaluated = false; c->value = Value::empty(); }
    }
}

void Sheet::recalcDirty() {
    // 保守型公式（依赖了超大区域 / lambda）每次都算 ——
    // 不为它们建百万条边，宁可每次重算这一条。
    deps_.markAllConservative();
    invalidateDirty();

    // 快照脏集合：求值过程中可能触发新的标脏（跨表回调、名称求值），
    // 直接迭代 deps_.dirty() 会在遍历中改容器。
    std::vector<uint64_t> keys(deps_.dirty().begin(), deps_.dirty().end());
    circular_.clear();
    for (uint64_t k : keys) {
        if (!deps_.isDirty(k)) continue;
        int col, row;
        cellUnkey(k, col, row);
        CellRec* c = find(col, row);
        if (!c || !c->hasFormula) { deps_.clearDirty(k); continue; }
        // 求值顺序无关：evaluated=false 的格子会在递归里被顺带算掉，
        // 这里跳过是被递归提前算好的那些
        if (c->evaluated) { deps_.clearDirty(k); continue; }
        evaluate(col, row);
        deps_.clearDirty(k);
    }
}

void Sheet::markAllDirty() {
    std::vector<uint64_t> all;
    for (auto& kv : cells_) all.push_back(cellKey(kv.first.first, kv.first.second));
    deps_.markAllCells(all);
}

bool Sheet::cellFormula(int col, int row, std::string& out) const {
    const CellRec* c = find(col, row);
    if (!c || !c->hasFormula) return false;
    out = c->formula;
    return true;
}

void Sheet::pushArgRefs(const std::vector<std::pair<int,int>>& refs) {
    argRefStack_.push_back(refs);
}
void Sheet::popArgRefs() {
    if (!argRefStack_.empty()) argRefStack_.pop_back();
}
bool Sheet::argRefAt(size_t idx, int& col, int& row) const {
    if (argRefStack_.empty()) return false;
    const auto& top = argRefStack_.back();
    if (idx >= top.size()) return false;
    if (top[idx].first < 0) return false;
    col = top[idx].first; row = top[idx].second;
    return true;
}

bool Sheet::cellAddr(int col, int row, std::string& out) const {
    if (col < 0 || row < 0) return false;
    out = (name_.empty() ? "" : name_ + "!") + addrToStr(col, row);
    return true;
}

Value Sheet::evaluate(int col, int row) {
    CellRec* c = find(col, row);
    if (!c) return Value::empty();
    if (!c->hasFormula) return c->value;
    if (c->evaluated) return c->value;

    std::pair<int,int> key{col, row};
    if (visiting_.count(key)) {           // 循环引用
        circular_.insert(key);
        return Value::error(Err::Calc);
    }
    visiting_.insert(key);
    int saveC = curCol_, saveR = curRow_;
    curCol_ = col; curRow_ = row;                  // ROW()/COLUMN() 依赖此上下文
    Value v = c->ast ? c->ast->eval(*this) : Value::empty();
    // 顶层结果是区域时（=A1:A5 这种动态数组写法）必须展开成真正的数组再缓存。
    // 惰性区域引用只在单次求值内部有效，存进 CellRec 会悬空语义。
    if (v.isRangeRef()) v = materializeRangeValue(v, *this);
    curCol_ = saveC; curRow_ = saveR;
    visiting_.erase(key);

    c->value = v;
    c->evaluated = true;
    return v;
}

Value Sheet::evalExprAt(const std::string& formula, int col, int row) {
    ParseResult pr = parseFormula(formula);
    if (!pr.ok()) return Value::error(Err::Value);
    int saveC = curCol_, saveR = curRow_;
    curCol_ = col; curRow_ = row;
    // 不进 visiting_：这条公式不是单元格内容，不构成依赖环的一环。
    // 若它引用了正在求值的格子，会走正常的 cell() -> evaluate() 路径，
    // 由那一层自己的 visiting_ 来判定循环。
    Value v = pr.node->eval(*this);
    curCol_ = saveC; curRow_ = saveR;
    if (v.isRangeRef()) v = materializeRangeValue(v, *this);
    return v;
}


// EvalCtx 入口：被引用的单元格按需递归求值
Value Sheet::cell(const std::string& sheet, int col, int row) {
    if (col < 0 || row < 0 || col > maxCol || row > maxRow) return Value::error(Err::Ref);
    if (!sheet.empty() && sheet != name_) {
        // 引用别的表：转发给工作簿。单表使用时 owner_ 为空 -> #REF!
        if (owner_) return lookupCrossSheet(owner_, sheet, col, row);
        return Value::error(Err::Ref);
    }
    return evaluate(col, row);
}

// 解析 "A1" / "$B$7" 形式的地址
bool Sheet::cellByAddr(const std::string& sheet, const std::string& addr, Value& out) {
    if (!sheet.empty() && sheet != name_ && owner_)
        return lookupCrossSheetByAddr(owner_, sheet, addr, out);
    std::string u;
    for (char c : addr) if (!std::isspace((unsigned char)c)) u.push_back(c);
    size_t i = 0;
    if (i < u.size() && u[i] == '$') i++;
    size_t cs = i;
    while (i < u.size() && std::isalpha((unsigned char)u[i])) i++;
    if (i == cs) return false;
    long col = 0;
    for (size_t k = cs; k < i; k++) col = col * 26 + (std::toupper((unsigned char)u[k]) - 'A' + 1);
    if (i < u.size() && u[i] == '$') i++;
    size_t rs = i;
    while (i < u.size() && std::isdigit((unsigned char)u[i])) i++;
    if (i == rs || i != u.size()) return false;
    int row = (int)(std::stol(u.substr(rs)) - 1);
    int c = (int)(col - 1);
    if (c < 0 || row < 0 || c > maxCol || row > maxRow) return false;
    out = cell(sheet, c, row);
    return true;
}

bool Sheet::definedName(const std::string& name, Value& out) {
    // 单表使用（owner_ 为空）时没有工作簿，也就没有定义名称
    if (!owner_) return false;
    return owner_->evalDefinedName(name, out, this);
}

bool Sheet::lookupName(const std::string& name, Value& out) const {
    for (size_t i = scope_.size(); i-- > 0;) {
        if (scope_[i].first == name) { out = scope_[i].second; return true; }
    }
    return false;
}
void Sheet::bindName(const std::string& name, const Value& v) {
    scope_.emplace_back(name, v);
}
void Sheet::unbindNames(size_t count) {
    if (count > scope_.size()) count = scope_.size();
    scope_.resize(scope_.size() - count);
}
Value Sheet::applyLambda(const LambdaDef& fn, const std::vector<Value>& args) {
    size_t n = fn.params.size();
    if (args.size() < n) return Value::error(Err::Value);
    size_t before = scope_.size();
    for (size_t i = 0; i < n; i++) scope_.emplace_back(fn.params[i], args[i]);
    // 递归 lambda：把自身名字也绑进去
    if (!fn.name.empty()) {
        auto self = std::make_shared<LambdaDef>(fn);
        scope_.emplace_back(fn.name, Value::lambdaV(self));
    }
    Value r = fn.body ? fn.body->eval(*this) : Value::empty();
    scope_.resize(before);
    return r;
}
void Sheet::defineName(const std::string& name, const Value& v) {
    // 命名定义放在栈底附近：先移除同名旧定义
    for (size_t i = scope_.size(); i-- > 0;)
        if (scope_[i].first == name) { scope_.erase(scope_.begin() + i); break; }
    scope_.insert(scope_.begin(), {name, v});
}

Value Sheet::valueAt(int col, int row) { return evaluate(col, row); }

void Sheet::setNumFmt(int col, int row, const std::string& code) {
    // 仍然确保 cells_ 里有记录（touchUsed + 建空记录）：
    // 只设格式、没设值的格子也要能被导出，否则存盘后格式会丢。
    // 省下来的是 CellRec 里那 184 字节，不是这条记录本身。
    touchUsed(col, row);
    cells_[{col, row}];
    if (code.empty()) numFmts_.erase({col, row});
    else              numFmts_[{col, row}] = code;
}

void Sheet::setStyle(int col, int row, const CellStyle& st) {
    touchUsed(col, row);
    cells_[{col, row}];
    if (st.empty())   styles_.erase({col, row});
    else              styles_[{col, row}] = st;
}

CellStyle Sheet::styleAt(int col, int row) const {
    auto it = styles_.find({col, row});
    return it == styles_.end() ? CellStyle() : it->second;
}

std::string Sheet::numFmtAt(int col, int row) const {
    auto it = numFmts_.find({col, row});
    return it == numFmts_.end() ? std::string() : it->second;
}

std::string Sheet::displayRaw(int col, int row) {
    return valueToText(evaluate(col, row));
}

std::string Sheet::display(int col, int row) {
    Value v = evaluate(col, row);
    // 先查格式码，再取单元格记录。
    // 无格式的格子（绝大多数）只需一次 map 查找就返回，
    // 不用再碰 cells_。
    auto f = numFmts_.find({col, row});
    if (f != numFmts_.end() && !f->second.empty())
        return formatValueByCode(v, f->second);
    return valueToText(v);
}

} // namespace xl
