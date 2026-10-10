#include "xlsx.hpp"
#include "numfmt.hpp"
#include "style.hpp"
#include "zip.hpp"
#include "chartxml.hpp"
#include "refshift.hpp"
#include "xml.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <cmath>
#include <functional>

namespace xl {

// ---------------------------------------------------------------------------
// A1 地址解析 / 生成
// ---------------------------------------------------------------------------
bool parseCellRef(const std::string& ref, int& col, int& row) {
    size_t i = 0;
    if (i < ref.size() && ref[i] == '$') i++;
    size_t cs = i;
    while (i < ref.size() && std::isalpha((unsigned char)ref[i])) i++;
    if (i == cs) return false;
    long c = 0;
    for (size_t k = cs; k < i; k++) c = c * 26 + (std::toupper((unsigned char)ref[k]) - 'A' + 1);
    if (i < ref.size() && ref[i] == '$') i++;
    size_t rs = i;
    while (i < ref.size() && std::isdigit((unsigned char)ref[i])) i++;
    if (i == rs || i != ref.size()) return false;
    col = (int)(c - 1);
    row = (int)(std::stol(ref.substr(rs)) - 1);
    return col >= 0 && row >= 0;
}

// ---------------------------------------------------------------------------
// 跨表引用转发（sheet.cpp 通过这两个入口访问工作簿）
// ---------------------------------------------------------------------------
Value lookupCrossSheet(Workbook* wb, const std::string& sheetName, int col, int row) {
    if (!wb) return Value::error(Err::Ref);
    Sheet* s = wb->sheetByName(sheetName);
    if (!s) return Value::error(Err::Ref);
    return s->valueAt(col, row);
}

bool lookupCrossSheetByAddr(Workbook* wb, const std::string& sheetName,
                            const std::string& addr, Value& out) {
    if (!wb) return false;
    Sheet* s = wb->sheetByName(sheetName);
    if (!s) return false;
    int c = 0, r = 0;
    if (!parseCellRef(addr, c, r)) return false;
    out = s->valueAt(c, r);
    return true;
}
// 供 Sheet::usedRange 转发：跨表整列引用要知道目标表的已用区域
bool workbookUsedRange(void* wb, const std::string& name,
                                  int& c0, int& r0, int& c1, int& r1) {
    Workbook* w = static_cast<Workbook*>(wb);
    if (!w) return false;
    Sheet* sh = w->sheetByName(name);
    if (!sh) return false;
    std::string self;
    (void)self;
    return sh->usedRange(std::string(), c0, r0, c1, r1);
}



// ---------------------------------------------------------------------------
// Workbook
// ---------------------------------------------------------------------------
Workbook::Workbook() {
    // 工作簿默认带一张空表，与 Excel 新建文件一致
    addSheet("Sheet1");
}

Sheet& Workbook::addSheet(const std::string& name) {
    std::string n = name.empty() ? ("Sheet" + std::to_string(sheets_.size() + 1)) : name;
    // 重名去重：Excel 表名必须唯一
    bool dup = false;
    for (auto& s : sheets_) if (s.sheet->name() == n) { dup = true; break; }
    if (dup) {
        for (int k = 2; ; k++) {
            std::string cand = n + "_" + std::to_string(k);
            bool hit = false;
            for (auto& s : sheets_) if (s.sheet->name() == cand) { hit = true; break; }
            if (!hit) { n = cand; break; }
        }
    }
    SheetRec rec;
    rec.name = n;
    rec.sheet = std::make_unique<Sheet>();
    rec.sheet->setName(n);
    rec.sheet->setOwner(this);
    sheets_.push_back(std::move(rec));
    charts_.emplace_back();
    chartTitles_.emplace_back();
    return *sheets_.back().sheet;
}

// 表名以 Sheet 自身的名字为准，不读 SheetRec::name。
//
// 原先两处都可能被改：renameSheet() 会同步两边，但 Sheet::setName() 只改一边。
// 于是"建表后改名"的表在 sheetByName 里查不到 —— 表现为**跨表引用一律 #REF!**，
// 且不报任何错。sheetNames() 早已按 Sheet 自身的名字来，这里保持一致。
Sheet* Workbook::sheetByName(const std::string& name) {
    for (auto& s : sheets_) if (s.sheet->name() == name) return s.sheet.get();
    return nullptr;
}
const Sheet* Workbook::sheetByName(const std::string& name) const {
    for (auto& s : sheets_) if (s.sheet->name() == name) return s.sheet.get();
    return nullptr;
}
Sheet& Workbook::sheet(size_t i) { return *sheets_.at(i).sheet; }

int Workbook::addChart(size_t sheetIndex, const Chart& c, const std::string& title) {
    if (sheetIndex >= sheets_.size()) return -1;
    charts_[sheetIndex].push_back(c);
    chartTitles_[sheetIndex].push_back(title.empty()
        ? ("图表 " + std::to_string(charts_[sheetIndex].size())) : title);
    return (int)charts_[sheetIndex].size() - 1;
}

Chart* Workbook::chart(int sheetIndex, size_t i) {
    if (sheetIndex < 0 || (size_t)sheetIndex >= charts_.size()) return nullptr;
    if (i >= charts_[sheetIndex].size()) return nullptr;
    return &charts_[sheetIndex][i];
}

size_t Workbook::chartCount(size_t sheetIndex) const {
    if (sheetIndex >= charts_.size()) return 0;
    return charts_[sheetIndex].size();
}

void Workbook::syncChartTitle(size_t sheetIndex, size_t i, const std::string& title) {
    if (sheetIndex >= chartTitles_.size()) return;
    if (i >= chartTitles_[sheetIndex].size()) chartTitles_[sheetIndex].resize(i + 1);
    chartTitles_[sheetIndex][i] = title;
}

void Workbook::eraseChartTitle(size_t sheetIndex, size_t i) {
    if (sheetIndex >= chartTitles_.size()) return;
    if (i >= chartTitles_[sheetIndex].size()) return;
    chartTitles_[sheetIndex].erase(chartTitles_[sheetIndex].begin() + (long)i);
}

std::vector<Chart>& Workbook::chartsOf(size_t sheetIndex) {
    return charts_.at(sheetIndex);
}

bool Workbook::renameSheet(size_t i, const std::string& newName) {
    std::string n = newName.empty() ? ("Sheet" + std::to_string(i + 1)) : newName;
    for (size_t k = 0; k < sheets_.size(); k++)
        if (k != i && sheets_[k].sheet->name() == n) return false;      // 表名必须唯一
    if (i >= sheets_.size()) return false;
    sheets_[i].name = n;
    sheets_[i].sheet->setName(n);
    return true;
}
const Sheet& Workbook::sheet(size_t i) const { return *sheets_.at(i).sheet; }

std::vector<std::string> Workbook::sheetNames() const {
    // 以 Sheet 自己的名字为准，不读 SheetRec::name。
    // 否则 sheet(0).setName("一月") 改了表自身、工作簿侧还是旧名，
    // 两边不一致 —— 保存时写旧名、界面显示新名。Sheet 才是唯一真源。
    std::vector<std::string> out;
    for (auto& s : sheets_) out.push_back(s.sheet->name());
    return out;
}

// ---------------------------------------------------------------------------
// 各 part 的 XML
// ---------------------------------------------------------------------------
std::string Workbook::buildContentTypes() const {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
      << "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
      << "<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n";
    // 图片按扩展名登记 Default（不是 Override —— 那是按部件路径登记的）
    for (const std::string& ext : imageTypesUsed_)
        o << "<Default Extension=\"" << ext << "\" ContentType=\"" << imageContentType(ext) << "\"/>\n";
    o << "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>\n";
    for (size_t i = 0; i < sheets_.size(); i++)
        o << "<Override PartName=\"/xl/worksheets/sheet" << (i + 1) << ".xml\" "
          << "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>\n";
    // 批注是独立部件，必须登记 ContentType，否则 Excel 报文件损坏
    for (size_t i = 0; i < notes_.size(); i++)
        if (!notes_[i].empty())
            o << "<Override PartName=\"/xl/comments" << (i + 1) << ".xml\" "
              << "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml\"/>\n";
    o << "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>\n";
    o << "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>\n";
    // 图表相关的两个 ContentType 缺一不可：漏了 chart 的，Excel 会提示文件损坏
    for (size_t i = 0; i < charts_.size(); i++)
        if (!charts_[i].empty())
            o << "<Override PartName=\"/xl/drawings/drawing" << (i + 1)
              << ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>\n";
    int total = 0;
    for (auto& v : charts_) total += (int)v.size();
    for (int k = 1; k <= total; k++)
        o << "<Override PartName=\"/xl/charts/chart" << k
          << ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>\n";
    o << "</Types>";
    return o.str();
}

std::string Workbook::buildRootRels() const {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>\n"
           "</Relationships>";
}

std::string Workbook::buildWorkbookXml() const {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
      << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n"
      << "<sheets>\n";
    for (size_t i = 0; i < sheets_.size(); i++)
        o << "<sheet name=\"" << xmlEscapeAttr(sheets_[i].sheet->name()) << "\" sheetId=\""
          << (i + 1) << "\" r:id=\"rId" << (i + 1) << "\"/>\n";
    o << "</sheets>\n";
    // definedNames 必须排在 <sheets> **之后**（CT_Workbook 序列约束）。
    // 放在前面 Excel 会提示文件有问题，而自己的解析器不会报错。
    if (!definedNames_.empty()) {
        o << "<definedNames>\n";
        for (const auto& kv : definedNames_)
            o << "<definedName name=\"" << xmlEscapeAttr(kv.first) << "\">"
              << xmlEscape(kv.second) << "</definedName>\n";
        o << "</definedNames>\n";
    }
    o << "</workbook>";
    return o.str();
}

std::string Workbook::buildWorkbookRels() const {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
    for (size_t i = 0; i < sheets_.size(); i++)
        o << "<Relationship Id=\"rId" << (i + 1) << "\" "
          << "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
          << "Target=\"worksheets/sheet" << (i + 1) << ".xml\"/>\n";
    size_t rid = sheets_.size() + 1;
    o << "<Relationship Id=\"rId" << rid++ << "\" "
      << "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
      << "Target=\"styles.xml\"/>\n";
    o << "<Relationship Id=\"rId" << rid++ << "\" "
      << "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" "
      << "Target=\"sharedStrings.xml\"/>\n";
    o << "</Relationships>";
    return o.str();
}

std::string Workbook::buildSharedStrings(const std::vector<std::string>& ss) const {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\""
      << ss.size() << "\" uniqueCount=\"" << ss.size() << "\">\n";
    for (auto& s : ss)
        o << "<si><t xml:space=\"preserve\">" << xmlEscape(s) << "</t></si>\n";
    o << "</sst>";
    return o.str();
}

// 收集工作簿里用到的全部格式码，并分配 xf 索引（0 保留给 General）
int StyleSet::numFmtIdFor(const std::string& code) {
    // 空格式码是 General。这里必须提前返回：builtinNumFmt 对未定义的 ID
    // 也返回空串，若拿 "" 去逐个比对，会匹配上第一个返回空串的 ID（比如 23），
    // 结果 General 的格子被写成一个莫名其妙的 numFmtId。
    if (code.empty()) return 0;
    // 内建 ID 直接引用，无需在 styles.xml 里声明
    for (int id = 0; id <= 49; id++) {
        const char* b = builtinNumFmt(id);
        if (b && code == b) return id;
    }
    for (auto& p : customFmts) if (p.second == code) return p.first;
    int id = 164 + (int)customFmts.size();
    customFmts.push_back({id, code});
    return id;
}

int StyleSet::fontIdFor(const CellStyle& st) {
    // 索引 0 是默认字体，从 1 起才是自定义
    for (size_t i = 0; i < fonts.size(); i++)
        if (fonts[i].key() == st.key()) return (int)i + 1;
    fonts.push_back(st);
    return (int)fonts.size();          // 刚 push 进去的，下标 +1
}

int StyleSet::fillIdFor(const CellStyle& st) {
    // fills 的前两个被 OOXML 占死：0 = none，1 = gray125
    if (st.fillColor.empty()) return 0;
    for (size_t i = 0; i < fills.size(); i++)
        if (fills[i] == st.fillColor) return (int)i + 2;
    fills.push_back(st.fillColor);
    return (int)fills.size() + 1;
}

int StyleSet::borderIdFor(const CellStyle& st) {
    if (st.border == BorderStyle::None) return 0;
    for (size_t i = 0; i < borders.size(); i++) {
        if (borders[i].border == st.border && borders[i].borderColor == st.borderColor)
            return (int)i + 1;
    }
    borders.push_back(st);
    return (int)borders.size();
}

int StyleSet::xfIndexFor(const std::string& code, const CellStyle& st) {
    std::string k = code + "\x01" + st.key();
    auto it = xfOfKey.find(k);
    if (it != xfOfKey.end()) return it->second;
    int idx = (int)xfCodes.size();
    xfCodes.push_back(code);
    xfStyles.push_back(st);
    xfOfKey[k] = idx;
    return idx;
}

int StyleSet::xfIndexForConst(const std::string& code, const CellStyle& st) const {
    std::string k = code + "\x01" + st.key();
    auto it = xfOfKey.find(k);
    return it == xfOfKey.end() ? 0 : it->second;
}

StyleSet Workbook::buildStyleSet() const {
    StyleSet st;
    for (auto& r : sheets_)
        for (auto& kv : r.sheet->allCells()) {
            // 格式码和样式都为默认时不需要额外 xf（用索引 0）
            std::string nf = r.sheet->numFmtAt(kv.first.first, kv.first.second);
            CellStyle cs  = r.sheet->styleAt(kv.first.first, kv.first.second);
            if (nf.empty() && cs.empty()) continue;
            st.xfIndexFor(nf, cs);
        }
    return st;
}

std::string Workbook::buildStyles(StyleSet& st, DxfSet& dxfs) const {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";

    // 先把每个组合都转成 numFmtId —— 这一步才会登记自定义格式。
    // 顺序不能反：先输出 numFmts 再算 ID 的话，customFmts 还是空的，
    // 自定义格式码会整个丢掉（读回来变成 General）。
    std::vector<int> ids;
    for (const std::string& code : st.xfCodes) ids.push_back(st.numFmtIdFor(code));

    // fonts / fills / borders 必须在输出之前全部登记完。
    // 边输出 <fonts count="N"> 边登记的话，count 写的是登记前的数量（1），
    // 而后面 xf 引用的 fontId 会超出表长 —— 文件能生成，但样式全丢。
    std::vector<int> fontIds, fillIds, borderIds;
    for (const CellStyle& cs : st.xfStyles) {
        fontIds.push_back(st.fontIdFor(cs));
        fillIds.push_back(st.fillIdFor(cs));
        borderIds.push_back(st.borderIdFor(cs));
    }

    // 自定义格式（内建 ID 不需要声明）
    if (!st.customFmts.empty()) {
        o << "<numFmts count=\"" << st.customFmts.size() << "\">\n";
        for (auto& p : st.customFmts)
            o << "<numFmt numFmtId=\"" << p.first << "\" formatCode=\""
              << xmlEscapeAttr(p.second) << "\"/>\n";
        o << "</numFmts>\n";
    }

    // fonts：索引 0 是默认，其后每个自定义字体一个
    o << "<fonts count=\"" << (st.fonts.size() + 1) << "\"><font><sz val=\"11\"/>"
      << "<color theme=\"1\"/><name val=\"Calibri\"/></font>\n";
    for (const CellStyle& f : st.fonts) {
        o << "<font>";
        if (f.bold)      o << "<b/>";
        if (f.italic)    o << "<i/>";
        if (f.underline) o << "<u/>";
        if (f.strike)    o << "<strike/>";
        o << "<sz val=\"" << (f.fontSize > 0 ? f.fontSize : 11) << "\"/>";
        if (!f.fontColor.empty()) o << "<color rgb=\"FF" << f.fontColor << "\"/>";
        o << "<name val=\"" << xmlEscapeAttr(f.fontName.empty() ? "Calibri" : f.fontName) << "\"/>";
        o << "</font>\n";
    }
    o << "</fonts>\n";

    // fills：前两个被 OOXML 占死（0=none, 1=gray125），这是硬性约定
    o << "<fills count=\"" << (st.fills.size() + 2) << "\">"
      << "<fill><patternFill patternType=\"none\"/></fill>"
      << "<fill><patternFill patternType=\"gray125\"/></fill>";
    for (const std::string& c : st.fills)
        o << "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF" << c
          << "\"/><bgColor indexed=\"64\"/></patternFill></fill>";
    o << "</fills>\n";

    // borders：索引 0 是无边框
    o << "<borders count=\"" << (st.borders.size() + 1) << "\"><border><left/><right/><top/><bottom/><diagonal/></border>";
    for (const CellStyle& b : st.borders) {
        const char* bs = borderToOoxml(b.border);
        std::string col = b.borderColor.empty() ? std::string() : (" rgb=\"FF" + b.borderColor + "\"");
        o << "<border>";
        o << "<left style=\"" << bs << "\"><color" << col << "/></left>";
        o << "<right style=\"" << bs << "\"><color" << col << "/></right>";
        o << "<top style=\"" << bs << "\"><color" << col << "/></top>";
        o << "<bottom style=\"" << bs << "\"><color" << col << "/></bottom>";
        o << "<diagonal/>";
        o << "</border>";
    }
    o << "</borders>\n";

    o << "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>\n";

    // cellXfs：索引 0 = General + 默认外观，其后每个组合一个
    size_t xfCount = st.xfCodes.size() + 1;
    o << "<cellXfs count=\"" << xfCount << "\">\n";
    o << "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>\n";
    for (size_t i = 0; i < st.xfCodes.size(); i++) {
        const CellStyle& cs = st.xfStyles[i];
        o << "<xf numFmtId=\"" << ids[i] << "\""
          << " fontId=\"" << fontIds[i] << "\""
          << " fillId=\"" << fillIds[i] << "\""
          << " borderId=\"" << borderIds[i] << "\""
          << " xfId=\"0\"";
        if (!st.xfCodes[i].empty()) o << " applyNumberFormat=\"1\"";
        if (!cs.empty())            o << " applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"";
        bool needAlign = (cs.hAlign != HAlign::General || cs.vAlign != VAlign::Bottom || cs.wrapText);
        if (needAlign) {
            o << "><alignment";
            if (cs.hAlign != HAlign::General) o << " horizontal=\"" << hAlignToOoxml(cs.hAlign) << "\"";
            if (cs.vAlign != VAlign::Bottom)  o << " vertical=\"" << vAlignToOoxml(cs.vAlign) << "\"";
            if (cs.wrapText)                  o << " wrapText=\"1\"";
            o << "/></xf>\n";
        } else {
            o << "/>\n";
        }
    }
    o << "</cellXfs>\n"
      << "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>\n";
    // dxfs 排在 cellStyles 之后 —— CT_Stylesheet 的序列约束。
    // 条件格式的样式走这里，而不是 cellXfs（普通单元格样式走 cellXfs）。
    if (!dxfs.items.empty()) {
        o << "<dxfs count=\"" << dxfs.items.size() << "\">\n";
        for (const CfStyle& d : dxfs.items) {
            o << "<dxf>";
            if (d.bold || d.italic || !d.fontColor.empty()) {
                o << "<font>";
                if (d.bold)   o << "<b/>";
                if (d.italic) o << "<i/>";
                if (!d.fontColor.empty()) o << "<color rgb=\"FF" << d.fontColor << "\"/>";
                o << "</font>";
            }
            if (!d.fillColor.empty())
                o << "<fill><patternFill><bgColor rgb=\"FF" << d.fillColor << "\"/></patternFill></fill>";
            o << "</dxf>\n";
        }
        o << "</dxfs>\n";
    }
    o << "</styleSheet>";
    return o.str();
}

// Err -> Excel 的错误文本（"#DIV/0!" 等），与读取端的 t="e" 对应
static std::string errToStr(Err e) { return errText(e); }

static std::string numToStr(double d) {
    if (!std::isfinite(d)) return "0";
    // 先试用递增精度，取第一个能原样读回的最短表示。
    // 直接写 setprecision(15) fixed 的话，1234.5678 会变成
    // "1234.567800000000034" —— 数值等价但文件里又丑又长，
    // 而且跨工具对比时会被当成两个不同的数。
    for (int prec = 1; prec <= 17; prec++) {
        std::ostringstream o;
        o << std::setprecision(prec) << d;
        std::string s = o.str();
        try {
            if (std::stod(s) == d) {
                // 科学计数形式里 Excel 用 E，这里统一成大写
                size_t e = s.find('e');
                if (e != std::string::npos) s[e] = 'E';
                return s;
            }
        } catch (...) { break; }
    }
    std::ostringstream o;
    o << std::setprecision(17) << d;
    return o.str();
}

// 取单元格的文本：<v> 是值，<is><t> 是内联字符串（含富文本）
std::string Workbook::buildSheetXml(Sheet& sh,
                                    std::vector<std::string>& sharedStrings,
                                    std::map<std::string, int>& ssIndex,
                                    const StyleSet& st,
                                    const std::vector<std::array<int,4>>& mergeRects,
                                    const std::vector<ConditionalFormat>& cfList,
                                    DxfSet& dxfs,
                                    const std::vector<DataValidation>& dvList,
                                    const SheetLayout& lay,
                                    bool hasDrawing) const {
    // 按 (row, col) 排序输出：XML 要求 row 递增，且同一 row 内 c 递增
    std::vector<std::pair<int,int>> keys;
    for (auto& kv : sh.allCells()) {
        const auto& key = kv.first;
        if (key.first < 0 || key.second < 0) continue;
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end(), [](const std::pair<int,int>& a, const std::pair<int,int>& b) {
        if (a.second != b.second) return a.second < b.second;   // 先行
        return a.first < b.first;                                // 后列
    });

    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      // r 前缀必须声明：<drawing r:id="..."/> 用到它，不声明的话
      // 严格 XML 解析器会报 "unbound prefix"（openpyxl 就是这么发现的）
      << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
      << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n";
    // sheetViews 与 cols 必须在 sheetData **之前**（CT_Worksheet 序列约束）。
    // 放到 sheetData 之后的话 Excel 会直接报文件损坏。
    o << buildSheetViewsXml(lay.freeze);
    o << buildColsXml(lay.colWidths);
    o << "<sheetData>\n";

    int curRow = -1;
    bool rowOpen = false;
    for (auto& key : keys) {
        int col = key.first, row = key.second;
        const Sheet::CellRec* c = sh.find(col, row);
        if (!c) continue;

        if (row != curRow) {
            if (rowOpen) o << "</row>\n";
            o << "<row r=\"" << (row + 1) << "\"";
            // 行高是 row 的属性，不是独立元素
            {
                auto rh = lay.rowHeights.find(row);
                if (rh != lay.rowHeights.end())
                    o << " ht=\"" << std::setprecision(10) << rh->second << "\" customHeight=\"1\"";
            }
            o << ">\n";
            curRow = row;
            rowOpen = true;
        }
        std::string ref = colToName(col) + std::to_string(row + 1);

        // 数字格式通过 xf 索引引用（s 属性）。
        // cellXfs 的索引 0 留给 General，所以自定义格式从 1 起 ——
        // 少了这个 +1，所有格式码会整体错位一格（第一个格子丢格式）。
        std::string sAttr;
        {
            // 索引 0 留给 General + 默认外观，所以组合索引要 +1
            std::string nf = sh.numFmtAt(key.first, key.second);
            CellStyle cs  = sh.styleAt(key.first, key.second);
            int xf = st.xfIndexForConst(nf, cs);
            if (xf != 0 || !nf.empty() || !cs.empty())
                sAttr = " s=\"" + std::to_string(xf + 1) + "\"";
        }

        // 判定写出的类型：公式格用结果类型，常量格用自身类型
        Value v = c->hasFormula ? sh.valueAt(col, row) : c->value;
        std::string tAttr;
        std::string body;

        if (v.isError()) {
            tAttr = " t=\"e\"";
            body = "<v>" + errToStr(v.e) + "</v>";
        } else if (v.isBool()) {
            tAttr = " t=\"b\"";
            body = "<v>" + std::string(v.b ? "1" : "0") + "</v>";
        } else if (v.isStr()) {
            auto it = ssIndex.find(v.s);
            int idx;
            if (it != ssIndex.end()) idx = it->second;
            else {
                idx = (int)sharedStrings.size();
                sharedStrings.push_back(v.s);
                ssIndex[v.s] = idx;
            }
            tAttr = " t=\"s\"";
            body = "<v>" + std::to_string(idx) + "</v>";
        } else if (v.isEmpty()) {
            // 空单元格：只写公式（若有），否则写空 c 标签
            if (c->hasFormula) {
                o << "<c r=\"" << ref << "\"" << sAttr << "><f>" << xmlEscape(c->formula)
                  << "</f></c>\n";
                continue;
            }
            o << "<c r=\"" << ref << "\"" << sAttr << "/>\n";
            continue;
        } else {
            body = "<v>" + numToStr(v.n) + "</v>";
        }

        if (c->hasFormula) {
            o << "<c r=\"" << ref << "\"" << sAttr << tAttr << "><f>" << xmlEscape(c->formula)
              << "</f>" << body << "</c>\n";
        } else {
            o << "<c r=\"" << ref << "\"" << sAttr << tAttr << ">" << body << "</c>\n";
        }
    }
    if (rowOpen) o << "</row>\n";
    o << "</sheetData>\n";
    // autoFilter 排在 sheetData 之后、mergeCells 之前。
    // 必须放在 mergeCells 的 if 块**外面** —— 否则没有合并单元格的表
    // 根本不会输出 autoFilter，表现为"设了筛选但存盘后消失"。
    o << buildAutoFilterXml(lay.filter);

    // mergeCells 必须排在 sheetData 之后
    if (!mergeRects.empty()) {
        o << "<mergeCells count=\"" << mergeRects.size() << "\">\n";
        for (auto& m : mergeRects)
            o << "<mergeCell ref=\"" << colToName(m[0]) << (m[1] + 1) << ":"
              << colToName(m[2]) << (m[3] + 1) << "\"/>\n";
        o << "</mergeCells>\n";
    }
    // conditionalFormatting 必须排在 mergeCells 之后、drawing 之前。
    // 这是 CT_Worksheet 的序列约束，顺序错了 Excel 会直接报文件损坏。
    for (const ConditionalFormat& cf : cfList) {
        if (cf.rects.empty() || cf.rules.empty()) continue;
        std::string sqref;
        for (size_t i = 0; i < cf.rects.size(); i++) {
            const auto& m = cf.rects[i];
            if (i) sqref += " ";
            sqref += colToName(m[0]) + std::to_string(m[1] + 1) + ":"
                   + colToName(m[2]) + std::to_string(m[3] + 1);
        }
        o << "<conditionalFormatting sqref=\"" << sqref << "\">\n";
        int prio = 1;
        for (const CfRule& rule : cf.rules) {
            int dxfId = dxfs.indexFor(rule.style);
            o << "<cfRule type=\"" << cfTypeToOoxml(rule.type) << "\""
              << " dxfId=\"" << dxfId << "\""
              << " priority=\"" << (rule.priority > 0 ? rule.priority : prio) << "\"";
            if (rule.type == CfType::CellIs && rule.op != CfOperator::None)
                o << " operator=\"" << cfOperatorToOoxml(rule.op) << "\"";
            if (rule.stopIfTrue) o << " stopIfTrue=\"1\"";
            if (rule.type == CfType::Top10) {
                if (rule.bottom)  o << " bottom=\"1\"";
                if (rule.percent) o << " percent=\"1\"";
                o << " rank=\"" << rule.rank << "\"";
            }
            if (rule.type == CfType::AboveAverage)
                o << " aboveAverage=\"" << (rule.above ? "1" : "0") << "\"";
            o << ">\n";
            // duplicateValues / uniqueValues 不写 formula；其余按需要写
            if (rule.type != CfType::Duplicate && rule.type != CfType::Unique)
                for (const std::string& f : rule.formulas)
                    o << "<formula>" << xmlEscape(f) << "</formula>\n";
            o << "</cfRule>\n";
            prio++;
        }
        o << "</conditionalFormatting>\n";
    }

    // dataValidations 必须排在 conditionalFormatting 之后、drawing 之前
    if (!dvList.empty()) {
        o << "<dataValidations count=\"" << dvList.size() << "\">\n";
        for (const DataValidation& dv : dvList) {
            std::string sqref;
            for (size_t i = 0; i < dv.rects.size(); i++) {
                const auto& m = dv.rects[i];
                if (i) sqref += " ";
                sqref += colToName(m[0]) + std::to_string(m[1] + 1) + ":"
                       + colToName(m[2]) + std::to_string(m[3] + 1);
            }
            o << "<dataValidation"
              << " type=\"" << dvTypeToOoxml(dv.type) << "\"";
            // list / custom 不写 operator —— Excel 对这两类不接受 operator 属性
            if (dv.type != DvType::List && dv.type != DvType::Custom && dv.op != DvOperator::None)
                o << " operator=\"" << dvOperatorToOoxml(dv.op) << "\"";
            o << " allowBlank=\"" << (dv.allowBlank ? "1" : "0") << "\""
              << " showInputMessage=\"" << (dv.showInputMessage ? "1" : "0") << "\""
              << " showErrorMessage=\"" << (dv.showErrorMessage ? "1" : "0") << "\""
              << " errorStyle=\"" << dvErrorStyleToOoxml(dv.errorStyle) << "\""
              << " sqref=\"" << sqref << "\"";
            if (!dv.promptTitle.empty() || !dv.prompt.empty())
                o << " promptTitle=\"" << xmlEscape(dv.promptTitle) << "\""
                  << " prompt=\"" << xmlEscape(dv.prompt) << "\"";
            if (!dv.errorTitle.empty() || !dv.error.empty())
                o << " errorTitle=\"" << xmlEscape(dv.errorTitle) << "\""
                  << " error=\"" << xmlEscape(dv.error) << "\"";
            o << ">\n";
            if (!dv.formula1.empty())
                o << "<formula1>" << xmlEscape(dv.formula1) << "</formula1>\n";
            if (!dv.formula2.empty())
                o << "<formula2>" << xmlEscape(dv.formula2) << "</formula2>\n";
            o << "</dataValidation>\n";
        }
        o << "</dataValidations>\n";
    }

    // drawing 必须排在 sheetData 之后、legacyDrawing 之前，否则 Excel 报部件顺序错误
    if (hasDrawing) o << "<drawing r:id=\"rId1\"/>\n";
    o << "</worksheet>";
    return o.str();
}

static std::string cellText(const XmlNode& c) {
    if (const XmlNode* is = c.child("is")) {
        std::string out;
        for (auto* t : is->children("t")) out += xmlUnescape(t->text);
        if (out.empty())
            for (auto* r : is->children("r"))
                if (const XmlNode* t = r->child("t")) out += xmlUnescape(t->text);
        return out;
    }
    if (const XmlNode* v = c.child("v")) return xmlUnescape(v->text);
    return std::string();
}


// 严格数值解析：整串必须是一个合法数字，不接受 "12abc" 这种部分匹配。
//
// std::stod 只吃前缀就返回成功，把 "12abc" 当成 12 —— 于是脏数据会
// 被静默改写成另一个值，用户根本发现不了。
static bool strictNumText(const std::string& t, Value& out) {
    if (t.empty()) return false;
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-') i++;
    bool digits = false, dot = false;
    for (; i < t.size(); i++) {
        char c = t[i];
        if (c >= '0' && c <= '9') { digits = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        if ((c == 'e' || c == 'E') && digits) {
            // 允许科学计数法
            size_t j = i + 1;
            if (j < t.size() && (t[j] == '+' || t[j] == '-')) j++;
            bool expDigits = false;
            for (; j < t.size(); j++) {
                if (t[j] >= '0' && t[j] <= '9') { expDigits = true; continue; }
                return false;
            }
            if (!expDigits) return false;
            i = t.size();
            break;
        }
        return false;
    }
    if (!digits) return false;
    try { out = Value::num(std::stod(t)); }
    catch (...) { return false; }
    return true;
}


// 严格数值解析：整串必须是一个合法数字，不接受 "12abc" 这种部分匹配。
//
// std::stod 只吃前缀就返回成功，把 "12abc" 当成 12 —— 于是脏数据会
// 被静默改写成另一个值，用户根本发现不了。

bool Workbook::loadSheetXml(Sheet& sh, const std::string& xmlText,
                            const std::vector<std::string>& shared,
                            const std::vector<std::string>& xfCodes,
                            const std::vector<CellStyle>& xfStyles,
                            int sheetIdx) {
    std::string perr;
    XmlNode root = xmlParse(xmlText, perr);
    if (!perr.empty()) { warnings_.push_back("工作表 XML 解析失败: " + perr); return false; }

    // ---- 视图属性：sheetViews / cols / autoFilter ----
    // 这些都是"读进来发现没有、用户立刻能察觉"的属性，不解析就等于丢失
    if (const XmlNode* svs = root.child("sheetViews"))
        if (const XmlNode* sv = svs->child("sheetView"))
            if (const XmlNode* pane = sv->child("pane")) {
                // 只有 state="frozen" 才是冻结窗格；
                // pane 元素也可能用于拆分窗口，不加这个判断会误判
                if (pane->attr("state") == "frozen") {
                    int xc = std::atoi(pane->attr("xSplit").c_str());
                    int yr = std::atoi(pane->attr("ySplit").c_str());
                    if (xc > 0 || yr > 0) setFreeze(sheetIdx, xc, yr);
                }
            }
    if (const XmlNode* cols = root.child("cols"))
        for (auto* c : cols->children("col")) {
            int lo = std::atoi(c->attr("min").c_str()) - 1;
            int hi = std::atoi(c->attr("max").c_str()) - 1;
            std::string w = c->attr("width");
            if (w.empty() || hi < lo) continue;
            try {
                double wv = std::stod(w);
                // 一个 <col min max> 覆盖一段连续列，要逐列展开
                for (int i = lo; i <= hi && i < 16384; i++) setColWidth(sheetIdx, i, wv);
            } catch (...) { /* 宽度非法就跳过，不影响其余内容 */ }
        }
    if (const XmlNode* af = root.child("autoFilter")) {
        std::string ref = af->attr("ref");
        size_t colon = ref.find(':');
        int c0, r0, c1, r1;
        bool ok = false;
        if (colon != std::string::npos)
            ok = parseCellRef(ref.substr(0, colon), c0, r0) &&
                 parseCellRef(ref.substr(colon + 1), c1, r1);
        else { ok = parseCellRef(ref, c0, r0); c1 = c0; r1 = r0; }
        if (ok) setAutoFilter(sheetIdx, c0, r0, c1, r1);
    }

    // dataValidations 与 sheetData 平级，且在 conditionalFormatting 之后
    for (auto* dvNode : root.children("dataValidations"))
        for (auto* d : dvNode->children("dataValidation"))
            parseDvNode(d, sheetIdx);

    // conditionalFormatting 同样与 sheetData 平级，必须单独读。
    // 注意是遍历 root 的**多个**同名子节点（一张表可以有多块条件格式），
    // 不能先 child() 取第一个再在其子节点里找同名节点 —— 那样一个都读不到，
    // 表现为"保存成功但读回来条件格式全丢"。
    for (auto* cfNode : root.children("conditionalFormatting"))
        parseCfNode(cfNode, sheetIdx);

    // mergeCells 与 sheetData 是兄弟节点，必须单独读，不能指望在 sheetData 里找到
    if (const XmlNode* mc = root.child("mergeCells"))
        for (auto* m : mc->children("mergeCell")) {
            std::string ref = m->attr("ref");
            size_t colon = ref.find(':');
            if (colon == std::string::npos) continue;
            int c0, r0, c1, r1;
            if (!parseCellRef(ref.substr(0, colon), c0, r0)) continue;
            if (!parseCellRef(ref.substr(colon + 1), c1, r1)) continue;
            addMerge(sheetIdx, c0, r0, c1, r1);
        }

    const XmlNode* sd = root.child("sheetData");
    if (!sd) return true;                      // 空表合法

    // ---- 共享公式：先预扫收集"主格" ----
    //
    // OOXML 里一块共享公式只写一次公式文本：
    //   主格   <f t="shared" si="0" ref="A1:A10">=B1*2</f>   带文本与 ref
    //   引用格 <f t="shared" si="0"/>                         只有 si，无文本
    //
    // 必须分两遍：引用格可能出现在主格之前（虽然 Excel 通常不这么写），
    // 单遍处理会查不到主格。
    //
    // 判别主格的唯一依据是"有没有 ref 属性"，不是 si 的值 ——
    // si="0" 既可能是主格也可能是引用格，用它判断会漏掉真正的引用格。
    struct SharedMaster {
        std::string text;
        int anchorCol = 0, anchorRow = 0;
    };
    std::map<std::string, SharedMaster> sharedMasters;
    for (auto* rowNode : sd->children("row"))
        for (auto* c : rowNode->children("c")) {
            const XmlNode* f = c->child("f");
            if (!f) continue;
            if (f->attr("t") != "shared") continue;
            std::string si = f->attr("si");
            std::string ftext = xmlUnescape(f->text);
            if (si.empty() || ftext.empty()) continue;      // 引用格，跳过
            std::string mref = f->attr("ref");
            SharedMaster m;
            m.text = ftext;
            // ref 形如 "A1:A10"，锚点是它的左上角；单个格则就是它自己
            {
                size_t colon = mref.find(':');
                std::string head = (colon == std::string::npos) ? mref : mref.substr(0, colon);
                int ac = 0, ar = 0;
                if (parseCellRef(head, ac, ar)) { m.anchorCol = ac; m.anchorRow = ar; }
                else if (!c->attr("r").empty()) parseCellRef(c->attr("r"), m.anchorCol, m.anchorRow);
            }
            sharedMasters[si] = m;
        }

    for (auto* rowNode : sd->children("row")) {
        // 行高挂在 row 属性上，且只有 customHeight="1" 才是用户显式设的
        {
            std::string ht = rowNode->attr("ht");
            if (!ht.empty() && rowNode->attr("customHeight") != "0") {
                int ridx = std::atoi(rowNode->attr("r").c_str()) - 1;
                if (ridx >= 0) {
                    try { setRowHeight(sheetIdx, ridx, std::stod(ht)); }
                    catch (...) { /* 非法值跳过 */ }
                }
            }
        }
        for (auto* c : rowNode->children("c")) {
            std::string ref = c->attr("r");
            int col = -1, row = -1;
            if (!ref.empty() && !parseCellRef(ref, col, row)) {
                // 无 r 属性：按行内顺序推算列
                warnings_.push_back("单元格缺少有效 r 属性，已跳过");
                continue;
            }
            if (col < 0 || row < 0) continue;

            // 数字格式：s 是 xf 索引，xf 再指向 numFmtId，映射在建表时已算好
            {
                std::string sAttr = c->attr("s");
                if (!sAttr.empty()) {
                    // s 直接就是 cellXfs 的下标，不要 -1。
                    // 读取端把 cellXfs 逐个解析成 xfCodes/xfStyles（索引 0 是 General），
                    // 与写入端的编号口径已经一致 —— 再减 1 就会整体错位，
                    // 表现为"所有格子的样式都跑到前一个格子上"。
                    int xf = std::atoi(sAttr.c_str());
                    if (xf > 0 && xf < (int)xfCodes.size()) {
                        if (!xfCodes[(size_t)xf].empty())
                            sh.setNumFmt(col, row, xfCodes[(size_t)xf]);
                        if (xf < (int)xfStyles.size() && !xfStyles[(size_t)xf].empty())
                            sh.setStyle(col, row, xfStyles[(size_t)xf]);
                    }
                }
            }

            std::string t = c->attr("t");
            std::string txt = cellText(*c);
            const XmlNode* f = c->child("f");

            // 注意外层条件不能写成 "f && !f->text.empty()"。
            // 共享公式的引用格就是 <f t="shared" si="0"/> —— 文本为空，
            // 那样写会把引用格整个跳过，落进常量分支，表现为"值对但公式没了"。
            if (f) {
                std::string ft = f->attr("t");
                std::string siIdx = f->attr("si");
                if (ft == "shared" && !siIdx.empty() && f->text.empty()) {
                    // 引用格：把主格公式按与锚点的偏移量平移后写入。
                    // 与"填充"、条件格式判定用的是同一套 refshift 规则。
                    auto it = sharedMasters.find(siIdx);
                    if (it != sharedMasters.end()) {
                        const SharedMaster& m = it->second;
                        int dc = col - m.anchorCol, dr = row - m.anchorRow;
                        std::string expanded = m.text;
                        if (dc != 0 || dr != 0) {
                            ShiftResult sr = shiftFormula(m.text, dc, dr);
                            expanded = sr.error.empty() ? sr.text : m.text;
                        }
                        std::string e2 = sh.setFormula(col, row, expanded);
                        if (!e2.empty() && warnings_.size() < 32)
                            warnings_.push_back("共享公式展开失败 " + ref + ": " + e2);
                        continue;
                    }
                    // 找不到主格（文件损坏或 si 不匹配）：降级成已算出的值，
                    // 并明确告知 —— 静默丢格子比留个常量更糟
                    if (warnings_.size() < 32)
                        warnings_.push_back("共享公式 si=" + siIdx + " 找不到主格，" + ref + " 保留为常量");
                    if (t == "e") {
                        sh.setValue(col, row, Value::error(Err::Value));
                    } else if (!txt.empty() && t != "s") {
                        Value nv;
                        if (strictNumText(txt, nv)) sh.setValue(col, row, nv);
                        else sh.setValue(col, row, Value::str(txt));
                    }
                    continue;
                }
                std::string e = sh.setFormula(col, row, f->text);
                if (!e.empty()) {
                    warnings_.push_back("公式解析失败 " + ref + ": " + e);
                    sh.setValue(col, row, Value::str(txt));
                }
                continue;
            }

            // 常量
            if (t == "s") {
                int idx = 0;
                try { idx = std::stoi(txt); } catch (...) { idx = -1; }
                if (idx >= 0 && idx < (int)shared.size())
                    sh.setValue(col, row, Value::str(shared[idx]));
                else
                    sh.setValue(col, row, Value::str(""));
            } else if (t == "inlineStr") {
                sh.setValue(col, row, Value::str(txt));
            } else if (t == "str") {
                sh.setValue(col, row, Value::str(txt));
            } else if (t == "b") {
                sh.setValue(col, row, Value::boolean(txt == "1" || txt == "true"));
            } else if (t == "e") {
                Err e2 = Err::Value;
                if (txt == "#DIV/0!") e2 = Err::Div0;
                else if (txt == "#REF!") e2 = Err::Ref;
                else if (txt == "#NAME?") e2 = Err::Name;
                else if (txt == "#NUM!") e2 = Err::Num;
                else if (txt == "#N/A") e2 = Err::NA;
                else if (txt == "#NULL!") e2 = Err::Null;
                else if (txt == "#CALC!") e2 = Err::Calc;
                else if (txt == "#SPILL!") e2 = Err::Spill;
                sh.setValue(col, row, Value::error(e2));
            } else {
                // n（默认）或缺失：数字
                if (txt.empty()) continue;             // 空单元格不占位
                try { sh.setValue(col, row, Value::num(std::stod(txt))); }
                catch (...) { sh.setValue(col, row, Value::str(txt)); }
            }
        }
    }
    return true;
}

const std::vector<std::vector<std::array<int,4>>>& Workbook::allMerges() const { return merges_; }

// 解析一个 <conditionalFormatting> 节点并登记到指定表。
// dxfId 指向 styles.xml 的 dxfs，所以真正的样式要等 styles.xml 解析完
// 才能回填 —— 见 applyDxfStyles()。
void Workbook::parseDvNode(const XmlNode* node, int sheetIdx) {
    if (!node) return;
    DataValidation dv;
    std::string ty = node->attr("type");
    if (!ty.empty() && !ooxmlToDvType(ty, dv.type)) return;
    std::string opStr = node->attr("operator");
    if (!opStr.empty()) ooxmlToDvOperator(opStr, dv.op);
    std::string es = node->attr("errorStyle");
    if (!es.empty()) ooxmlToDvErrorStyle(es, dv.errorStyle);

    dv.allowBlank = (node->attr("allowBlank") != "0");
    dv.showInputMessage = (node->attr("showInputMessage") == "1");
    dv.showErrorMessage = (node->attr("showErrorMessage") != "0");
    dv.promptTitle = xmlUnescape(node->attr("promptTitle"));
    dv.prompt      = xmlUnescape(node->attr("prompt"));
    dv.errorTitle  = xmlUnescape(node->attr("errorTitle"));
    dv.error       = xmlUnescape(node->attr("error"));

    std::string sqref = node->attr("sqref");
    {
        std::string tok;
        for (size_t i = 0; i <= sqref.size(); i++) {
            char ch = (i < sqref.size()) ? sqref[i] : ' ';
            if (ch == ' ') {
                if (!tok.empty()) {
                    int c0, r0, c1, r1;
                    size_t colon = tok.find(':');
                    bool ok = false;
                    if (colon != std::string::npos)
                        ok = parseCellRef(tok.substr(0, colon), c0, r0) &&
                             parseCellRef(tok.substr(colon + 1), c1, r1);
                    else { ok = parseCellRef(tok, c0, r0); c1 = c0; r1 = r0; }
                    if (ok) dv.rects.push_back({{std::min(c0,c1), std::min(r0,r1),
                                                 std::max(c0,c1), std::max(r0,r1)}});
                    tok.clear();
                }
            } else tok += ch;
        }
    }
    if (dv.rects.empty()) return;

    for (auto* f : node->children("formula1")) dv.formula1 = xmlUnescape(f->text);
    for (auto* f : node->children("formula2")) dv.formula2 = xmlUnescape(f->text);
    if (dv.type == DvType::List && !dv.formula1.empty() && dv.formula1.front() == '"')
        dv.listItems = splitListFormula(dv.formula1);

    addDv(sheetIdx, dv);
}

void Workbook::parseCfNode(const XmlNode* node, int sheetIdx) {
    if (!node) return;
    ConditionalFormat cf;

    // sqref 可以是多个区域，用空格分隔："A1:A10 C1:C5"
    std::string sqref = node->attr("sqref");
    {
        std::string tok;
        for (size_t i = 0; i <= sqref.size(); i++) {
            char ch = (i < sqref.size()) ? sqref[i] : ' ';
            if (ch == ' ') {
                if (!tok.empty()) {
                    int c0, r0, c1, r1;
                    size_t colon = tok.find(':');
                    bool ok = false;
                    if (colon != std::string::npos) {
                        ok = parseCellRef(tok.substr(0, colon), c0, r0) &&
                             parseCellRef(tok.substr(colon + 1), c1, r1);
                    } else {
                        ok = parseCellRef(tok, c0, r0);
                        c1 = c0; r1 = r0;
                    }
                    if (ok) {
                        cf.rects.push_back({{std::min(c0,c1), std::min(r0,r1),
                                             std::max(c0,c1), std::max(r0,r1)}});
                    }
                    tok.clear();
                }
            } else tok += ch;
        }
    }
    if (cf.rects.empty()) return;

    int prio = 1;
    for (auto* r : node->children("cfRule")) {
        CfRule rule;
        std::string ty = r->attr("type");
        if (!ooxmlToCfType(ty, rule.type)) continue;    // 未支持的类型整体跳过
        std::string opStr = r->attr("operator");
        if (!opStr.empty()) ooxmlToCfOperator(opStr, rule.op);
        int p = std::atoi(r->attr("priority").c_str());
        rule.priority = (p > 0) ? p : prio++;
        rule.stopIfTrue = (r->attr("stopIfTrue") == "1");
        if (rule.type == CfType::Top10) {
            rule.bottom  = (r->attr("bottom") == "1");
            rule.percent = (r->attr("percent") == "1");
            int rk = std::atoi(r->attr("rank").c_str());
            rule.rank = (rk > 0) ? rk : 10;
        }
        if (rule.type == CfType::AboveAverage)
            rule.above = (r->attr("aboveAverage") != "0");
        for (auto* f : r->children("formula")) rule.formulas.push_back(xmlUnescape(f->text));

        // dxfId 先记下来，样式等 styles.xml 解析完再回填
        int dxfId = -1;
        std::string dx = r->attr("dxfId");
        if (!dx.empty()) dxfId = std::atoi(dx.c_str());
        pendingDxf_.push_back({sheetIdx, (int)cfs_.size(), (int)cf.rules.size(), dxfId});
        cf.rules.push_back(rule);
    }
    if (cf.rules.empty()) return;
    addCf(sheetIdx, cf);
}

// styles.xml 解析完后，把 dxfId 对应的样式回填到各条规则上
void Workbook::applyDxfStyles(const std::vector<CfStyle>& dxfs) {
    for (auto& p : pendingDxf_) {
        if (p.dxfId < 0 || p.dxfId >= (int)dxfs.size()) continue;
        if (p.sheet >= (int)cfs_.size()) continue;
        auto& list = cfs_[(size_t)p.sheet];
        if (p.cfIndex >= (int)list.size()) continue;
        auto& rules = list[(size_t)p.cfIndex].rules;
        if (p.ruleIndex >= (int)rules.size()) continue;
        rules[(size_t)p.ruleIndex].style = dxfs[(size_t)p.dxfId];
    }
    pendingDxf_.clear();
}

// ---------------------------------------------------------------------------
// 条件格式
// ---------------------------------------------------------------------------
static void growCf(std::vector<std::vector<ConditionalFormat>>& v, size_t n) {
    while (v.size() <= n) v.emplace_back();
}

void Workbook::addCf(int sheetIdx, const ConditionalFormat& cf) {
    if (sheetIdx < 0) return;
    growCf(cfs_, (size_t)sheetIdx);
    cfs_[(size_t)sheetIdx].push_back(cf);
}

void Workbook::clearCfs(int sheetIdx) {
    if (sheetIdx >= 0 && sheetIdx < (int)cfs_.size()) cfs_[(size_t)sheetIdx].clear();
}



// ---------------------------------------------------------------------------
// 增量重算（工作簿级）
// ---------------------------------------------------------------------------
void Workbook::recalcDirty() {
    // 逐表增量重算。某表算完后，把"引用了它"的其它表标脏。
    //
    // 顺序问题：这里按表序单趟处理，若 A 引用 B 而 B 在 A 之后才算，
    // A 这一轮会用到 B 的旧值。但 B 算完后会标脏 A，下一轮再算 A。
    // 所以跑两趟：第二趟消化跨表传播出来的脏。
    // （单表内不存在这个问题 —— 递归求值会把前驱先算掉。）
    // 收敛循环而不是固定两趟：跨表依赖可能成链（A 引用 B，B 引用 C），
    // 固定趟数会在链长超过趟数时留下脏格子，表现为"跨表的数偶尔不更新"。
    // 上限设一个较大的值，正常情况下 1~2 趟就收敛。
    const int kMaxPass = 8;
    for (int pass = 0; pass < kMaxPass; pass++) {
        bool anyDirty = false;
        for (size_t i = 0; i < sheets_.size(); i++) {
            Sheet* sh = sheets_[i].sheet.get();
            if (!sh) continue;
            if (sh->dirtyCount() == 0) continue;
            anyDirty = true;
            sh->recalcDirty();
            // 本表算完后，把"引用了本表"的其它表标脏
            for (size_t j = 0; j < sheets_.size(); j++) {
                if (j == i) continue;
                if (sheets_[j].sheet) sheets_[j].sheet->invalidateSheetRef(sh->name());
            }
        }
        // 再检查一轮：若跨表传播又弄脏了谁，继续
        bool stillDirty = false;
        for (auto& sr : sheets_)
            if (sr.sheet && sr.sheet->dirtyCount() > 0) { stillDirty = true; break; }
        if (!anyDirty || !stillDirty) break;
    }
}

// ---------------------------------------------------------------------------
// 定义名称（命名区域）
// ---------------------------------------------------------------------------
//
// 存在 workbook.xml 的 <definedNames> 里，公式里可以直接写名字：
//   =SUM(销售额)         而不是 =SUM(B2:B5)
//
// 与 LET 是两套机制：LET 是公式内部的临时绑定，定义名称是持久的、全表可见的。
// 求值走 eval.cpp 的 NodeKind::Name 分支，先查 LET、再查这里。
namespace {

// 名字合法性。Excel 的限制比"非空"严格得多，这里守住几条会出问题的：
//   - 空
//   - 首字符是数字（会被词法器当数字/出错）
//   - 长得像单元格地址（A1 / AB12）—— 词法器永远产出 Ref，名字根本不会被查到
//   - 是布尔字面量（TRUE/FALSE 是保留的）
//   - 含非法字符（空格、运算符等）
bool validDefinedName(const std::string& name, std::string& why) {
    if (name.empty()) { why = "名称不能为空"; return false; }
    char c0 = name[0];
    if (!(std::isalpha((unsigned char)c0) || c0 == '_' || (unsigned char)c0 >= 0x80)) {
        why = "首字符必须是字母或下划线（不能是数字）"; return false;
    }
    for (char c : name) {
        bool ok = std::isalnum((unsigned char)c) || c == '_' || c == '.'
                  || (unsigned char)c >= 0x80;
        if (!ok) { why = "名称只能含字母、数字、下划线和点（不能有空格或运算符）"; return false; }
    }
    // 像单元格地址：字母开头 + 数字结尾，且中间只有字母
    {
        size_t i = 0;
        while (i < name.size() && std::isalpha((unsigned char)name[i])) i++;
        size_t j = i;
        while (j < name.size() && std::isdigit((unsigned char)name[j])) j++;
        if (i > 0 && i < name.size() && j == name.size()) {
            why = "名称不能像单元格地址（如 A1）"; return false;
        }
    }
    std::string up = name;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    if (up == "TRUE" || up == "FALSE") { why = "不能使用保留字 " + up; return false; }
    if (up == "R" || up == "C") { why = "单字母 R/C 是 R1C1 引用样式保留字"; return false; }
    return true;
}

} // namespace

bool Workbook::addDefinedName(const std::string& name, const std::string& refersTo,
                              std::string& why) {
    why.clear();
    std::string w;
    if (!validDefinedName(name, w)) { why = w; return false; }
    if (refersTo.empty()) { why = "引用目标不能为空"; return false; }

    // 同名覆盖：Excel 的语义是"重新定义"，不是报错
    for (auto& kv : definedNames_)
        if (kv.first == name) {
            kv.second = refersTo;
            invalidateNameRefs(name);      // 引用它的公式必须重算
            return true;
        }
    definedNames_.emplace_back(name, refersTo);
    return true;
}

bool Workbook::removeDefinedName(const std::string& name) {
    for (size_t i = 0; i < definedNames_.size(); i++)
        if (definedNames_[i].first == name) {
            definedNames_.erase(definedNames_.begin() + (long)i);
            invalidateNameRefs(name);
            return true;
        }
    return false;
}

void Workbook::clearDefinedNames() { definedNames_.clear(); }

const std::string* Workbook::findDefinedName(const std::string& name) const {
    for (const auto& kv : definedNames_)
        if (kv.first == name) return &kv.second;
    return nullptr;
}

void Workbook::invalidateNameRefs(const std::string& name) {
    for (auto& sr : sheets_)
        if (sr.sheet) sr.sheet->invalidateName(name);
}

bool Workbook::evalDefinedName(const std::string& name, Value& out, Sheet* ctxSheet) {
    const std::string* def = findDefinedName(name);
    if (!def) return false;

    // 递归保护：名称 A 引用名称 B、B 又引用 A 会无限下潜。
    // 这里限制展开层数，超限给 #NAME? 而不是让栈爆掉。
    if (dnDepth_ >= 32) { out = Value::error(Err::Name); return true; }

    if (!ctxSheet) { out = Value::error(Err::Ref); return true; }

    // 定义文本可能带前导 '='（Excel 保存时常常这么写），先剥掉
    std::string txt = *def;
    size_t i = 0;
    while (i < txt.size() && std::isspace((unsigned char)txt[i])) i++;
    if (i < txt.size() && txt[i] == '=') i++;
    txt = txt.substr(i);

    dnDepth_++;
    Value v = ctxSheet->evalExprAt(txt, ctxSheet->curCol(), ctxSheet->curRow());
    dnDepth_--;
    out = v;
    return true;
}

// ---------------------------------------------------------------------------
// 嵌入图片
// ---------------------------------------------------------------------------
static void growImage(std::vector<std::vector<ImagePart>>& v, size_t n) {
    while (v.size() <= n) v.emplace_back();
}

void Workbook::addImage(int sheetIdx, const ImagePart& img) {
    if (sheetIdx < 0 || img.data.empty()) return;
    growImage(images_, (size_t)sheetIdx);
    images_[(size_t)sheetIdx].push_back(img);
}

void Workbook::clearImages(int sheetIdx) {
    if (sheetIdx >= 0 && sheetIdx < (int)images_.size()) images_[(size_t)sheetIdx].clear();
}

// ---------------------------------------------------------------------------
// 视图属性（列宽/行高/冻结/筛选）
// ---------------------------------------------------------------------------
static void growLayout(std::vector<SheetLayout>& v, size_t n) {
    while (v.size() <= n) v.emplace_back();
}

SheetLayout& Workbook::layout(int sheetIdx) {
    if (sheetIdx < 0) sheetIdx = 0;
    growLayout(layouts_, (size_t)sheetIdx);
    return layouts_[(size_t)sheetIdx];
}

const SheetLayout& Workbook::layout(int sheetIdx) const {
    static const SheetLayout kEmpty;
    if (sheetIdx < 0 || sheetIdx >= (int)layouts_.size()) return kEmpty;
    return layouts_[(size_t)sheetIdx];
}

void Workbook::setColWidth(int sheetIdx, int col, double w) {
    if (col < 0) return;
    // 宽度取正值：0 或负数会让 Excel 把列压成不可见
    layout(sheetIdx).colWidths[col] = (w > 0.01) ? w : 0.01;
}

void Workbook::setRowHeight(int sheetIdx, int row, double h) {
    if (row < 0) return;
    layout(sheetIdx).rowHeights[row] = (h > 0.01) ? h : 0.01;
}

void Workbook::setFreeze(int sheetIdx, int frozenCols, int frozenRows) {
    SheetLayout& L = layout(sheetIdx);
    L.freeze.frozenCols = std::max(0, frozenCols);
    L.freeze.frozenRows = std::max(0, frozenRows);
    L.freeze.enabled = (L.freeze.frozenCols > 0 || L.freeze.frozenRows > 0);
}

void Workbook::setAutoFilter(int sheetIdx, int c0, int r0, int c1, int r1) {
    SheetLayout& L = layout(sheetIdx);
    L.filter.enabled = true;
    L.filter.c0 = std::min(c0, c1); L.filter.c1 = std::max(c0, c1);
    L.filter.r0 = std::min(r0, r1); L.filter.r1 = std::max(r0, r1);
}

void Workbook::clearAutoFilter(int sheetIdx) {
    layout(sheetIdx).filter = AutoFilter();
}

// ---------------------------------------------------------------------------
// 视图属性（列宽/行高/冻结/筛选）
// ---------------------------------------------------------------------------








// ---------------------------------------------------------------------------
// 批注
// ---------------------------------------------------------------------------
static void growNote(std::vector<std::vector<CellNote>>& v, size_t n) {
    while (v.size() <= n) v.emplace_back();
}

void Workbook::addNote(int sheetIdx, const CellNote& n) {
    if (sheetIdx < 0) return;
    growNote(notes_, (size_t)sheetIdx);
    auto& list = notes_[(size_t)sheetIdx];
    // 同一格只保留一条：重复 addNote 是"修改"而不是"追加"
    for (auto& e : list)
        if (e.col == n.col && e.row == n.row) { e = n; return; }
    list.push_back(n);
}

bool Workbook::removeNote(int sheetIdx, int col, int row) {
    if (sheetIdx < 0 || sheetIdx >= (int)notes_.size()) return false;
    auto& list = notes_[(size_t)sheetIdx];
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].col == col && list[i].row == row) {
            list.erase(list.begin() + (long)i);
            return true;
        }
    return false;
}

