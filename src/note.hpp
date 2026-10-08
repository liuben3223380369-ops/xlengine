#pragma once
// ---------------------------------------------------------------------------
// 单元格批注（Cell Notes / Comments）
//
// 与条件格式、数据验证同属"挂在单元格上的附加信息"，但存放方式不同：
// 前两者写在 sheetN.xml 内部，批注写在**独立部件** xl/commentsN.xml，
// 靠 sheetN.xml.rels 里的 comments 关系挂上去。
//
// 这是它最麻烦的地方：
//   1. 必须写 xl/commentsN.xml
//   2. 必须在 sheetN.xml.rels 里加一条 comments 关系
//   3. 必须在 [Content_Types].xml 里加一条 Override
//   4. 保存时 sheet rels 只在有 drawing 时才生成 —— 只有批注没有图表的表
//      原本根本不生成 rels 文件，批注就会全部丢失
//
// 未实现的部分（诚实标注）：
//   Excel 里批注的红色三角标记靠 vmlDrawingN.vml + sheetN.xml 的
//   <legacyDrawing> 呈现。这里不生成 VML，所以**批注数据能被读到，
//   但 Excel 里看不到三角标记**（需要用 Excel 的"显示批注"才能看到内容）。
//   做 VML 需要再实现一套完全不同的 drawing 方言，代价与收益不成比例。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <map>

namespace xl {

struct CellNote {
    int col = 0, row = 0;
    std::string author;
    std::string text;
    bool visible = false;      // 是否常显（Excel 的"显示批注"）
};

// ---------------------------------------------------------------------------
// 与 OOXML 的互转
// ---------------------------------------------------------------------------

// 生成 xl/commentsN.xml
std::string buildCommentsXml(const std::vector<CellNote>& notes);

// 解析 xl/commentsN.xml。authors 表按出现顺序编号，comment 的 authorId 指回它。
bool parseCommentsXml(const std::string& xml, std::vector<CellNote>& out,
                      std::vector<std::string>& warnings);

// 单元格地址文本（"A1"），供 ref 属性使用
std::string cellAddrText(int col, int row);
// 解析 "A1" 形式的地址，供读取 ref 属性
bool parseCellAddr(const std::string& s, int& col, int& row);

} // namespace xl
