#pragma once
// ---------------------------------------------------------------------------
// TrueType 解析与子集化（用于 PDF 字体嵌入）
//
// 为什么必须子集化：一个中文 TTF 有 4~8 MB。整包嵌入会让每份 PDF 都变成
// 几 MB，而一页表格实际只用得上几十个字形。子集化后通常只剩几 KB~几十 KB。
//
// 子集化的核心难点有三个，每个都容易静默出错：
//   1. 复合字形（Composite glyph）：中日韩字体大量使用"用别的字形拼出本字形"，
//      只保留顶层字形会画出残缺的字。必须做传递闭包，把被引用的组件一起带上，
//      并且重写组件里的 glyphIndex（因为子集后编号变了）。
//   2. loca 表格式：head.indexToLocFormat 决定用 short（/2）还是 long。
//      重新生成 loca 时必须同步改写这个字段，否则字形全部错位。
//   3. cmap：子集的码点不连续，只能生成 format 4（BMP）或 format 12。
//      沿用原表的 segment 结构会让未包含的码点映射到错误字形。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace xl {

struct TtfMetrics {
    int unitsPerEm = 1000;
    int numGlyphs = 0;
    int numberOfHMetrics = 0;
    int ascent = 0, descent = 0;
    int indexToLocFormat = 0;
    int bboxXMin = 0, bboxYMin = 0, bboxXMax = 0, bboxYMax = 0;
};

class TtfFont {
public:
    // 返回 false 时 err 说明原因（文件不存在 / 不是 TTF / 缺关键表）
    bool load(const std::string& path, std::string& err);

    const TtfMetrics& metrics() const { return m_; }

    // Unicode 码点 -> 原始字形 ID；不存在返回 0
    uint16_t glyphFor(uint32_t cp) const;

    // 文本宽度（单位：字身单位 em）
    double advanceOf(uint16_t gid) const;
    double textWidthEm(const std::string& utf8) const;

    // 子集化：给定要保留的码点集合，生成完整 TTF 二进制。
    // 返回的 cids 把"码点 -> 子集内的字形序号"回填，供 PDF 内容流使用。
    bool subset(const std::vector<uint32_t>& cps,
                std::vector<uint8_t>& outFont,
                std::map<uint32_t, uint16_t>& cids,
                std::string& err) const;

    bool loaded() const { return !raw_.empty(); }

    // 码点序列（UTF-8 -> Unicode），与引擎里的解码逻辑保持一致
    static std::vector<uint32_t> decodeUtf8(const std::string& s);

private:
    std::vector<uint8_t> raw_;
    TtfMetrics m_;
    std::map<uint32_t, uint32_t> cmap_;           // cp -> gid
    std::vector<uint32_t> loca_;                  // gid -> glyf 内偏移
    std::vector<uint16_t> advW_;                  // 每个字形的前进宽度
    std::map<std::string, std::pair<uint32_t, uint32_t>> tables_;  // tag -> (offset, len)
    uint32_t glyfOff_ = 0, glyfLen_ = 0;

    const uint8_t* table(const std::string& tag, uint32_t& len) const;
    // 递归收集复合字形依赖
    void collectDeps(uint16_t gid, std::vector<bool>& keep, int depth = 0) const;
};

} // namespace xl
