#include "depgraph.hpp"
#include <algorithm>
#include <cstdint>

namespace xl {

// ---------------------------------------------------------------------------
// 边的维护
// ---------------------------------------------------------------------------
void DepGraph::addEdge(uint64_t from, uint64_t to) {
    deps_[from].push_back(to);
    rdeps_[to].push_back(from);
}

void DepGraph::setDeps(uint64_t cell, const std::vector<uint64_t>& precedents,
                       const DepInfo& info) {
    // 先摘除旧边。不做这一步的话，改过的公式会留下指向旧依赖的悬空边，
    // 表现为"改了不相关的格子，这条公式却被无谓重算"—— 不影响正确性，
    // 但会让脏集合慢慢膨胀到全表，增量优化也就失效了。
    removeCell(cell);

    std::vector<uint64_t> ps = precedents;
    // 去重：=A1+A1*2 会产生重复边，传播时重复标脏只是浪费，但要花时间
    std::sort(ps.begin(), ps.end());
    ps.erase(std::unique(ps.begin(), ps.end()), ps.end());

    // 自依赖不建边：=A1+1 写在 A1 上就是循环引用，
    // 建边会让传播陷入自身，实际求值靠 visiting_ 检测，不需要这条边。
    ps.erase(std::remove(ps.begin(), ps.end(), cell), ps.end());

    for (uint64_t p : ps) addEdge(cell, p);
    deps_[cell] = std::move(ps);
    info_[cell] = info;
    dirty_.insert(cell);
    if (info.conservative) {
        bool dup = false;
        for (uint64_t c : conservative_) if (c == cell) { dup = true; break; }
        if (!dup) conservative_.push_back(cell);
    } else {
        conservative_.erase(std::remove(conservative_.begin(), conservative_.end(), cell),
                            conservative_.end());
    }
}

void DepGraph::removeCell(uint64_t cell) {
    auto it = deps_.find(cell);
    if (it != deps_.end()) {
        for (uint64_t p : it->second) {
            auto rit = rdeps_.find(p);
            if (rit == rdeps_.end()) continue;
            auto& v = rit->second;
            v.erase(std::remove(v.begin(), v.end(), cell), v.end());
            if (v.empty()) rdeps_.erase(rit);
        }
        deps_.erase(it);
    }
    info_.erase(cell);
    dirty_.erase(cell);
    conservative_.erase(std::remove(conservative_.begin(), conservative_.end(), cell),
                        conservative_.end());
    // 别人对它的依赖要保留吗？不该 —— 该格子没了，引用它的公式应得 #REF!/
    // 空值，而不是继续被这条边驱动。但 rdeps_[cell] 里存的是"谁依赖它"，
    // 那些公式自己的 deps_ 里还留着 cell，摘除它们的工作由那些公式
    // 重新 setDeps 时完成。这里不动 rdeps_[cell]，避免误删有效边。
}

void DepGraph::clear() {
    deps_.clear();
    rdeps_.clear();
    info_.clear();
    dirty_.clear();
    conservative_.clear();
}

// ---------------------------------------------------------------------------
// 脏标记传播
// ---------------------------------------------------------------------------
void DepGraph::propagate(const std::vector<uint64_t>& changed) {
    // 显式栈而不是递归：长依赖链（5000 层）递归会吃栈，
    // 而且这里只是集合遍历，没必要承担递归开销。
    std::vector<uint64_t> stack = changed;
    while (!stack.empty()) {
        uint64_t cur = stack.back();
        stack.pop_back();
        auto it = rdeps_.find(cur);
        if (it == rdeps_.end()) continue;
        for (uint64_t dep : it->second) {
            if (dirty_.insert(dep).second) stack.push_back(dep);
        }
    }
}

void DepGraph::markAllConservative() {
    for (uint64_t c : conservative_) dirty_.insert(c);
}

void DepGraph::markUsingName(const std::string& name) {
    for (auto& kv : info_) {
        bool hit = false;
        for (const std::string& n : kv.second.names)
            if (n == name) { hit = true; break; }
        if (hit) dirty_.insert(kv.first);
    }
}

void DepGraph::markUsingSheet(const std::string& sheet) {
    for (auto& kv : info_) {
        bool hit = false;
        for (const std::string& s : kv.second.sheets)
            if (s == sheet) { hit = true; break; }
        if (hit) dirty_.insert(kv.first);
    }
}

void DepGraph::markDirty(uint64_t cell) { dirty_.insert(cell); }

void DepGraph::markAllCells(const std::vector<uint64_t>& cells) {
    for (uint64_t c : cells) dirty_.insert(c);
}

size_t DepGraph::edgeCount() const {
    size_t n = 0;
    for (auto& kv : deps_) n += kv.second.size();
    return n;
}

} // namespace xl