void Workbook::clearNotes(int sheetIdx) {
    if (sheetIdx >= 0 && sheetIdx < (int)notes_.size()) notes_[(size_t)sheetIdx].clear();
}

const CellNote* Workbook::noteAt(int sheetIdx, int col, int row) const {
    if (sheetIdx < 0 || sheetIdx >= (int)notes_.size()) return nullptr;
    for (const CellNote& n : notes_[(size_t)sheetIdx])
        if (n.col == col && n.row == row) return &n;
    return nullptr;
}

// ---------------------------------------------------------------------------
// 数据验证
// ---------------------------------------------------------------------------
static void growDv(std::vector<std::vector<DataValidation>>& v, size_t n) {
    while (v.size() <= n) v.emplace_back();
}

void Workbook::addDv(int sheetIdx, const DataValidation& dv) {
    if (sheetIdx < 0 || dv.rects.empty()) return;
    growDv(dvs_, (size_t)sheetIdx);
    dvs_[(size_t)sheetIdx].push_back(dv);
}

void Workbook::clearDvs(int sheetIdx) {
    if (sheetIdx >= 0 && sheetIdx < (int)dvs_.size()) dvs_[(size_t)sheetIdx].clear();
}

// ---------------------------------------------------------------------------
// 合并单元格
// ---------------------------------------------------------------------------
static void growTo(std::vector<std::vector<std::array<int,4>>>& v, size_t n) {
    while (v.size() <= n) v.emplace_back();
}

