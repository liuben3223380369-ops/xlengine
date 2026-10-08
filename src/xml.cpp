#include "xml.hpp"
#include <cstdlib>

namespace xl {

// 去掉命名空间前缀： "x:row" -> "row"
static std::string localName(const std::string& s) {
    size_t p = s.find(':');
    return (p == std::string::npos) ? s : s.substr(p + 1);
}

bool XmlNode::is(const std::string& tag) const { return localName(name) == tag; }

std::string XmlNode::attr(const std::string& a) const {
    // 属性可能带前缀（r:id），按"完全匹配 -> 本地名匹配"两级查找
    auto it = attrs.find(a);
    if (it != attrs.end()) return it->second;
    for (auto& kv : attrs) if (localName(kv.first) == a) return kv.second;
    return std::string();
}

const XmlNode* XmlNode::child(const std::string& tag) const {
    for (auto& k : kids) if (k.is(tag)) return &k;
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::children(const std::string& tag) const {
    std::vector<const XmlNode*> out;
    for (auto& k : kids) if (k.is(tag)) out.push_back(&k);
    return out;
}

std::string xmlUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') { out.push_back(s[i]); continue; }
        size_t end = s.find(';', i);
        if (end == std::string::npos || end - i > 12) { out.push_back(s[i]); continue; }
        std::string ent = s.substr(i + 1, end - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (ent.size() > 1 && ent[0] == '#') {
            long v = 0;
            if (ent[1] == 'x' || ent[1] == 'X') v = strtol(ent.c_str() + 2, nullptr, 16);
            else v = strtol(ent.c_str() + 1, nullptr, 10);
            // XML 是 Unicode 码点，这里只处理 ASCII/Latin-1 范围，其余按码点转 UTF-8
            if (v < 0x80) out.push_back((char)v);
            else if (v < 0x800) {
                out.push_back((char)(0xC0 | (v >> 6)));
                out.push_back((char)(0x80 | (v & 0x3F)));
            } else if (v < 0x10000) {
                out.push_back((char)(0xE0 | (v >> 12)));
                out.push_back((char)(0x80 | ((v >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (v & 0x3F)));
            } else {
                out.push_back((char)(0xF0 | (v >> 18)));
                out.push_back((char)(0x80 | ((v >> 12) & 0x3F)));
                out.push_back((char)(0x80 | ((v >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (v & 0x3F)));
            }
            i = end;
            continue;
        } else { out.push_back(s[i]); continue; }
        i = end;
    }
    return out;
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string xmlEscapeAttr(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            case '\n': out += "&#10;"; break;
            case '\r': out += "&#13;"; break;
            case '\t': out += "&#9;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------
namespace {

// XML 嵌套深度上限。
//
// parseNode 每遇到一层子标签就递归一次，层数完全由输入决定。
// 正常的 xlsx 部件嵌套不超过 10 层（worksheet > sheetData > row > c > v），
// 但一个被损坏或恶意构造的文件可以写出几万层嵌套 —— 压力测试里
// 变异过的大 xlsx 就是这么把栈打爆的（ASAN 报 stack-overflow）。
//
// 这不是"理论上可能"，是实测崩溃。上限取 200，是正常需求的 20 倍，
// 超出则解析失败并给出错误信息，而不是让进程消失。
static const int kMaxXmlDepth = 200;

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    int depth = 0;
    bool depthTruncated = false;      // 是否因超过深度上限而丢弃了内容

    explicit Parser(const std::string& src) : s(src) {}

    struct DepthGuard {
        Parser* p;
        bool ok;
        explicit DepthGuard(Parser* owner) : p(owner) {
            ok = (++p->depth <= kMaxXmlDepth);
            if (!ok) {
                p->depthTruncated = true;
                p->err = "XML 嵌套过深（超过 " + std::to_string(kMaxXmlDepth) + " 层）";
            }
        }
        ~DepthGuard() { --p->depth; }
        explicit operator bool() const { return ok; }
    };

    void skipWs() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) i++; }

    bool lit(const char* p) {
        size_t n = 0; while (p[n]) n++;
        if (i + n > s.size()) return false;
        for (size_t k = 0; k < n; k++) if (s[i+k] != p[k]) return false;
        i += n;
        return true;
    }

    void skipPI() {           // <? ... ?>
        size_t p = s.find("?>", i);
        i = (p == std::string::npos) ? s.size() : p + 2;
    }
    void skipComment() {
        size_t p = s.find("-->", i);
        i = (p == std::string::npos) ? s.size() : p + 3;
    }
    void skipDoctype() {
        int depth = 1;
        while (i < s.size() && depth > 0) {
            if (s[i] == '<') {
                if (s.compare(i, 2, "<!--") == 0) { i += 4; skipComment(); continue; }
                depth++;
                i++;
            } else if (s[i] == '>') { depth--; i++; }
            else i++;
        }
    }
    void skipCDATA(std::string& text) {
        size_t p = s.find("]]>", i);
        if (p == std::string::npos) { text += s.substr(i); i = s.size(); return; }
        text += s.substr(i, p - i);
        i = p + 3;
    }

    std::string readName() {
        size_t start = i;
        while (i < s.size()) {
            char c = s[i];
            if (c==':'||c=='-'||c=='_'||c=='.'||(c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z'))
                i++;
            else break;
        }
        return s.substr(start, i - start);
    }

    bool parseAttrs(std::map<std::string,std::string>& attrs, bool& selfClose) {
        selfClose = false;
        for (;;) {
            skipWs();
            if (i >= s.size()) return false;
            if (s[i] == '>') { i++; selfClose = false; return true; }
            if (s[i] == '/' && i + 1 < s.size() && s[i+1] == '>') { i += 2; selfClose = true; return true; }
            std::string key = readName();
            if (key.empty()) { i++; continue; }        // 容错：跳过无法识别的字符
            skipWs();
            if (i >= s.size()) return false;
            if (s[i] != '=') { attrs[key] = ""; continue; }
            i++;
            skipWs();
            if (i >= s.size()) return false;
            char q = s[i];
            if (q == '"' || q == '\'') {
                i++;
                size_t start = i;
                size_t p = s.find(q, i);
                if (p == std::string::npos) return false;
                attrs[key] = xmlUnescape(s.substr(start, p - start));
                i = p + 1;
            } else {
                size_t start = i;
                while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != '>') i++;
                attrs[key] = xmlUnescape(s.substr(start, i - start));
            }
        }
    }

    bool parseNode(XmlNode& out) {
        DepthGuard dg(this);
        if (!dg) return false;
        // 只处理元素与文本
        while (i < s.size()) {
            if (s[i] == '<') {
                if (s.compare(i, 2, "<?") == 0)  { i += 2; skipPI(); continue; }
                if (s.compare(i, 4, "<!--") == 0) { i += 4; skipComment(); continue; }
                if (s.compare(i, 9, "<![CDATA[") == 0) { i += 9; skipCDATA(out.text); continue; }
                if (s.compare(i, 9, "<!DOCTYPE") == 0) { i += 9; skipDoctype(); continue; }
                if (s.compare(i, 2, "</") == 0) {
                    size_t p = s.find('>', i);
                    if (p == std::string::npos) return false;
                    std::string close = s.substr(i + 2, p - i - 2);
                    // 去掉空白
                    while (!close.empty() && isspace((unsigned char)close.back())) close.pop_back();
                    i = p + 1;
                    // 由调用方比对标签名
                    out.text += xmlUnescape(out.text);
                    return true;
                }
                // 开标签
                i++;
                std::string name = readName();
                if (name.empty()) return false;
                out.name = name;
                // 自闭合必须由 parseAttrs 明确报告（"/>" 结尾）。
                // 不能事后回看字符判断：parseAttrs 已经把 "/>" 消费掉了，
                // 那时 s[i-1] 是 '>' 而不是 '/'，回看必然判错 —— 这会让
                // <sheet .../> 被当成非自闭合，从而吞掉后续兄弟节点。
                bool selfClose = false;
                if (!parseAttrs(out.attrs, selfClose)) return false;
                if (selfClose) {
                    out.text = xmlUnescape(out.text);
                    return true;
                }
                // 子节点
                for (;;) {
                    if (i >= s.size()) break;
                    if (s[i] != '<') {
                        size_t start = i;
                        while (i < s.size() && s[i] != '<') i++;
                        out.text += s.substr(start, i - start);
                        continue;
                    }
                    // 关闭标签：结束本元素
                    if (s.compare(i, 2, "</") == 0) {
                        size_t p = s.find('>', i);
                        if (p == std::string::npos) break;
                        i = p + 1;
                        break;
                    }
                    // 非元素节点必须在这里就地处理：若交给 parseNode，
                    // 它会一路解析到下一个 "</" 才返回，把兄弟节点全吞掉
                    if (s.compare(i, 2, "<?") == 0)  { i += 2; skipPI(); continue; }
                    if (s.compare(i, 4, "<!--") == 0) { i += 4; skipComment(); continue; }
                    if (s.compare(i, 9, "<![CDATA[") == 0) { i += 9; skipCDATA(out.text); continue; }
                    if (s.compare(i, 9, "<!DOCTYPE") == 0) { i += 9; skipDoctype(); continue; }
                    XmlNode kid;
                    size_t before = i;
                    if (!parseNode(kid)) { i = before + 1; continue; }
                    if (!kid.name.empty()) out.kids.push_back(std::move(kid));
                }
                out.text = xmlUnescape(out.text);
                return true;
            }
            // 顶层文本
            size_t start = i;
            while (i < s.size() && s[i] != '<') i++;
            out.text += s.substr(start, i - start);
        }
        return true;
    }
};

} // namespace

XmlNode xmlParse(const std::string& s, std::string& err) {
    XmlNode root;
    Parser p(s);
    // 跳过 prolog，找到第一个元素
    while (p.i < s.size()) {
        if (s[p.i] == '<') {
            if (s.compare(p.i, 2, "<?") == 0)  { p.i += 2; p.skipPI(); continue; }
            if (s.compare(p.i, 4, "<!--") == 0) { p.i += 4; p.skipComment(); continue; }
            if (s.compare(p.i, 9, "<!DOCTYPE") == 0) { p.i += 9; p.skipDoctype(); continue; }
            break;
        }
        p.i++;
    }
    if (p.i >= s.size()) { err = "XML 为空"; return root; }
    if (!p.parseNode(root)) {
        // 深度超限是自我保护，不是普通的语法错误 —— 单独报出来，
        // 否则调用方只会看到"解析失败"，无法区分文件损坏和嵌套过深。
        err = p.err.empty() ? ("XML 解析失败，偏移 " + std::to_string(p.i)) : p.err;
        return root;
    }
    // 超限发生在深层子标签时，外层 parseNode 会把失败当成"普通兄弟节点解析失败"
    // 继续往下走，最终整体返回成功 —— 于是内容被静默丢弃而 err 为空。
    // 这样调用方会以为文件完好，实际上数据已经缺失，所以这里必须显式上报。
    if (p.depthTruncated) {
        err = p.err.empty() ? "XML 嵌套过深，部分内容已丢弃" : p.err;
        return root;
    }
    err.clear();
    return root;
}

} // namespace xl
