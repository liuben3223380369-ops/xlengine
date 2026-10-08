#include "chartxml.hpp"
#include "xml.hpp"
#include <sstream>
#include <cmath>
#include <algorithm>

namespace xl {

// ---------------------------------------------------------------------------
// 类型映射
// ---------------------------------------------------------------------------
std::string chartTypeToOoxml(ChartType t) {
    switch (t) {
        case ChartType::Column:        return "barChart:col:clustered";
        case ChartType::StackedColumn: return "barChart:col:stacked";
        case ChartType::Bar:           return "barChart:bar:clustered";
        case ChartType::Line:          return "lineChart::standard";
        case ChartType::Area:          return "areaChart::standard";
        case ChartType::StackedArea:   return "areaChart::stacked";
        case ChartType::Pie:           return "pieChart::";
        case ChartType::Scatter:       return "scatterChart::";
        case ChartType::Histogram:     return "barChart:col:clustered";
    }
    return "barChart:col:clustered";
}

ChartType ooxmlToChartType(const std::string& family, const std::string& barDir,
                           const std::string& grouping, bool& ok) {
    ok = true;
    if (family == "barChart") {
        if (barDir == "bar") return ChartType::Bar;
        return (grouping == "stacked" || grouping == "percentStacked") ? ChartType::StackedColumn
                                                                       : ChartType::Column;
    }
    if (family == "lineChart")  return ChartType::Line;
    if (family == "areaChart")  return (grouping == "stacked" || grouping == "percentStacked")
                                        ? ChartType::StackedArea : ChartType::Area;
    if (family == "pieChart")   return ChartType::Pie;
    if (family == "scatterChart") return ChartType::Scatter;
    ok = false;
    return ChartType::Column;
}

// ---------------------------------------------------------------------------
// 数值文本
// ---------------------------------------------------------------------------
static std::string numStr(double d) {
    if (!std::isfinite(d)) return "0";
    std::ostringstream o;
    o << std::setprecision(15) << d;
    return o.str();
}

// ---------------------------------------------------------------------------
// 生成 chart XML
// ---------------------------------------------------------------------------
std::string buildChartXml(const Chart& c, std::string& err) {
    err.clear();
    if (c.series.empty()) { err = "图表没有数据系列"; return std::string(); }

    std::string spec = chartTypeToOoxml(c.type);
    // family:barDir:grouping
    std::string family = spec.substr(0, spec.find(':'));
    std::string rest = spec.substr(spec.find(':') + 1);
    std::string barDir = rest.substr(0, rest.find(':'));
    std::string grouping = rest.substr(rest.find(':') + 1);

    bool isScatter = (c.type == ChartType::Scatter);
    bool isPie = (c.type == ChartType::Pie);

    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
      << "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
      << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n"
      << "<c:chart>\n";

    // 标题
    if (!c.title.empty()) {
        o << "<c:title><c:tx><c:strRef><c:strCache><c:pt idx=\"0\"><c:v>"
          << xmlEscape(c.title) << "</c:v></c:pt></c:strCache></c:strRef></c:tx>"
          << "<c:overlay val=\"0\"/></c:title>\n";
    } else {
        o << "<c:autoTitleDeleted val=\"0\"/>\n";
    }

    o << "<c:plotArea><c:layout/>\n";

    // ---------- 图族开标签 ----------
    o << "<c:" << family << ">\n";
    if (family == "barChart")
        o << "<c:barDir val=\"" << barDir << "\"/>\n";
    if (!grouping.empty() && family != "pieChart")
        o << "<c:grouping val=\"" << grouping << "\"/>\n";
    if (family == "scatterChart")
        o << "<c:scatterStyle val=\"lineMarker\"/>\n";
    if (family == "pieChart" && c.showDataLabels)
        o << "<c:varyColors val=\"1\"/>\n";

    // ---------- 数据系列 ----------
    for (size_t si = 0; si < c.series.size(); si++) {
        const DataSeries& s = c.series[si];
        // 引用型系列（只有 valRange、没有内联 values）同样要输出 ——
        // 之前只判 values.empty() 会把这类系列整个丢掉
        if (s.values.empty() && s.valRange.empty()) continue;
        // 散点图的 X 引用型同理
        if (isScatter && s.xs.empty() && s.xRange.empty() && s.valRange.empty()) continue;
        o << "<c:ser>\n"
          << "<c:idx val=\"" << si << "\"/><c:order val=\"" << si << "\"/>\n";

        // 系列名
        o << "<c:tx><c:strRef><c:strCache><c:pt idx=\"0\"><c:v>"
          << xmlEscape(s.name.empty() ? ("系列" + std::to_string(si + 1)) : s.name)
          << "</c:v></c:pt></c:strCache></c:strRef></c:tx>\n";

        // 点的填充色（Excel 主题色之外的自定义色）
        if (s.hasColor()) {
            std::string hex = s.color;
            if (hex.size() == 7 && hex[0] == '#')
                o << "<c:spPr><a:solidFill><a:srgbClr val=\"" << hex.substr(1)
                  << "\"/></a:solidFill><a:ln><a:solidFill><a:srgbClr val=\""
                  << hex.substr(1) << "\"/></a:solidFill></a:ln></c:spPr>\n";
        }

        // 散点图：先 X 后 Y
        if (isScatter) {
            o << "<c:xVal>";
            if (!s.xRange.empty()) {
                o << "<c:numRef><c:f>" << xmlEscape(s.xRange) << "</c:f></c:numRef>";
            } else {
                o << "<c:numLit>";
                for (size_t i = 0; i < s.values.size(); i++) {
                    double xv = (i < s.xs.size()) ? s.xs[i] : (double)i;
                    o << "<c:pt idx=\"" << i << "\"><c:v>" << numStr(xv) << "</c:v></c:pt>";
                }
                o << "</c:numLit>";
            }
            o << "</c:xVal>\n";
            o << "<c:yVal>";
            if (!s.valRange.empty()) {
                o << "<c:numRef><c:f>" << xmlEscape(s.valRange) << "</c:f></c:numRef>";
            } else {
                o << "<c:numLit>";
                for (size_t i = 0; i < s.values.size(); i++)
                    o << "<c:pt idx=\"" << i << "\"><c:v>" << numStr(s.values[i]) << "</c:v></c:pt>";
                o << "</c:numLit>";
            }
            o << "</c:yVal>\n";
        } else {
            // 分类轴标签
            if (!s.catRange.empty()) {
                o << "<c:cat><c:strRef><c:f>" << xmlEscape(s.catRange)
                  << "</c:f></c:strRef></c:cat>\n";
            } else if (!c.categories.empty()) {
                o << "<c:cat><c:strRef><c:strCache>";
                for (size_t i = 0; i < c.categories.size(); i++)
                    o << "<c:pt idx=\"" << i << "\"><c:v>" << xmlEscape(c.categories[i]) << "</c:v></c:pt>";
                o << "</c:strCache></c:strRef></c:cat>\n";
            } else {
                o << "<c:cat><c:numLit>";
                for (size_t i = 0; i < s.values.size(); i++)
                    o << "<c:pt idx=\"" << i << "\"><c:v>" << i + 1 << "</c:v></c:pt>";
                o << "</c:numLit></c:cat>\n";
            }
            // 数值
            o << "<c:val>";
            if (!s.valRange.empty()) {
                o << "<c:numRef><c:f>" << xmlEscape(s.valRange) << "</c:f></c:numRef>";
            } else {
                o << "<c:numLit>";
                for (size_t i = 0; i < s.values.size(); i++)
                    o << "<c:pt idx=\"" << i << "\"><c:v>" << numStr(s.values[i]) << "</c:v></c:pt>";
                o << "</c:numLit>";
            }
            o << "</c:val>\n";
        }
        o << "</c:ser>\n";
    }

    // ---------- 坐标轴 ID ----------
    // Excel 要求每个轴的 axId 全局唯一且在 axId 列表中登记。
    // 约定：分类轴 100001+，数值轴 200001+，同一图表的多个轴依次递增。
    if (!isPie) {
        if (isScatter) {
            o << "<c:axId val=\"100001\"/><c:axId val=\"200001\"/>\n";
        } else {
            o << "<c:axId val=\"100001\"/><c:axId val=\"200001\"/>\n";
        }
    }
    o << "</c:" << family << ">\n";

    // ---------- 坐标轴定义 ----------
    if (!isPie) {
        // 分类轴（散点图为数值轴）
        if (isScatter) {
            o << "<c:valAx><c:axId val=\"100001\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
              << "<c:delete val=\"0\"/><c:axPos val=\"b\"/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>"
              << "<c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/>"
              << "<c:crossAx val=\"200001\"/><c:crosses val=\"autoZero\"/><c:auto val=\"1\"/>"
              << "<c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:valAx>\n";
        } else {
            o << "<c:catAx><c:axId val=\"100001\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
              << "<c:delete val=\"0\"/><c:axPos val=\"b\"/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>"
              << "<c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/>"
              << "<c:crossAx val=\"200001\"/><c:crosses val=\"autoZero\"/><c:auto val=\"1\"/>"
              << "<c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:catAx>\n";
        }
        // 数值轴
        o << "<c:valAx><c:axId val=\"200001\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
          << "<c:delete val=\"0\"/><c:axPos val=\"l\"/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>"
          << "<c:majorGridlines/><c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/>"
          << "<c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"100001\"/><c:crosses val=\"autoZero\"/>"
          << "<c:crossBetween val=\"between\"/><c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/>"
          << "<c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:valAx>\n";
    }

    o << "</c:plotArea>\n";

    // ---------- 图例 ----------
    if (c.showLegend && !isPie) {
        o << "<c:legend><c:legendPos val=\"b\"/><c:overlay val=\"0\"/></c:legend>\n";
    } else if (c.showLegend && isPie) {
        o << "<c:legend><c:legendPos val=\"r\"/><c:overlay val=\"0\"/></c:legend>\n";
    }
    o << "<c:plotVisOnly val=\"1\"/>\n";
    o << "</c:chart>\n</c:chartSpace>";
    return o.str();
}

// ---------------------------------------------------------------------------
// 生成 drawing XML（把图表锚定到单元格）
// ---------------------------------------------------------------------------
std::string buildDrawingXml(const std::vector<ChartPart>& parts,
                            const std::vector<ImagePart>& images,
                            std::string& err) {
    err.clear();
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
      << "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
      << "xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
      << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n";

    for (size_t i = 0; i < parts.size(); i++) {
        const Chart& c = parts[i].chart;
        o << "<xdr:twoCellAnchor>\n"
          << "<xdr:from><xdr:col>" << c.anchor.fromCol << "</xdr:col>"
          << "<xdr:colOff>0</xdr:colOff>"
          << "<xdr:row>" << c.anchor.fromRow << "</xdr:row>"
          << "<xdr:rowOff>0</xdr:rowOff></xdr:from>\n"
          << "<xdr:to><xdr:col>" << c.anchor.toCol << "</xdr:col>"
          << "<xdr:colOff>0</xdr:colOff>"
          << "<xdr:row>" << c.anchor.toRow << "</xdr:row>"
          << "<xdr:rowOff>0</xdr:rowOff></xdr:to>\n"
          << "<xdr:graphicFrame macro=\"\">\n"
          << "<xdr:nvGraphicFramePr><xdr:cNvPr id=\"" << (i + 2) << "\" name=\""
          << xmlEscapeAttr(parts[i].title) << "\"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>\n"
          << "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm>\n"
          << "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">\n"
          << "<c:chart r:id=\"rId" << (i + 1) << "\"/>\n"
          << "</a:graphicData></a:graphic>\n"
          << "</xdr:graphicFrame>\n"
          << "<xdr:clientData/>\n"
          << "</xdr:twoCellAnchor>\n";
    }

    // 图片排在图表之后，rId 从图表数量 +1 开始 —— 与 rels 的分配顺序必须一致，
    // 错开的话图片会指向图表的部件，Excel 里表现为图片区域空白或报错。
    //
    // shapeId 从 1000 起，避开图表用的 2..N+1，防止 cNvPr id 撞车
    for (size_t k = 0; k < images.size(); k++) {
        int rId = (int)parts.size() + (int)k + 1;
        std::string frag = buildPicAnchorXml(images[k], rId, 1000 + (int)k);
        if (frag.empty()) { err = "图片 " + std::to_string(k + 1) + " 数据为空，已跳过"; continue; }
        o << frag;
    }
    o << "</xdr:wsDr>";
    return o.str();
}

std::string buildDrawingRels(int chartCount, int imageCount,
                             const std::vector<std::string>* imageTargets) {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
    for (int i = 1; i <= chartCount; i++)
        o << "<Relationship Id=\"rId" << i << "\" "
          << "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" "
          << "Target=\"../charts/chart" << i << ".xml\"/>\n";
    // 图片用 image 关系类型，Target 指向 media 部件
    for (int k = 0; k < imageCount; k++) {
        std::string tgt = (imageTargets && k < (int)imageTargets->size())
                            ? (*imageTargets)[k]
                            : ("../media/image" + std::to_string(k + 1) + ".png");
        o << "<Relationship Id=\"rId" << (chartCount + k + 1) << "\" "
          << "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
          << "Target=\"" << tgt << "\"/>\n";
    }
    o << "</Relationships>";
    return o.str();
}

std::string buildSheetRels(int drawingId) {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
      << "<Relationship Id=\"rId1\" "
      << "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" "
      << "Target=\"../drawings/drawing" << drawingId << ".xml\"/>\n"
      << "</Relationships>";
    return o.str();
}

// ---------------------------------------------------------------------------
// 解析 chart XML
// ---------------------------------------------------------------------------
namespace {

// 取 <c:f> 引用文本。
// 它嵌在 <c:val><c:numRef><c:f> 里，不是 c:val 的直接子节点，
// 只查一层会拿不到 —— 必须递归。
std::string refFormula(const XmlNode& n) {
    if (const XmlNode* f = n.child("f")) return f->text;
    for (auto& k : n.kids) {
        std::string r = refFormula(k);
        if (!r.empty()) return r;
    }
    return std::string();
}

// 取内联值（numLit / strLit 下的 pt/v）
// 取内联值。同理，<c:pt><c:v> 藏在 numCache/strCache/numLit/strLit 之下，
// 而这些容器又嵌在 numRef/strRef 里，需要按 idx 顺序收集。
void collectPts(const XmlNode& n, std::vector<size_t>& idxs, std::vector<std::string>& vals) {
    if (n.is("pt")) {
        if (const XmlNode* v = n.child("v")) {
            size_t idx = idxs.size();
            std::string ia = n.attr("idx");
            if (!ia.empty()) { try { idx = (size_t)std::stoul(ia); } catch (...) {} }
            idxs.push_back(idx);
            vals.push_back(v->text);
        }
        return;
    }
    for (auto& k : n.kids) collectPts(k, idxs, vals);
}

void litValues(const XmlNode& n, std::vector<double>& nums, std::vector<std::string>& strs) {
    std::vector<size_t> idxs;
    std::vector<std::string> vals;
    collectPts(n, idxs, vals);
    // 按 idx 排序，保证与写入顺序一致
    std::vector<size_t> order(vals.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return idxs[a] < idxs[b]; });
    for (size_t i : order) {
        strs.push_back(vals[i]);
        double d = std::nan("");
        try {
            size_t pos = 0;
            std::string sv = vals[i];
            while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.erase(sv.begin());
            while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.pop_back();
            if (sv.empty()) d = 0.0;
            else { d = std::stod(sv, &pos); if (pos != sv.size()) d = std::nan(""); }
        } catch (...) { d = std::nan(""); }
        nums.push_back(d);
    }
}

void seriesTx(const XmlNode& ser, std::string& name) {
    const XmlNode* tx = ser.child("tx");
    if (!tx) return;
    // 优先取内联文本：<c:tx><c:strRef><c:strCache><c:pt><c:v>名字</c:v>
    std::vector<double> d; std::vector<std::string> s;
    litValues(*tx, d, s);
    if (!s.empty() && !s[0].empty()) { name = s[0]; return; }
    // 只有引用时，名字就是引用文本
    std::string f = refFormula(*tx);
    if (!f.empty()) name = f;
}

}

