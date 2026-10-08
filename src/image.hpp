#pragma once
// ---------------------------------------------------------------------------
// 嵌入图片（Drawing Pictures）
//
// 图片与图表共用同一个 drawing 部件，但走完全不同的两套 XML：
//   图表 <xdr:graphicFrame> + <c:chart r:id="..."/>   （引用型，r:id）
//   图片 <xdr:pic> + <a:blip r:embed="rIdN"/>          （嵌入型，r:embed）
//
// 图表用 r:id（关系型引用），图片用 r:embed（嵌入型引用）——
// 这两个属性名不能互换，写错了 Excel 会说文件有问题或直接不显示图片。
//
// 三处必须同时存在，缺一不可：
//   1. xl/media/imageN.png                              图片本体（二进制）
//   2. xl/drawings/_rels/drawingN.xml.rels              image 关系
//   3. [Content_Types].xml  的 <Default Extension="png">  注意是 Default 不是 Override
//
// 注意第 3 点：部件 Override 是按路径登记的（如 /xl/charts/chart1.xml），
// 而图片是按**扩展名**登记 Default。两者机制不同，混用会导致打不开。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <map>

namespace xl {

// 一张嵌入的图片
struct ImagePart {
    int fromCol = 0, fromRow = 0;       // 锚点左上角
    int toCol = 2, toRow = 6;           // 锚点右下角
    std::vector<uint8_t> data;          // PNG / JPEG 的原始字节
    std::string ext = "png";            // 扩展名，决定 ContentType
    std::string name;                   // 显示名（Excel 里选中时看到）
};

// ---------------------------------------------------------------------------
// 写出
// ---------------------------------------------------------------------------

// 生成单个 <xdr:pic> 所在的 <xdr:twoCellAnchor>。
// rId 由调用方给出（必须与 drawing rels 里的 Id 一致）。
// shapeId 必须与同一 drawing 内的图表 id 不冲突。
std::string buildPicAnchorXml(const ImagePart& img, int rId, int shapeId);

// 扩展名 -> MIME。未知扩展名返回空串，调用方应据此拒绝。
std::string imageContentType(const std::string& ext);

// ---------------------------------------------------------------------------
// 解析
// ---------------------------------------------------------------------------

// 从 drawing XML 里抽出所有 <xdr:pic> 的 r:embed id 与锚点。
// 拿到 embedId 后，再到 drawing rels 里查出 media 路径。
struct PicRef {
    std::string embedId;                // 如 "rId2"
    std::string name;
    int fromCol = 0, fromRow = 0, toCol = 0, toRow = 0;
};

std::vector<PicRef> parsePicRefs(const std::string& drawingXml);

} // namespace xl
