#pragma once
// ---------------------------------------------------------------------------
// 数据验证（Data Validation）
//
// 与条件格式是同一类结构：一条规则挂在若干区域上。但语义完全不同 ——
// 条件格式只影响显示，数据验证要**拦住输入**。
//
// 这一点决定了实现上的差异：条件格式可以等到渲染时再判定，
// 数据验证必须在写入的那一刻就判定，否则用户已经把非法值存进去了。
//
// OOXML 里 <dataValidations> 的位置有序列约束：
//   在 conditionalFormatting 之后、drawing 之前
// 顺序错了 Excel 会报文件损坏。
//
// 支持的 type：
//   none / whole / decimal / list / date / time / textLength / custom
// 支持的 operator（list 与 custom 除外）：
//   between / notBetween / equal / notEqual / greaterThan / lessThan
//   greaterThanOrEqual / lessThanOrEqual
//
// 未实现的部分：
//   - date / time 的边界值按序列号比较，但界面输入不做日期解析，
//     所以"日期必须在某区间"这类规则的实际体验受限
//   - 下拉箭头（list 在 Excel 里会显示下拉框）在终端无对应物
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <array>
#include <optional>
#include "value.hpp"

namespace xl {

class Sheet;

enum class DvType {
    None,
    Whole,        // 整数
    Decimal,      // 小数
    List,         // 序列（下拉）
    Date,
    Time,
    TextLength,   // 文本长度
    Custom        // 自定义公式
};

enum class DvOperator {
    None,
    Between, NotBetween,
    Equal, NotEqual,
    GreaterThan, LessThan,
    GreaterThanOrEqual, LessThanOrEqual
};

// 非法输入时的处理强度
enum class DvErrorStyle {
    Stop,         // 拒绝输入
    Warning,      // 警告，用户可选择继续
    Information   // 仅提示
};

struct DataValidation {
    DvType type = DvType::None;
    DvOperator op = DvOperator::None;
    DvErrorStyle errorStyle = DvErrorStyle::Stop;

    std::vector<std::array<int, 4>> rects;   // 每个区域 {c0,r0,c1,r1}

    std::string formula1;                     // 下界 / 序列内容 / 自定义公式
    std::string formula2;                     // 上界（仅 between / notBetween）

    bool allowBlank = true;                   // 空格子是否放行
    bool showInputMessage = false;
    bool showErrorMessage = true;
    std::string promptTitle, prompt;          // 输入提示
    std::string errorTitle, error;            // 出错提示

    // list 的候选项（formula1 解析后的结果）
    std::vector<std::string> listItems;
};

// 判定某个值是否满足规则。只判定，不负责拦截 —— 拦截在写入层做。
//
// col/row 用于求值自定义公式：公式相对区域左上角书写，
// 判定第 k 格时要按偏移量平移相对引用（与条件格式一致）。
struct DvVerdict {
    bool ok = true;
    std::string message;      // 不满足时的说明（供界面显示）
    bool stop = true;         // 是否必须拒绝（errorStyle == Stop）
};

DvVerdict checkDataValidation(Sheet& sh, const DataValidation& dv, int col, int row,
                              const Value& v);

// 找出覆盖指定格子的规则（返回 nullptr 表示无规则）
const DataValidation* dvForCell(const std::vector<DataValidation>& list, int col, int row);

// ---------------------------------------------------------------------------
// 与 OOXML 的互转
// ---------------------------------------------------------------------------
const char* dvTypeToOoxml(DvType t);
const char* dvOperatorToOoxml(DvOperator o);
const char* dvErrorStyleToOoxml(DvErrorStyle s);
bool ooxmlToDvType(const std::string& s, DvType& out);
bool ooxmlToDvOperator(const std::string& s, DvOperator& out);
bool ooxmlToDvErrorStyle(const std::string& s, DvErrorStyle& out);

// 把 "1,2,3" 或 "\"a,b\"" 形式的序列文本拆成候选项。
// 引号先剥离，再按逗号切分；连续逗号产生的空项会被丢弃。
std::vector<std::string> splitListFormula(const std::string& f);

// 反向：把候选项拼回 formula1（带引号，与 Excel 写法一致）
std::string joinListFormula(const std::vector<std::string>& items);

} // namespace xl
