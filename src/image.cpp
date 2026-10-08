#include "image.hpp"
#include "xml.hpp"
#include <sstream>
#include <algorithm>

namespace xl {

std::string imageContentType(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    if (e == "png")  return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    if (e == "gif")  return "image/gif";
    if (e == "bmp")  return "image/bmp";
    if (e == "emf")  return "image/x-emf";
    if (e == "wmf")  return "image/x-wmf";
    if (e == "svg")  return "image/svg+xml";
    return std::string();       // 未知类型：让调用方拒绝，不要猜
}

std::string buildPicAnchorXml(const ImagePart& img, int rId, int shapeId) {
    std::ostringstream o;
    // 空数据不输出：写出一个 0 字节的 media 部件会让 Excel 报文件损坏
    if (img.data.empty()) return std::string();

    std::string nm = img.name.empty() ? ("图片 " + std::to_string(shapeId)) : img.name;

    o << "<xdr:twoCellAnchor>\n"
      << "<xdr:from>"
      << "<xdr:col>" << img.fromCol << "</xdr:col>"
      << "<xdr:colOff>0</xdr:colOff>"
      << "<xdr:row>" << img.fromRow << "</xdr:row>"
      << "<xdr:rowOff>0</xdr:rowOff>"
      << "</xdr:from>\n"
      << "<xdr:to>"
      << "<xdr:col>" << img.toCol << "</xdr:col>"
      << "<xdr:colOff>0</xdr:colOff>"
      << "<xdr:row>" << img.toRow << "</xdr:row>"
      << "<xdr:rowOff>0</xdr:rowOff>"
      << "</xdr:to>\n"
      << "<xdr:pic>\n"
      << "<xdr:nvPicPr>"
      << "<xdr:cNvPr id=\"" << shapeId << "\" name=\"" << xmlEscapeAttr(nm) << "\"/>"
      << "<xdr:cNvPicPr><a:picLocks noChangeAspect=\"0\"/></xdr:cNvPicPr>"
      << "</xdr:nvPicPr>\n"
      // 关键：图片用 r:embed，不是 r:id（那是图表用的）
      << "<xdr:blipFill>"
      << "<a:blip r:embed=\"rId" << rId << "\" cstate=\"none\"/>"
      << "<a:stretch><a:fillRect/></a:stretch>"
      << "</xdr:blipFill>\n"
      << "<xdr:spPr>"
      << "<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></a:xfrm>"
      << "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
      << "</xdr:spPr>\n"
      << "</xdr:pic>\n"
      << "<xdr:clientData/>\n"
      << "</xdr:twoCellAnchor>\n";
    return o.str();
}

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------
namespace {

// 递归找后代。drawing XML 的标签都带 xdr: / a: 前缀，
// XmlNode::is() 会忽略前缀，所以直接用它判定即可。
const XmlNode* findDeep(const XmlNode& n, const std::string& shortName) {
    for (const XmlNode& k : n.kids) {
        if (k.is(shortName)) return &k;
        if (const XmlNode* d = findDeep(k, shortName)) return d;
    }
    return nullptr;
}

int intOf(const XmlNode* parent, const std::string& shortName) {
    if (!parent) return 0;
    for (const XmlNode& k : parent->kids)
        if (k.is(shortName)) return std::atoi(k.text.c_str());
    return 0;
}

} // namespace

std::vector<PicRef> parsePicRefs(const std::string& drawingXml) {
    std::vector<PicRef> out;
    std::string err;
    XmlNode root = xmlParse(drawingXml, err);
    if (!err.empty()) return out;

    for (const XmlNode& anchor : root.kids) {
        if (!anchor.is("twoCellAnchor") && !anchor.is("oneCellAnchor")
            && !anchor.is("absoluteAnchor")) continue;
        // 锚点标签有三种：twoCellAnchor / oneCellAnchor / absoluteAnchor
        const XmlNode* pic = findDeep(anchor, "pic");
        if (!pic) continue;
        const XmlNode* blip = findDeep(*pic, "blip");
        if (!blip) continue;
        // r:embed：XmlNode::attr 已按"完全匹配 -> 本地名匹配"两级查找，
        // 所以直接传本地名 embed 就能拿到带前缀的那个属性
        std::string emb = blip->attr("embed");
        if (emb.empty()) continue;

        PicRef p;
        p.embedId = emb;
        if (const XmlNode* cnp = findDeep(*pic, "cNvPr"))
            p.name = xmlUnescape(cnp->attr("name"));

        // 锚点：优先取紧贴 twoCellAnchor 的 from/to，
        // 不能递归找 —— pic 内部没有同名标签，但递归会在别处误命中
        for (const XmlNode& c : anchor.kids) {
            bool isFrom = c.is("from");
            bool isTo   = c.is("to");
            if (!isFrom && !isTo) continue;
            int col = intOf(&c, "col");
            int row = intOf(&c, "row");
            if (isFrom) { p.fromCol = col; p.fromRow = row; }
            else        { p.toCol   = col; p.toRow   = row; }
        }
        out.push_back(p);
    }
    return out;
}

} // namespace xl
