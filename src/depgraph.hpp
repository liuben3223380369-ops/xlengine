#pragma once
// ---------------------------------------------------------------------------
// 增量重算：依赖图 + 脏标记传播
//
// 现状的问题（实测）：改 1 个格子 -> recalc() 把**全部**公式标脏并重算。
// 20,000 条公式时单次 20.3 ms，规模再大就直接不可用。
//
// 正确做法：
//   1. 每条公式建一张"前驱"表（它读了哪些格子）
//   2. 反向前驱表得到"后继"表（哪些格子依赖它）
//   3. 改动时只把受影响的格子标脏，recalcDirty() 只算脏的
//
// 关键性质：**求值顺序不重要**。
// 脏格子的 evaluated 都是 false，求值 A 时会递归触发 B 的求值。
// 所以不需要拓扑排序，脏集合按任意顺序遍历都正确。
//
// 循环引用不破坏这个性质：visiting_ 依然会在递归里检测到环并给 #CALC!。
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace xl {

// 单元格键：行在高 32 位、列在低 32 位。
// 不用 (row<<14)|col 那种紧凑编码 —— 测试里会出现越界坐标（如 99999），
// 紧凑编码会串位导致错误的依赖边，而这种错误极难复现。
inline uint64_t cellKey(int col, int row) {
    return ((uint64_t)(uint32_t)row << 32) | (uint32_t)col;
}
inline void cellUnkey(uint64_t k, int& col, int& row) {
    row = (int)(int32_t)(k >> 32);
    col = (int)(int32_t)(k & 0xFFFFFFFFu);
}

// 一条公式的依赖信息
struct DepInfo {
    // 是否依赖了"无法精确展开"的东西（超大区域、整列引用等）。
    // 这类公式保守起见每次都重算 —— 重算一条的代价远小于算错。
    bool conservative = false;
    // 是否引用了定义名称。名称变更时这些公式必须重算，
    // 否则会拿到旧值（这是增量重算最容易出错的地方）。
    std::vector<std::string> names;
    // 引用的其他表名。跨表时由工作簿负责把脏标记传过来。
    std::vector<std::string> sheets;
};

class DepGraph {
public:
    // 替换某格的依赖（旧依赖会被完整摘除，不留悬空边）
    void setDeps(uint64_t cell, const std::vector<uint64_t>& precedents, const DepInfo& info);
    void removeCell(uint64_t cell);
    void clear();

    // 从一组格子出发，把传递依赖到它们的所有格子标脏
    void propagate(const std::vector<uint64_t>& changed);

    // 按条件标脏
    void markAllConservative();
    void markUsingName(const std::string& name);
    void markUsingSheet(const std::string& sheet);
    void markDirty(uint64_t cell);
    void markAllCells(const std::vector<uint64_t>& cells);

    bool isDirty(uint64_t cell) const { return dirty_.count(cell) > 0; }
    void clearDirty(uint64_t cell) { dirty_.erase(cell); }
    const std::unordered_set<uint64_t>& dirty() const { return dirty_; }
    void clearDirtyAll() { dirty_.clear(); }

    size_t edgeCount() const;
    size_t cellCount() const { return deps_.size(); }
    size_t conservativeCount() const { return conservative_.size(); }

private:
    void addEdge(uint64_t from, uint64_t to);

    // cell -> 它依赖的格子（用于摘除旧边）
    std::unordered_map<uint64_t, std::vector<uint64_t>> deps_;
    // cell -> 依赖它的格子（用于传播）
    std::unordered_map<uint64_t, std::vector<uint64_t>> rdeps_;
    std::unordered_map<uint64_t, DepInfo> info_;
    std::unordered_set<uint64_t> dirty_;
    // 保守型格子单独维护一份。
    // markAllConservative() 原本要遍历全部 info_ —— 每次重算都扫一遍全表，
    // 固定开销 O(公式数)，把增量优化的好处全吃掉了。
    std::vector<uint64_t> conservative_;
};

} // namespace xl
