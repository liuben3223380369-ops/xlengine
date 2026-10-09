#pragma once
// ---------------------------------------------------------------------------
// 引用重写：把公式里的相对引用按偏移量平移
//
// 这是"填充"功能的地基 —— 把 A1 的 =B1*2 往下拉到 A2，期望变成 =B2*2。
// 同一套逻辑也用于展开 xlsx 里的共享公式（t="shared"）。
//
// 规则（与 Excel 一致，只对相对部分生效）：
//     A1     -> 列行都平移
//     $A1    -> 列绝对，只平移行
//     A$1    -> 行绝对，只平移列
//     $A$1   -> 完全不动
//     Sheet!A1 / 'My Sheet'!A1 -> 同上，表名原样保留
//     A1:B2  -> 两端各自平移（词法器出两个 Ref，天然分别处理）
//
// 不能碰的东西：字符串字面量（"A1" 里的 A1 不是引用）、
// 函数名、已定义名称（LET/LAMBDA 的绑定名、命名区域）。
// 靠词法器的 token 类型区分，而不是靠正则在原文上找 —— 后者一定会误伤字符串。
//
// 越界处理：平移后超出表格边界时按 Excel 的行为替换成 #REF!，
// 而不是静默夹取到边界，否则数据会悄悄指向错误的格子。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include "lexer.hpp"
#include "sheet.hpp"

namespace xl {

// 平移结果
struct ShiftResult {
    std::string text;           // 重写后的公式
    bool ok = true;
    int refErrors = 0;          // 有多少个引用越界变成了 #REF!
    std::string error;          // 词法/语法层面的失败原因（区别于越界）
};

// dc / dr 为列、行偏移量（可为负）
ShiftResult shiftFormula(const std::string& formula, int dc, int dr,
                         int maxCol = 16383, int maxRow = 1048575);

// ---------------------------------------------------------------------------
// 插入 / 删除行列时的引用重写
// ---------------------------------------------------------------------------
// 与整体平移 shiftFormula 的区别：这里只有"位置在阈值之后"的引用才动。
//
// 在第 3 行插入 1 行时：
//     =A1+1   不动（A1 在插入点之前）
//     =A3+1   -> =A4+1（原本指向第 3 行，被挤到下面去了）
//     =$A$3+1 不动（绝对引用不随插入移动，这是 Excel 的行为）
//
// axis: 0 = 按列，1 = 按行
// at:   插入/删除发生的位置
// delta: 位移量（插入为正，删除为负）
ShiftResult shiftFormulaAt(const std::string& formula, int axis, int at, int delta,
                           int maxCol = 16383, int maxRow = 1048575);

// 只判断公式里是否含可平移的引用（用于决定"填充"是否有意义）
bool hasRelativeRef(const std::string& formula);

// 把引用还原成文本（含表名与 $ 修饰），供重写与其它模块复用
std::string refToText(const RefPart& r);

// ---------------------------------------------------------------------------
// 填充：把源区域的公式按相对引用规则铺到目标区域
// ---------------------------------------------------------------------------
struct FillRequest {
    int srcC0 = 0, srcR0 = 0, srcC1 = 0, srcR1 = 0;   // 源区域
    int dstC0 = 0, dstR0 = 0;                          // 目标左上角
    int repeatCols = 1, repeatRows = 1;                // 目标尺寸
};

struct FillResult {
    int written = 0;
    int refErrors = 0;          // 因越界产生 #REF! 的单元格数
    std::string error;
};

// 按源区域的内容填充目标区域。源区域尺寸为 1 或与目标同尺寸时是逐格平移；
// 源区域尺寸 >1 且与目标不同时按取模循环平铺（Excel 的行为）。
FillResult fillRange(Sheet& sh, const FillRequest& req);

// 便捷封装：向下填充（源取选区首行）/ 向右填充（源取选区首列）
void fillRect(Sheet& sh, int c0, int r0, int c1, int r1, bool byColumn, FillResult& out);

} // namespace xl