void Workbook::addMerge(int sheetIdx, int c0, int r0, int c1, int r1) {
    if (sheetIdx < 0 || c0 < 0 || r0 < 0) return;
    //  normalize：允许反向拖动选区产生的 c0>c1
    // 先保证该表的容器存在，再判 1x1。
    // 顺序反了的话，只加过 1x1 区域的表在 allMerges()[i] 查询时会越界。
    growTo(merges_, (size_t)sheetIdx);
    int a0 = std::min(c0, c1), a1 = std::max(c0, c1);
    int b0 = std::min(r0, r1), b1 = std::max(r0, r1);
    if (a0 == a1 && b0 == b1) return;                 // 1x1 不算合并
    // 重叠的合并区域在 Excel 里非法，先清掉相交的旧区域
    auto& v = merges_[(size_t)sheetIdx];
    std::vector<std::array<int,4>> keep;
    for (auto& m : v) {
        bool overlap = !(m[1] > b1 || m[3] < b0 || m[0] > a1 || m[2] < a0);
        if (!overlap) keep.push_back(m);
    }
    v = keep;
    v.push_back({{a0, b0, a1, b1}});
}

void Workbook::clearMerges(int sheetIdx) {
    if (sheetIdx >= 0 && sheetIdx < (int)merges_.size()) merges_[(size_t)sheetIdx].clear();
}

