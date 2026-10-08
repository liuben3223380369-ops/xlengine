#pragma once
// ---------------------------------------------------------------------------
// 极简 XML：只读需要的部分 + 只写需要的部分。
//
// xlsx 的 XML 有几个必须处理的点，否则真实文件会解析失败：
//   - 命名空间前缀（如 <x:row>、<ss:si>），标签名要原样保留
//   - 实体 &amp; &lt; &gt; &quot; &apos; 与数字引用 &#NNN;
//   - 自闭合标签 <c r="A1"/>
//   - 处理指令 <?xml ...?> 与注释 <!-- -->
// 不支持 DTD、外部实体（安全考虑：遇到就跳过）。
// ---------------------------------------------------------------------------
#include <string>
#include <map>
#include <vector>

namespace xl {

struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::string text;                       // 元素内的直接文本（拼接后）
    std::vector<XmlNode> kids;

    const XmlNode* child(const std::string& tag) const;      // 按"本地名"匹配，忽略前缀
    std::vector<const XmlNode*> children(const std::string& tag) const;
    std::string attr(const std::string& a) const;
    bool is(const std::string& tag) const;                   // 忽略前缀的标签名判定
};

// 解析；err 非空表示失败
XmlNode xmlParse(const std::string& s, std::string& err);

// 转义文本节点内容
std::string xmlEscape(const std::string& s);
// 转义属性值（额外处理引号）
std::string xmlEscapeAttr(const std::string& s);
// 反转义（解析实体与数字引用）
std::string xmlUnescape(const std::string& s);

} // namespace xl
