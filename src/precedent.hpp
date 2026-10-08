#pragma once
// 从 AST 里抽出"这条公式读了哪些东西"，供依赖图建边。
#include "depgraph.hpp"
#include "ast.hpp"
#include <string>
#include <vector>

namespace xl {

class Sheet;

struct PrecedentOut {
    std::vector<uint64_t> cells;         // 精确前驱（区域已展开）
    std::vector<std::string> names;      // 用到的定义名称
    std::vector<std::string> sheets;     // 引用到的其他表
    bool conservative = false;           // 有无法精确展开的依赖

    // 展开上限：单块区域与整条公式的累计前驱数
    static const long long kMaxExpand = 65536;
    static const long long kMaxTotal  = 200000;

    void addRect(int c0, int r0, int c1, int r1, const Sheet* sh);
    void addSheet(const std::string& s);
};

void collectPrecedents(const NodePtr& ast, const Sheet* sh, PrecedentOut& out);

} // namespace xl
