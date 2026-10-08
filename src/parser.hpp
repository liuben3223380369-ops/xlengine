#pragma once
// ---------------------------------------------------------------------------
// 递归下降解析器。
// 优先级（低→高）：比较 < & < + - < * / < ^ < 一元- < % < 交集空格 < 区域: < 基本单元
// 关键歧义：函数参数里的逗号是"参数分隔"，普通括号里的逗号是"区域联合"。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include "lexer.hpp"
#include "ast.hpp"

namespace xl {

struct ParseResult {
    NodePtr node;
    std::string error;
    bool ok() const { return error.empty() && node != nullptr; }
};

ParseResult parseFormula(const std::string& text);   // 可带或不带前导 '='

} // namespace xl
