#include "style.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <cctype>

namespace xl {

static std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string colorByName(const std::string& name) {
    static const std::map<std::string, std::string> k = {
        {"black", "000000"}, {"white", "FFFFFF"}, {"red", "FF0000"},
        {"green", "008000"}, {"blue", "0000FF"}, {"yellow", "FFFF00"},
        {"cyan", "00FFFF"}, {"magenta", "FF00FF"}, {"gray", "808080"},
        {"grey", "808080"}, {"orange", "FFA500"}, {"pink", "FFC0CB"},
        {"purple", "800080"}, {"brown", "A52A2A"}, {"silver", "C0C0C0"},
        {"gold", "FFD700"}, {"navy", "000080"}, {"teal", "008080"},
        {"lime", "00FF00"}, {"maroon", "800000"}, {"olive", "808000"},
    };
    std::string low = name;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    auto it = k.find(low);
    return it == k.end() ? std::string() : it->second;
}

std::string normalizeColor(const std::string& s) {
    std::string u = upper(s);
    bool hadHash = !u.empty() && u[0] == '#';
    if (hadHash) u.erase(u.begin());
    // #RGB 简写只在带 # 时展开。不加这条限制的话，
    // 颜色名 "red" 会被当成三位十六进制展开成 "RREEDD" —— 名字和简写撞车了。
    if (hadHash && u.size() == 3) {
        std::string x;
        for (char c : u) { x += c; x += c; }
        u = x;
    }
    // 三位简写与颜色名撞车（"F00" vs "red"）。规则：先查名字，
    // 名字查不到且全是十六进制字符时才按简写展开。
    if (u.size() == 3) {
        std::string n = colorByName(s);
        if (!n.empty()) return n;
        bool hex = true;
        for (char c : u) if (!std::isxdigit((unsigned char)c)) hex = false;
        if (hex) {
            std::string x;
            for (char c : u) { x += c; x += c; }
            u = x;
        }
    }
    if (!hadHash && u.size() != 6 && u.size() != 8) {
        std::string n = colorByName(s);
        if (!n.empty()) return n;
    }
    // ARGB（8 位，Excel 内部常用）：去掉前两位 alpha
    if (u.size() == 8) u = u.substr(2);
    if (u.size() != 6) {
        std::string n = colorByName(s);
        return n;                       // 名字也没有就返回空（= 不设置）
    }
    for (char c : u) {
        if (!std::isxdigit((unsigned char)c)) return std::string();
    }
    return u;
}

std::string CellStyle::key() const {
    std::ostringstream o;
    o << (bold ? 'B' : '-') << (italic ? 'I' : '-') << (underline ? 'U' : '-')
      << (strike ? 'S' : '-') << '|' << fontSize << '|' << fontColor << '|'
      << fontName << '|' << fillColor << '|'
      << (int)hAlign << '|' << (int)vAlign << '|' << (wrapText ? 'W' : '-') << '|'
      << (int)border << '|' << borderColor;
    return o.str();
}

const char* hAlignToOoxml(HAlign a) {
    switch (a) {
        case HAlign::Left:   return "left";
        case HAlign::Center: return "center";
        case HAlign::Right:  return "right";
        case HAlign::Justify: return "justify";
        default:             return "general";
    }
}
const char* vAlignToOoxml(VAlign a) {
    switch (a) {
        case VAlign::Center: return "center";
        case VAlign::Top:    return "top";
        default:             return "bottom";
    }
}
const char* borderToOoxml(BorderStyle b) {
    switch (b) {
        case BorderStyle::Thin:   return "thin";
        case BorderStyle::Medium: return "medium";
        case BorderStyle::Thick:  return "thick";
        case BorderStyle::Double: return "double";
        default:                  return "none";
    }
}

bool ooxmlToHAlign(const std::string& s, HAlign& out) {
    if (s == "left") { out = HAlign::Left; return true; }
    if (s == "center" || s == "centre") { out = HAlign::Center; return true; }
    if (s == "right") { out = HAlign::Right; return true; }
    if (s == "justify") { out = HAlign::Justify; return true; }
    if (s == "general") { out = HAlign::General; return true; }
    return false;
}
bool ooxmlToVAlign(const std::string& s, VAlign& out) {
    if (s == "center" || s == "centre") { out = VAlign::Center; return true; }
    if (s == "top") { out = VAlign::Top; return true; }
    if (s == "bottom") { out = VAlign::Bottom; return true; }
    return false;
}
bool ooxmlToBorder(const std::string& s, BorderStyle& out) {
    if (s == "thin") { out = BorderStyle::Thin; return true; }
    if (s == "medium") { out = BorderStyle::Medium; return true; }
    if (s == "thick") { out = BorderStyle::Thick; return true; }
    if (s == "double") { out = BorderStyle::Double; return true; }
    if (s == "none") { out = BorderStyle::None; return true; }
    return false;
}

} // namespace xl
