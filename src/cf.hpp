#pragma once
// ---------------------------------------------------------------------------
// 条件格式（Conditional Formatting）
//
// OOXML 里条件格式分两处存放，缺一不可：
//   sheetN.xml  <conditionalFormatting sqref="A1:A10"><cfRule .../></conditionalFormatting>
//   styles.xml  <dxfs><dxf>...</dxf></dxfs>         规则引用的样式本身
//
// cfRule 只存 dxfId（指向 styles.xml 的 dxfs），不像普通单元格那样用 xf。
// 这是最容易搞混的地方：条件格式的样式走 dxfs，普通样式走 cellXfs。
//
// 判定语义的关键点：**公式是相对于区域左上角书写的**。
// sqref="B2:B10" 里写公式 "5"，对 B5 求值时用的仍是 "5"；
// 但写 "A1>0" 时，对 B5 求值的语义等价于 "A4>0" —— 要按与左上角的
// 偏移量平移相对引用。复用 refshift 的同一套规则，与"填充"行为一致。
//
// 已实现的类型：
//   CellIs        —— 与常量/公式比较（8 种运算符）
//   Expression    —— 自定义公式，结果非零即生效
//   Top10         —— 前 N 项 / 后 N 项，支持按百分比
//   AboveAverage  —— 高于/低于区域平均值
//   Duplicate     —— 重复值
//   Unique        —— 唯一值
//
// 未实现：dataBar、colorScale、iconSet、containsText。
// 前三者属于"图形化"呈现，需要独立的绘制通道；containsText 在
// "文本包含"的语义上与 Duplicate 定位不同，暂不覆盖。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <map>
#include <array>
#include "value.hpp"

namespace xl {

class Sheet;

// 规则类型
enum class CfType {
    CellIs,          // 单元格值与给定值比较
    Expression,      // 自定义公式
    Top10,           // 前/后 N 项
    AboveAverage,    // 高于/低于平均
    Duplicate,       // 重复值
    Unique           // 唯一值
};

// CellIs 的运算符
enum class CfOperator {
    None,
    Between, NotBetween,
    Equal, NotEqual,
    GreaterThan, LessThan,
    GreaterThanOrEqual, LessThanOrEqual
};

// 条件命中时叠加的样式（dxf）
struct CfStyle {
    std::string fontColor;      // RRGGBB
    std::string fillColor;      // RRGGBB
    bool bold = false;
    bool italic = false;

    bool empty() const {
        return fontColor.empty() && fillColor.empty() && !bold && !italic;
    }
};

struct CfRule {
    CfType type = CfType::CellIs;
    CfOperator op = CfOperator::GreaterThan;

    // 判定用的公式（原文）。相对区域左上角书写，求值时按需平移。
    // CellIs 用 1 条（Between/NotBetween 用 2 条），Expression 用 1 条。
    std::vector<std::string> formulas;

    int priority = 1;           // 数字越小优先级越高（Excel 语义）
    bool stopIfTrue = false;    // 命中后不再看后续规则

    // Top10
    bool bottom = false;        // true = 取最小的 N 个
    bool percent = false;       // true = rank 按百分比解释
    int rank = 10;

    // AboveAverage
    bool above = true;          // false = 低于平均

    CfStyle style;
};

// 一个 <conditionalFormatting> 块：若干区域 + 若干规则
struct ConditionalFormat {
    // 每个区域 {c0, r0, c1, r1}，含首尾
    std::vector<std::array<int, 4>> rects;
    std::vector<CfRule> rules;
};

// ---------------------------------------------------------------------------
// 判定
// ---------------------------------------------------------------------------

// 计算某张表上所有条件格式的命中结果。
//
// 为什么是批量接口而不是"查单格"：Top10 / AboveAverage / Duplicate 这几类
// 规则要先看整个区域的取值才能判定（阈值、平均值、出现次数）。
// 逐个格子查会导致同一区域被重复统计 O(n) 次，大表上退化成 O(n²)。
// 批量接口让每块区域只统计一次。
//
// out：命中的格子 -> 叠加样式。未命中的格子不在 map 里。
void resolveCfStyles(Sheet& sh, const std::vector<ConditionalFormat>& cfs,
                     std::map<std::pair<int, int>, CfStyle>& out);

// 便捷：只查一个格子（内部仍会走批量逻辑，仅供少量调用）
bool cfStyleAt(Sheet& sh, const std::vector<ConditionalFormat>& cfs,
               int col, int row, CfStyle& out);

// ---------------------------------------------------------------------------
// 与 OOXML 的互转
// ---------------------------------------------------------------------------
const char* cfTypeToOoxml(CfType t);
const char* cfOperatorToOoxml(CfOperator op);
bool ooxmlToCfType(const std::string& s, CfType& out);
bool ooxmlToCfOperator(const std::string& s, CfOperator& out);

// dxfs 去重表：相同的 CfStyle 共用一个 dxfId
struct DxfSet {
    std::vector<CfStyle> items;
    int indexFor(const CfStyle& st);
    int indexForConst(const CfStyle& st) const;
};

} // namespace xl