bool Workbook::mergeAnchorOf(int sheetIdx, int col, int row, int& ac, int& ar) const {
    if (sheetIdx < 0 || sheetIdx >= (int)merges_.size()) return false;
    for (auto& m : merges_[(size_t)sheetIdx])
        if (col >= m[0] && col <= m[2] && row >= m[1] && row <= m[3]) {
            ac = m[0]; ar = m[1];
            return true;
        }
    return false;
}

bool Workbook::isMergedAway(int sheetIdx, int col, int row) const {
    int ac = 0, ar = 0;
    return mergeAnchorOf(sheetIdx, col, row, ac, ar) && (ac != col || ar != row);
}

std::pair<int,int> Workbook::mergeSpan(int sheetIdx, int col, int row) const {
    if (sheetIdx < 0 || sheetIdx >= (int)merges_.size()) return {1, 1};
    for (auto& m : merges_[(size_t)sheetIdx])
        if (col >= m[0] && col <= m[2] && row >= m[1] && row <= m[3])
            return {m[2] - m[0] + 1, m[3] - m[1] + 1};
    return {1, 1};
}

// ---------------------------------------------------------------------------
// 保存
// ---------------------------------------------------------------------------
bool Workbook::save(const std::string& path, std::string& err) const {
    warnings_.clear();
    ZipWriter zw;
    std::vector<std::string> sharedStrings;
    std::map<std::string, int> ssIndex;

    // 图片扩展名必须先扫一遍：Content_Types 在部件写入之前就输出了，
    // 而 Default 是按扩展名登记的，不能等到写 media 时才知道用了哪些类型。
    imageTypesUsed_.clear();
    for (const std::vector<ImagePart>& v : images_)
        for (const ImagePart& im : v)
            if (!im.data.empty() && !imageContentType(im.ext).empty())
                imageTypesUsed_.insert(im.ext);

    // 先收集全部数字格式：单元格靠 xf 索引引用它，索引必须先定下来
    StyleSet st = buildStyleSet();
    DxfSet dxfs;

    // 先生成各 sheet（过程中填充共享字符串表）
    std::vector<std::string> sheetXmls;
    for (size_t i = 0; i < sheets_.size(); i++) {
        bool hasDrawing = ((i < charts_.size()) && !charts_[i].empty())
                       || ((i < images_.size()) && !images_[i].empty());
        std::vector<std::array<int,4>> mr;
        if (i < merges_.size()) mr = merges_[i];
        std::vector<ConditionalFormat> cfl;
        if (i < cfs_.size()) cfl = cfs_[i];
        std::vector<DataValidation> dvl;
        if (i < dvs_.size()) dvl = dvs_[i];
        const SheetLayout& lay = layout((int)i);
        sheetXmls.push_back(buildSheetXml(*sheets_[i].sheet, sharedStrings, ssIndex, st,
                                          mr, cfl, dxfs, dvl, lay, hasDrawing));
    }

    zw.addFileStr("[Content_Types].xml", buildContentTypes());
    zw.addFileStr("_rels/.rels", buildRootRels());
    zw.addFileStr("xl/workbook.xml", buildWorkbookXml());
    zw.addFileStr("xl/_rels/workbook.xml.rels", buildWorkbookRels());
    zw.addFileStr("xl/styles.xml", buildStyles(st, dxfs));
    zw.addFileStr("xl/sharedStrings.xml", buildSharedStrings(sharedStrings));
    for (size_t i = 0; i < sheetXmls.size(); i++)
        zw.addFileStr("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml", sheetXmls[i]);

    // 图表与图片：两者共用一个 drawing 部件，必须统一生成。
    // 原先只有图表才生成 drawing，于是"只有图片、没有图表"的表
    // 根本不产出 drawing，图片全部静默丢失。
    int chartSeq = 0;
    int mediaSeq = 0;                       // media 全局编号，跨表递增
    for (size_t i = 0; i < sheets_.size(); i++) {
        bool hasChart = (i < charts_.size()) && !charts_[i].empty();
        bool hasImage = (i < images_.size()) && !images_[i].empty();
        if (!hasChart && !hasImage) continue;
        int drawingId = (int)i + 1;

        std::vector<ChartPart> parts;
        if (hasChart)
            for (size_t k = 0; k < charts_[i].size(); k++) {
                chartSeq++;
                std::string cerr;
                std::string cx = buildChartXml(charts_[i][k], cerr);
                if (!cerr.empty())
                    warnings_.push_back("图表 " + std::to_string(chartSeq) + ": " + cerr);
                zw.addFileStr("xl/charts/chart" + std::to_string(chartSeq) + ".xml", cx);
                ChartPart p;
                p.chart = charts_[i][k];
                p.sheetIndex = (int)i;
                p.title = (i < chartTitles_.size() && k < chartTitles_[i].size())
                            ? chartTitles_[i][k] : ("图表 " + std::to_string(k + 1));
                parts.push_back(p);
            }

        // 图片本体写进 media，并记下 rels 里要用的 Target
        std::vector<std::string> imgTargets;
        if (hasImage)
            for (const ImagePart& im : images_[i]) {
                if (im.data.empty()) {
                    warnings_.push_back("图片数据为空，已跳过");
                    continue;
                }
                std::string ct = imageContentType(im.ext);
                if (ct.empty()) {
                    warnings_.push_back("不支持的图片类型: " + im.ext + "，已跳过");
                    continue;
                }
                mediaSeq++;
                std::string fname = "image" + std::to_string(mediaSeq) + "." + im.ext;
                zw.addFile("xl/media/" + fname, im.data);
                imgTargets.push_back("../media/" + fname);
                imageTypesUsed_.insert(im.ext);       // 供 ContentTypes 登记 Default
            }

        std::string derr;
        zw.addFileStr("xl/drawings/drawing" + std::to_string(drawingId) + ".xml",
                      buildDrawingXml(parts, hasImage ? images_[i] : std::vector<ImagePart>(), derr));
        if (!derr.empty()) warnings_.push_back(derr);
        zw.addFileStr("xl/drawings/_rels/drawing" + std::to_string(drawingId) + ".xml.rels",
                      buildDrawingRels((int)parts.size(), (int)imgTargets.size(), &imgTargets));
    }

    // 批注部件
    for (size_t i = 0; i < notes_.size(); i++) {
        if (notes_[i].empty()) continue;
        zw.addFileStr("xl/comments" + std::to_string(i + 1) + ".xml",
                      buildCommentsXml(notes_[i]));
    }

    // sheetN.xml.rels 必须统一生成：
    // 原先只在有 drawing 时才写，于是"只有批注、没有图表"的表根本不生成 rels，
    // 批注就全丢了 —— 这类问题自测很难发现，因为不报错，只是读回来什么都没有。
    for (size_t i = 0; i < sheets_.size(); i++) {
        bool hasDrawing = ((i < charts_.size()) && !charts_[i].empty())
                       || ((i < images_.size()) && !images_[i].empty());
        bool hasNotes   = (i < notes_.size())  && !notes_[i].empty();
        if (!hasDrawing && !hasNotes) continue;
        int rid = 1;
        std::ostringstream o;
        o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
        if (hasDrawing)
            o << "<Relationship Id=\"rId" << rid++ << "\" "
              << "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" "
              << "Target=\"../drawings/drawing" << (i + 1) << ".xml\"/>\n";
        if (hasNotes)
            o << "<Relationship Id=\"rId" << rid++ << "\" "
              << "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments\" "
              << "Target=\"../comments" << (i + 1) << ".xml\"/>\n";
        o << "</Relationships>";
        zw.addFileStr("xl/worksheets/_rels/sheet" + std::to_string(i + 1) + ".xml.rels", o.str());
    }

    return zw.writeTo(path, err);
}

