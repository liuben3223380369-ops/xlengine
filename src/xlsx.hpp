#pragma once
// ---------------------------------------------------------------------------
// xlsx 读写（OOXML / SpreadsheetML）。
//
// 自己写的原因与边界：沙盒无 libzip/minizip，且上一轮实证核验确认
// xlsx 是本项目中"没有可拼组件、必须自己实现"的三块之一（另两块是图表与 UI）。
//
// 已支持：
//   写：多工作表、数字/文本/布尔/错误/公式、共享字符串、最小样式表
//   读：同上 + inlineStr + 按 r 属性定位单元格（跳空列）
// 明示不支持（读时降级而非报错）：
//   共享公式 t="shared" 的引用偏移展开（只取首格公式，其余留空）
//   数字格式的日期还原（日期以序列号读出，格式化交给上层）
//   合并单元格、条件格式、数据验证、批注、图片
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <memory>
#include "sheet.hpp"
#include <array>
#include "cf.hpp"
#include "dv.hpp"
#include "note.hpp"
#include "view.hpp"
#include "image.hpp"
#include "view.hpp"
#include "image.hpp"
#include "dv.hpp"
#include "note.hpp"
#include "view.hpp"
#include "image.hpp"
#include "view.hpp"
#include "image.hpp"
#include "xml.hpp"
#include "value.hpp"
#include "chart.hpp"

namespace xl {

// 样式集合：xf 索引 0 保留给 General + 默认外观
//
// OOXML 的一个 xf 同时编码 numFmtId / fontId / fillId / borderId / alignment，
// 所以索引必须由"格式码 + 样式"这个**组合**决定。
// 若让格式码和样式各自编号，单元格只能引用其中一个，另一个会被静默覆盖。
struct StyleSet {
    std::vector<std::pair<int, std::string>> customFmts;   // 自定义 numFmtId -> 格式码
    std::vector<std::string> xfCodes;                      // xf 索引 -> 格式码（空 = General）
    std::vector<CellStyle> xfStyles;                       // xf 索引 -> 样式
    std::map<std::string, int> xfOfKey;                    // 组合键 -> xf 索引

    // fonts / fills / borders 去重表。fills 的前两个必须是 none 和 gray125，
    // 这是 OOXML 的硬性约定，Excel 遇到不符的文件会报损坏。
    std::vector<CellStyle> fonts;
    std::vector<std::string> fills;                        // 只存颜色（空 = 无填充）
    std::vector<CellStyle> borders;

    int xfIndexFor(const std::string& code, const CellStyle& st);
    int xfIndexForConst(const std::string& code, const CellStyle& st) const;
    int numFmtIdFor(const std::string& code);
    int fontIdFor(const CellStyle& st);
    int fillIdFor(const CellStyle& st);
    int borderIdFor(const CellStyle& st);
};

class Workbook {
public:
    Workbook();

    // ---- 工作表管理 ----
    Sheet& addSheet(const std::string& name);
    Sheet* sheetByName(const std::string& name);
    const Sheet* sheetByName(const std::string& name) const;
    Sheet& sheet(size_t i);
    // 重命名（Sheet::setName 只改自身，工作簿侧的记录需要同步）
    bool renameSheet(size_t i, const std::string& newName);
    const Sheet& sheet(size_t i) const;
    size_t sheetCount() const { return sheets_.size(); }
    std::vector<std::string> sheetNames() const;

    // ---- 图表 ----
    // 图表挂在某张表上；anchor 决定它在表里的位置。
    // 保存时写入 xl/charts/chartN.xml + xl/drawings/drawingN.xml 及相应关系。
    int addChart(size_t sheetIndex, const Chart& c, const std::string& title = "");
    Chart* chart(int sheetIndex, size_t i);
    size_t chartCount(size_t sheetIndex) const;
    // 某张表上的全部图表（可读可改）
    std::vector<Chart>& chartsOf(size_t sheetIndex);
    // 改标题时必须同步 chartTitles_ —— 存盘用的是那份，只改 Chart::title 不生效。
    // 删除图表时也要同步删该表的标题，否则后续图表会套用错位的标题。
    void syncChartTitle(size_t sheetIndex, size_t i, const std::string& title);
    void eraseChartTitle(size_t sheetIndex, size_t i);

