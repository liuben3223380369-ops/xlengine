#include "note.hpp"
#include "xml.hpp"
#include "sheet.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace xl {

std::string cellAddrText(int col, int row) {
    return colToName(col) + std::to_string(row + 1);
}

// 自己解析 "A1"：不用 parseCellRef（它有两个重载，一个是 (string,int&,int&)、
// 一个是 lexer 里的 (string,size_t&,RefPart&)，在 note.cpp 里会撞上重载歧义）
bool parseCellAddr(const std::string& s, int& col, int& row) {
    size_t i = 0;
    if (i < s.size() && s[i] == '$') i++;
    size_t cs = i;
    while (i < s.size() && std::isalpha((unsigned char)s[i])) i++;
    if (i == cs) return false;
    long c = 0;
    for (size_t k = cs; k < i; k++) c = c * 26 + (std::toupper((unsigned char)s[k]) - 'A' + 1);
    if (i < s.size() && s[i] == '$') i++;
    size_t rs = i;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) i++;
    if (i == rs || i != s.size()) return false;
    col = (int)(c - 1);
    row = (int)(std::stol(s.substr(rs)) - 1);
    return col >= 0 && row >= 0;
}

// ---------------------------------------------------------------------------
// 写出
// ---------------------------------------------------------------------------
std::string buildCommentsXml(const std::vector<CellNote>& notes) {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<comments xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";

    // authors 表：去重后按首次出现顺序编号，comment 用 authorId 引用。
    // 不先建表的话，同名作者会重复出现，Excel 打开时作者列表会很难看。
    std::vector<std::string> authors;
    std::vector<int> authorId(notes.size(), 0);
    for (size_t i = 0; i < notes.size(); i++) {
        std::string a = notes[i].author.empty() ? std::string("作者") : notes[i].author;
        int found = -1;
        for (size_t k = 0; k < authors.size(); k++)
            if (authors[k] == a) { found = (int)k; break; }
        if (found < 0) { authors.push_back(a); found = (int)authors.size() - 1; }
        authorId[i] = found;
    }
    if (authors.empty()) authors.push_back("作者");

    o << "<authors>";
    for (const std::string& a : authors) o << "<author>" << xmlEscape(a) << "</author>";
    o << "</authors>\n";

    o << "<commentList>\n";
    for (size_t i = 0; i < notes.size(); i++) {
        const CellNote& n = notes[i];
        o << "<comment ref=\"" << cellAddrText(n.col, n.row) << "\""
          << " authorId=\"" << authorId[i] << "\""
          << " shapeId=\"0\"";
        // Excel 2019+ 用 xr:uid，可省略；这里不写，兼容性更好
        o << ">";
        o << "<text>";
        // 批注文本是富文本：<r><t>段落</t></r>。
        // 支持多行：按 \n 拆成多个 <r>，每段 <t> 前加空格保留（xml:space）
        std::vector<std::string> lines;
        {
            std::string cur;
            for (char c : n.text) {
                if (c == '\n') { lines.push_back(cur); cur.clear(); }
                else cur += c;
            }
            lines.push_back(cur);
        }
        for (size_t k = 0; k < lines.size(); k++) {
            o << "<r>";
            if (k > 0) o << "<rPr><sz val=\"10\"/></rPr>";   // 换行段稍微区分一下
            o << "<t xml:space=\"preserve\">" << xmlEscape(lines[k]) << "</t>";
            o << "</r>";
        }
        o << "</text>";
        o << "</comment>\n";
    }
    o << "</commentList>\n";
    o << "</comments>";
    return o.str();
}

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------
bool parseCommentsXml(const std::string& xml, std::vector<CellNote>& out,
                      std::vector<std::string>& warnings) {
    out.clear();
    std::string err;
    XmlNode root = xmlParse(xml, err);
    if (!err.empty()) return false;

    std::vector<std::string> authors;
    if (const XmlNode* as = root.child("authors"))
        for (auto* a : as->children("author"))
            authors.push_back(xmlUnescape(a->text));

    if (const XmlNode* cl = root.child("commentList")) {
        for (auto* c : cl->children("comment")) {
            CellNote n;
            std::string ref = c->attr("ref");
            if (ref.empty() || !parseCellAddr(ref, n.col, n.row)) {
                warnings.push_back("批注地址无法解析: " + ref);
                continue;
            }
            int aid = std::atoi(c->attr("authorId").c_str());
            if (aid >= 0 && aid < (int)authors.size()) n.author = authors[aid];
            // 文本可能分多个 <r><t>，也可能直接是 <text> 的裸文本
            std::string txt;
            if (const XmlNode* t = c->child("text")) {
                bool gotRun = false;
                for (auto* r : t->children("r")) {
                    gotRun = true;
                    if (const XmlNode* te = r->child("t")) {
                        if (!txt.empty()) txt += "\n";
                        txt += xmlUnescape(te->text);
                    }
                }
                if (!gotRun) txt = xmlUnescape(t->text);
            }
            n.text = txt;
            out.push_back(n);
        }
    }
    return true;
}

} // namespace xl