bool Workbook::load(const std::string& path, std::string& err) {
    warnings_.clear();
    // 定义名称在解析 <sheets> 之后就填充，而"逐表加载"那一步还会重置
    // images_/notes_ 等容器。必须在这里统一清一次 ——
    // 若把 clear 放进后面的重置块，刚解析出来的名称会被立刻清掉，
    // 表现为"存盘时写了，读回来是空的"，且不报任何错。
    definedNames_.clear();
    dnDepth_ = 0;
    ZipReader zr;
    if (!zr.open(path, err)) return false;

    // 1) 共享字符串
    std::vector<std::string> shared;
    std::string sstXml;
    if (zr.readText("xl/sharedStrings.xml", sstXml)) {
        std::string perr;
        XmlNode root = xmlParse(sstXml, perr);
        if (perr.empty()) {
            for (auto* si : root.children("si")) {
                std::string s;
                for (auto* t : si->children("t")) s += t->text;
                shared.push_back(s);
            }
        } else warnings_.push_back("sharedStrings 解析失败");
    }

    // 2) workbook.xml：表名与 rId
    std::string wbXml;
    if (!zr.readText("xl/workbook.xml", wbXml)) {
        // 有些文件用非标准路径，退回按 workbook 关键词搜索
        for (auto& e : zr.entries())
            if (e.name.find("workbook.xml") != std::string::npos && e.name.find("_rels") == std::string::npos) {
                wbXml.assign(e.data.begin(), e.data.end()); break;
            }
    }
    if (wbXml.empty()) { err = "未找到 xl/workbook.xml"; return false; }

    std::string perr;
    XmlNode wbRoot = xmlParse(wbXml, perr);
    if (!perr.empty()) { err = "workbook.xml 解析失败: " + perr; return false; }
    const XmlNode* sheetsNode = wbRoot.child("sheets");
    if (!sheetsNode) { err = "workbook.xml 缺少 <sheets>"; return false; }
    std::vector<std::pair<std::string,std::string>> sheetDefs;   // name -> rId
    for (auto* s : sheetsNode->children("sheet")) {
        sheetDefs.push_back({s->attr("name"), s->attr("id")});
    }
    if (sheetDefs.empty()) { err = "工作簿中没有工作表"; return false; }

    // 2.5) 定义名称。<definedName> 带 localSheetId 的是表级作用域，这里只认全局的；
    // 表级名称语义不同（同名在不同表可有不同定义），支持它需要给每个名字再记一张
    // 表名映射，暂不实现 —— 但至少要跳过它，不能误当成全局名读进来。
    if (const XmlNode* dns = wbRoot.child("definedNames")) {
        for (auto* d : dns->children("definedName")) {
            std::string nm = xmlUnescape(d->attr("name"));
            std::string ref = xmlUnescape(d->text);
            if (nm.empty() || ref.empty()) continue;
            if (!d->attr("localSheetId").empty()) {
                warnings_.push_back("表级定义名称 '" + nm + "' 未导入（仅支持全局名称）");
                continue;
            }
            std::string why;
            // 名字非法时不能静默丢弃 —— 那就等于悄悄改了用户的数据
            if (!addDefinedName(nm, ref, why))
                warnings_.push_back("定义名称 '" + nm + "' 无效: " + why);
        }
    }

    // 3) workbook.xml.rels：rId -> target
    std::map<std::string,std::string> rels;
    std::string relsXml;
    if (zr.readText("xl/_rels/workbook.xml.rels", relsXml)) {
        std::string rerr;
        XmlNode rr = xmlParse(relsXml, rerr);
        if (rerr.empty()) {
            for (auto* r : rr.children("Relationship")) {
                std::string id = r->attr("Id");
                std::string tgt = r->attr("Target");
                if (tgt.compare(0, 3, "xl/") != 0 && tgt[0] != '/')
                    tgt = "xl/" + tgt;                 // 相对路径基于 xl/
                else if (tgt[0] == '/')
                    tgt = tgt.substr(1);
                rels[id] = tgt;
            }
        }
    }

    // 3.5) 样式表：numFmtId -> 格式码，再建立 xf 索引 -> 格式码
    std::vector<std::string> xfCodes;
    std::vector<CellStyle> xfStyles;
    std::vector<CfStyle> dxfs;          // 条件格式样式（cfRule 通过 dxfId 引用）
    {
        std::string stx;
        if (zr.readText("xl/styles.xml", stx)) {
            std::string serr;
            XmlNode sroot = xmlParse(stx, serr);
            if (serr.empty()) {
                std::map<int, std::string> fmtById;          // numFmtId -> 格式码
                if (const XmlNode* nfs = sroot.child("numFmts"))
                    for (auto* n : nfs->children("numFmt")) {
                        int id = std::atoi(n->attr("numFmtId").c_str());
                        std::string code = xmlUnescape(n->attr("formatCode"));
                        if (!code.empty()) fmtById[id] = code;
                    }
                // fonts / fills / borders 先建表，xf 再按 id 引用它们
                std::vector<CellStyle> fonts;
                if (const XmlNode* fs = sroot.child("fonts"))
                    for (auto* f : fs->children("font")) {
                        CellStyle cs;
                        if (f->child("b"))      cs.bold = true;
                        if (f->child("i"))      cs.italic = true;
                        if (f->child("u"))      cs.underline = true;
                        if (f->child("strike")) cs.strike = true;
                        if (const XmlNode* sz = f->child("sz")) {
                            int v = std::atoi(sz->attr("val").c_str());
                            if (v > 0) cs.fontSize = v;
                        }
                        if (const XmlNode* cl = f->child("color")) {
                            std::string rgb = cl->attr("rgb");
                            if (rgb.empty()) rgb = cl->attr("theme").empty() ? "" : "";
                            if (!rgb.empty()) cs.fontColor = normalizeColor(rgb);
                        }
                        if (const XmlNode* nm = f->child("name"))
                            cs.fontName = xmlUnescape(nm->attr("val"));
                        fonts.push_back(cs);
                    }
                std::vector<std::string> fills;      // 前两个被 OOXML 占死
                if (const XmlNode* fls = sroot.child("fills"))
                    for (auto* fl : fls->children("fill")) {
                        std::string col;
                        if (const XmlNode* pf = fl->child("patternFill")) {
                            std::string pt = pf->attr("patternType");
                            if (pt == "solid") {
                                if (const XmlNode* fg = pf->child("fgColor"))
                                    col = normalizeColor(fg->attr("rgb"));
                            }
                        }
                        fills.push_back(col);
                    }
                std::vector<CellStyle> borders;
                if (const XmlNode* bs = sroot.child("borders"))
                    for (auto* b : bs->children("border")) {
                        CellStyle cs;
                        if (const XmlNode* side = b->child("left")) {
                            BorderStyle bst;
                            if (ooxmlToBorder(side->attr("style"), bst)) cs.border = bst;
                            if (const XmlNode* cl = side->child("color"))
                                cs.borderColor = normalizeColor(cl->attr("rgb"));
                        }
                        borders.push_back(cs);
                    }

                // dxfs：条件格式引用的样式。排在 cellStyles 之后。
                // 必须在这里读出来，cfRule 只存了 dxfId。
                if (const XmlNode* dx = sroot.child("dxfs"))
                    for (auto* d : dx->children("dxf")) {
                        CfStyle cs;
                        if (const XmlNode* f = d->child("font")) {
                            if (f->child("b")) cs.bold = true;
                            if (f->child("i")) cs.italic = true;
                            if (const XmlNode* cl = f->child("color"))
                                cs.fontColor = normalizeColor(cl->attr("rgb"));
                        }
                        // 条件格式的填充色写在 patternFill/bgColor 上（不是 fgColor）
                        if (const XmlNode* fl = d->child("fill"))
                            if (const XmlNode* pf = fl->child("patternFill"))
                                if (const XmlNode* bg = pf->child("bgColor"))
                                    cs.fillColor = normalizeColor(bg->attr("rgb"));
                        dxfs.push_back(cs);
                    }

                if (const XmlNode* xfs = sroot.child("cellXfs"))
                    for (auto* x : xfs->children("xf")) {
                        int id = std::atoi(x->attr("numFmtId").c_str());
                        auto it = fmtById.find(id);
                        if (it != fmtById.end()) xfCodes.push_back(it->second);
                        else {
                            const char* b = builtinNumFmt(id);
                            // 内建 ID 0 是 General —— 存空串，让 display 走通用显示
                            xfCodes.push_back((b && id != 0) ? b : std::string());
                        }
                        CellStyle cs;
                        int fi = std::atoi(x->attr("fontId").c_str());
                        if (fi > 0 && fi < (int)fonts.size()) {
                            CellStyle f = fonts[(size_t)fi];
                            cs.bold = f.bold; cs.italic = f.italic;
                            cs.underline = f.underline; cs.strike = f.strike;
                            cs.fontSize = f.fontSize; cs.fontColor = f.fontColor;
                            cs.fontName = f.fontName;
                        }
                        int lli = std::atoi(x->attr("fillId").c_str());
                        if (lli >= 2 && lli < (int)fills.size()) cs.fillColor = fills[(size_t)lli];
                        int bi = std::atoi(x->attr("borderId").c_str());
                        if (bi > 0 && bi < (int)borders.size()) {
                            cs.border = borders[(size_t)bi].border;
                            cs.borderColor = borders[(size_t)bi].borderColor;
                        }
                        if (const XmlNode* al = x->child("alignment")) {
                            HAlign ha; VAlign va;
                            if (ooxmlToHAlign(al->attr("horizontal"), ha)) cs.hAlign = ha;
                            if (ooxmlToVAlign(al->attr("vertical"), va))   cs.vAlign = va;
                            cs.wrapText = (al->attr("wrapText") == "1");
                        }
                        xfStyles.push_back(cs);
                    }
            } else {
                warnings_.push_back("styles.xml 解析失败: " + serr);
            }
        }
    }

    // 4) 逐表加载
    sheets_.clear();
    charts_.clear();
    chartTitles_.clear();
    merges_.clear();
    cfs_.clear();
    dvs_.clear();
    notes_.clear();
    layouts_.clear();
    images_.clear();
    imageTypesUsed_.clear();
    dvs_.clear();
    std::vector<bool> sheetHasDrawing;
    for (auto& def : sheetDefs) {
        std::string part = rels.count(def.second) ? rels[def.second] : std::string();
        if (part.empty()) {
            // 退化为按序号猜测路径
            part = "xl/worksheets/sheet" + std::to_string(sheets_.size() + 1) + ".xml";
        }
        std::string sx;
        if (!zr.readText(part, sx)) {
            warnings_.push_back("找不到工作表部件: " + part);
            continue;
        }
        Sheet& sh = addSheet(def.first);
        loadSheetXml(sh, sx, shared, xfCodes, xfStyles, (int)sheets_.size() - 1);
        // 记下该表是否有 drawing，稍后统一解析
        sheetHasDrawing.push_back(sx.find("<drawing") != std::string::npos);
    }

    // 4.4) 批注：sheetN.xml.rels -> commentsN.xml。
    // 必须在图表解析之前做，因为两者都要读 sheet rels；
    // 而且批注的表未必有 drawing，不能复用"有 drawing 才读"的分支。
    for (size_t i = 0; i < sheets_.size(); i++) {
        std::string srel;
        if (!zr.readText("xl/worksheets/_rels/sheet" + std::to_string(i + 1) + ".xml.rels", srel))
            continue;
        std::string rerr;
        XmlNode rr = xmlParse(srel, rerr);
        if (!rerr.empty()) continue;
        std::string target;
        for (auto* r : rr.children("Relationship")) {
            std::string ty = r->attr("Type");
            if (ty.size() >= 8 && ty.compare(ty.size() - 8, 8, "comments") == 0) {
                target = r->attr("Target");
                break;
            }
        }
        if (target.empty()) continue;
        // Target 形如 "../comments1.xml"，要还原成包内路径
        std::string part = target;
        if (part.compare(0, 3, "../") == 0) part = "xl/" + part.substr(3);
        std::string cx;
        if (!zr.readText(part, cx)) {
            warnings_.push_back("找不到批注部件: " + part);
            continue;
        }
        std::vector<CellNote> got;
        if (parseCommentsXml(cx, got, warnings_))
            for (const CellNote& n : got) addNote((int)i, n);
    }

    // 4.5) 回填条件格式的样式：cfRule 只存 dxfId，真正的样式在 styles.xml 的 dxfs 里。
    // 必须等所有表都解析完再做，因为 parseCfNode 期间 pendingDxf_ 还在累积。
    applyDxfStyles(dxfs);

    // 5) 图表：sheetN.xml.rels -> drawingN -> chartN
    for (size_t i = 0; i < sheetHasDrawing.size(); i++) {
        if (!sheetHasDrawing[i]) continue;
        std::string srel;
        if (!zr.readText("xl/worksheets/_rels/sheet" + std::to_string(i + 1) + ".xml.rels", srel))
            continue;
        // 找 drawing 目标
        std::string rerr;
        XmlNode rr = xmlParse(srel, rerr);
        if (!rerr.empty()) continue;
        std::string drawTarget;
        for (auto* r : rr.children("Relationship")) {
            std::string ty = r->attr("Type");
            if (ty.find("/drawing") != std::string::npos) { drawTarget = r->attr("Target"); break; }
        }
        if (drawTarget.empty()) continue;
        std::string full = "xl/worksheets/" + drawTarget;
        if (drawTarget.compare(0, 3, "../") == 0) full = "xl/" + drawTarget.substr(3);
        else if (drawTarget[0] == '/') full = drawTarget.substr(1);

        std::string dxml;
        if (!zr.readText(full, dxml)) continue;
        std::string derr;
        XmlNode dr = xmlParse(dxml, derr);
        if (!derr.empty()) { warnings_.push_back("drawing 解析失败"); continue; }

        // drawing -> chart 的关系
        std::string drelsPath = full.substr(0, full.find_last_of('/')) + "/_rels/"
                              + full.substr(full.find_last_of('/') + 1) + ".rels";
        std::string drels;
        std::map<std::string, std::string> chartTargets;
        std::map<std::string, std::string> imageTargets;
        if (zr.readText(drelsPath, drels)) {
            std::string rerr2;
            XmlNode dr2 = xmlParse(drels, rerr2);
            if (rerr2.empty())
                for (auto* r : dr2.children("Relationship")) {
                    std::string ty = r->attr("Type");
                    std::string tg = r->attr("Target");
                    std::string resolved = (tg.compare(0, 3, "../") == 0) ? ("xl/" + tg.substr(3))
                                         : ((!tg.empty() && tg[0] == '/') ? tg.substr(1)
                                                                           : ("xl/drawings/" + tg));
                    if (ty.find("/chart") != std::string::npos)
                        chartTargets[r->attr("Id")] = resolved;
                    else if (ty.find("/image") != std::string::npos)
                        imageTargets[r->attr("Id")] = resolved;
                }
        }

        // 图片：<xdr:pic> 的 r:embed -> rels -> media 部件
        // 与图表分开处理：图表用 r:id + graphicFrame，图片用 r:embed + pic
        for (const PicRef& pr : parsePicRefs(dxml)) {
            auto it = imageTargets.find(pr.embedId);
            if (it == imageTargets.end()) {
                warnings_.push_back("图片关系缺失: " + pr.embedId);
                continue;
            }
            // ZipReader 只暴露解压后的 entries()，没有 readBytes ——
            // 图片是二进制，必须走 entries 拿原始字节，不能走 readText（会破坏数据）
            const ZipEntry* ze = zr.find(it->second);
            if (!ze) {
                warnings_.push_back("找不到图片部件: " + it->second);
                continue;
            }
            ImagePart im;
            im.data = ze->data;
            // 扩展名来自部件路径，而不是 ContentTypes —— 路径后缀才是可靠的
            {
                size_t dot = it->second.find_last_of('.');
                im.ext = (dot == std::string::npos) ? std::string("png") : it->second.substr(dot + 1);
            }
            im.fromCol = pr.fromCol; im.fromRow = pr.fromRow;
            im.toCol   = pr.toCol;   im.toRow   = pr.toRow;
            im.name    = pr.name;
            addImage((int)i, im);
            imageTypesUsed_.insert(im.ext);
        }

        // 每个 graphicFrame 里的 <c:chart r:id="...">
        std::vector<const XmlNode*> frames;
        std::function<void(const XmlNode&)> walk = [&](const XmlNode& n) {
            if (n.is("graphicFrame")) { frames.push_back(&n); return; }
            for (auto& k : n.kids) walk(k);
        };
        walk(dr);

        for (auto* fr : frames) {
            std::string rid, frameName;
            std::function<void(const XmlNode&)> findChart = [&](const XmlNode& n) {
                if (n.is("chart")) { rid = n.attr("id"); }
                for (auto& k : n.kids) findChart(k);
            };
            findChart(*fr);
            if (const XmlNode* cnp = fr->child("cNvPr")) frameName = cnp->attr("name");
            if (rid.empty() || !chartTargets.count(rid)) continue;
            std::string cxml;
            if (!zr.readText(chartTargets[rid], cxml)) continue;
            Chart c;
            std::string cerr;
            if (parseChartXml(cxml, c, cerr)) {
                charts_[i].push_back(c);
                chartTitles_[i].push_back(frameName.empty() ? ("图表 " + std::to_string(charts_[i].size()))
                                                            : frameName);
            } else {
                warnings_.push_back("图表解析失败: " + cerr);
            }
        }
    }

    if (sheets_.empty()) { err = "没有成功加载任何工作表"; return false; }
    err.clear();
    return true;
}

} // namespace xl