    // ---- 保存 ----
    bool save(const std::string& path, std::string& err) const;

    // ---- 合并单元格 ----
    // 区域含首尾。合并后只有左上角存数据，其余格子在 UI 上不显示。
    void addMerge(int sheetIdx, int c0, int r0, int c1, int r1);
    void clearMerges(int sheetIdx);
    // 该格子是否属于某个合并区域、且不是左上角（即被吞掉的格子）
    bool isMergedAway(int sheetIdx, int col, int row) const;
    // 该格子所属合并区域的左上角；不在任何区域内时返回 false
    bool mergeAnchorOf(int sheetIdx, int col, int row, int& ac, int& ar) const;
    // 该格子所在合并区域的尺寸（不在区域内时返回 {1,1}）
    std::pair<int,int> mergeSpan(int sheetIdx, int col, int row) const;
    const std::vector<std::vector<std::array<int,4>>>& allMerges() const;

    // ---- 结构性编辑：插入 / 删除行与列 ----
    // 定义在 edit.cpp。除内容外，还会同步移动合并区域、条件格式、
    // 数据验证、批注、图表/图片锚点与行高列宽。
    void insertRows(int sheetIdx, int at, int count);
    void insertCols(int sheetIdx, int at, int count);
    void deleteRows(int sheetIdx, int at, int count);
    void deleteCols(int sheetIdx, int at, int count);
    void structuralEdit(int sheetIdx, int axis, int at, int delta);
    // 取消合并：与给定区域有交集的合并块全部移除
    bool unmerge(int sheetIdx, int c0, int r0, int c1, int r1);

    // ---- 条件格式 ----
    void addCf(int sheetIdx, const ConditionalFormat& cf);
    void clearCfs(int sheetIdx);
    const std::vector<std::vector<ConditionalFormat>>& allCfs() const { return cfs_; }

    // ---- 增量重算 ----
    // 逐表增量重算，并处理跨表失效：
    // 表 A 的公式引用表 B 的格子，B 改了以后 A 必须重算，
    // 否则会读到 B 的旧缓存值 —— 这是增量重算最容易出错的地方之一。
    void recalcDirty();

    // ---- 定义名称（命名区域）----
    // name -> 定义文本，如 "销售额" -> "Sheet1!$B$2:$B$5"
    bool addDefinedName(const std::string& name, const std::string& refersTo,
                        std::string& why);
    bool removeDefinedName(const std::string& name);
    void clearDefinedNames();
    const std::vector<std::pair<std::string,std::string>>& definedNames() const {
        return definedNames_;
    }
    const std::string* findDefinedName(const std::string& name) const;
    // 名称变更时让所有引用它的公式失效（增量重算需要）
    void invalidateNameRefs(const std::string& name);
    // 求某个定义名称的值。ctxSheet 为当前求值所在表（用于解析相对引用）。
    bool evalDefinedName(const std::string& name, Value& out, Sheet* ctxSheet);

    // ---- 嵌入图片 ----
    void addImage(int sheetIdx, const ImagePart& img);
    void clearImages(int sheetIdx);
    const std::vector<std::vector<ImagePart>>& allImages() const { return images_; }

    // ---- 视图属性（列宽/行高/冻结/筛选）----
    SheetLayout& layout(int sheetIdx);
    const SheetLayout& layout(int sheetIdx) const;
    void setColWidth(int sheetIdx, int col, double w);
    void setRowHeight(int sheetIdx, int row, double h);
    void setFreeze(int sheetIdx, int frozenCols, int frozenRows);
    void setAutoFilter(int sheetIdx, int c0, int r0, int c1, int r1);
    void clearAutoFilter(int sheetIdx);
    const std::vector<SheetLayout>& allLayouts() const { return layouts_; }