bool parseChartXml(const std::string& xmlText, Chart& out, std::string& err) {
    std::string perr;
    XmlNode root = xmlParse(xmlText, perr);
    if (!perr.empty()) { err = "chart XML 解析失败: " + perr; return false; }

    const XmlNode* chart = root.child("chart");
    if (!chart) { err = "缺少 c:chart"; return false; }

    // 标题
    if (const XmlNode* t = chart->child("title")) {
        std::vector<double> d; std::vector<std::string> s;
        litValues(*t, d, s);
        if (!s.empty()) out.title = s[0];
    }

    const XmlNode* plot = chart->child("plotArea");
    if (!plot) { err = "缺少 c:plotArea"; return false; }

    // 找到图族节点
    static const char* families[] = {"barChart", "lineChart", "areaChart", "pieChart",
                                     "scatterChart", "radarChart", "doughnutChart", "bar3DChart"};
    const XmlNode* fam = nullptr;
    std::string famName;
    for (const char* f : families) {
        if (const XmlNode* n = plot->child(f)) { fam = n; famName = f; break; }
    }
    if (!fam) { err = "未识别的图表类型"; return false; }

    std::string barDir, grouping;
    if (const XmlNode* bd = fam->child("barDir")) barDir = bd->attr("val");
    if (const XmlNode* g = fam->child("grouping")) grouping = g->attr("val");
    bool ok = false;
    out.type = ooxmlToChartType(famName, barDir, grouping, ok);
    if (!ok) err = "图表类型未完全支持: " + famName;

    bool isScatter = (out.type == ChartType::Scatter);

    for (auto* ser : fam->children("ser")) {
        DataSeries s;
        seriesTx(*ser, s.name);

        if (isScatter) {
            if (const XmlNode* xv = ser->child("xVal")) {
                std::string f = refFormula(*xv);
                if (!f.empty()) s.xRange = f;
                std::vector<double> d; std::vector<std::string> vs;
                litValues(*xv, d, vs);
                for (double v : d) s.xs.push_back(v);
            }
            if (const XmlNode* yv = ser->child("yVal")) {
                std::string f = refFormula(*yv);
                if (!f.empty()) s.valRange = f;
                std::vector<double> d; std::vector<std::string> vs;
                litValues(*yv, d, vs);
                for (double v : d) s.values.push_back(v);
            }
        } else {
            if (const XmlNode* cat = ser->child("cat")) {
                std::string f = refFormula(*cat);
                if (!f.empty()) s.catRange = f;
                std::vector<double> d; std::vector<std::string> vs;
                litValues(*cat, d, vs);
                if (out.categories.empty() && !vs.empty()) out.categories = vs;
            }
            if (const XmlNode* val = ser->child("val")) {
                std::string f = refFormula(*val);
                if (!f.empty()) s.valRange = f;
                std::vector<double> d; std::vector<std::string> vs;
                litValues(*val, d, vs);
                for (double v : d) s.values.push_back(v);
            }
        }
        out.series.push_back(std::move(s));
    }

    out.showLegend = (chart->child("legend") != nullptr);
    return true;
}

} // namespace xl