    // ---- 批注 ----
    void addNote(int sheetIdx, const CellNote& n);
    bool removeNote(int sheetIdx, int col, int row);
    void clearNotes(int sheetIdx);
    const CellNote* noteAt(int sheetIdx, int col, int row) const;
    const std::vector<std::vector<CellNote>>& allNotes() const { return notes_; }

    // ---- 数据验证 ----
    void addDv(int sheetIdx, const DataValidation& dv);
    void clearDvs(int sheetIdx);
    const std::vector<std::vector<DataValidation>>& allDvs() const { return dvs_; }

    // ---- 数字格式 ----
    // 读文件时把解析到的格式码写回单元格；写文件时收集用到的格式码。
    // 内建格式 ID（0..49）直接引用，其余从 164 起分配自定义 ID。

    // ---- 加载 ----
    bool load(const std::string& path, std::string& err);

    // 加载/保存过程中遇到的降级项（如共享公式），供上层提示用户
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    struct SheetRec {
        std::string name;
        std::unique_ptr<Sheet> sheet;
    };
    std::vector<SheetRec> sheets_;
    // 每张表上挂的图表
    std::vector<std::vector<Chart>> charts_;
    std::vector<std::vector<std::string>> chartTitles_;
    mutable std::vector<std::string> warnings_;
    std::vector<std::vector<std::array<int,4>>> merges_;   // 每表的合并区域
    std::vector<std::vector<ConditionalFormat>> cfs_;      // 每表的条件格式
    struct PendingDxf { int sheet, cfIndex, ruleIndex, dxfId; };
    std::vector<PendingDxf> pendingDxf_;  // dxfId 回填队列（等 styles.xml 解析完）
    std::vector<std::vector<DataValidation>> dvs_;   // 每表的数据验证
    std::vector<std::vector<CellNote>> notes_;       // 每表的批注
    std::vector<SheetLayout> layouts_;               // 每表的视图属性
    std::vector<std::vector<ImagePart>> images_;     // 每表嵌入的图片
    std::vector<std::pair<std::string,std::string>> definedNames_;  // 名称 -> 引用文本
    mutable int dnDepth_ = 0;                        // 定义名称求值递归深度
    mutable std::set<std::string> imageTypesUsed_;   // 用过的图片扩展名（save 是 const，需 mutable）

    // 内部：组装/拆分各 part
    std::string buildContentTypes() const;
    std::string buildRootRels() const;
    std::string buildWorkbookXml() const;
    std::string buildWorkbookRels() const;
    std::string buildSharedStrings(const std::vector<std::string>& ss) const;
    std::string buildStyles(StyleSet& st, DxfSet& dxfs) const;
    void parseCfNode(const XmlNode* node, int sheetIdx);
    void parseDvNode(const XmlNode* node, int sheetIdx);
    void applyDxfStyles(const std::vector<CfStyle>& dxfs);
    std::string buildSheetXml(Sheet& sh,
                              std::vector<std::string>& sharedStrings,
                              std::map<std::string, int>& ssIndex,
                              const StyleSet& st,
                              const std::vector<std::array<int,4>>& mergeRects,
                              const std::vector<ConditionalFormat>& cfList,
                              DxfSet& dxfs,
                              const std::vector<DataValidation>& dvList,
                              const SheetLayout& lay,
                              bool hasDrawing = false) const;
    // 遍历全部表收集用到的数字格式码
    StyleSet buildStyleSet() const;

    bool loadSheetXml(Sheet& sh, const std::string& xml,
                      const std::vector<std::string>& shared,
                      const std::vector<std::string>& xfCodes,
                      const std::vector<CellStyle>& xfStyles,
                      int sheetIdx);
};

// 工具：A1 <-> (col,row)
bool parseCellRef(const std::string& ref, int& col, int& row);

} // namespace xl
