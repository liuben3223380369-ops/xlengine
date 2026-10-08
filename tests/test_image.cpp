// 嵌入图片测试
//
// 图片与图表共用 drawing 部件，但走两套完全不同的 XML：
//   图表 <xdr:graphicFrame> + <c:chart r:id="..."/>   引用型，r:id
//   图片 <xdr:pic> + <a:blip r:embed="rIdN"/>          嵌入型，r:embed
//
// 三处必须同时存在：media 部件、drawing rels、Content_Types 的 Default。
// 这一组用真实的 PNG 字节验证往返（不是占位数据），并用 openpyxl 交叉确认。
#include "image.hpp"
#include "xlsx.hpp"
#include "sheet.hpp"
#include "canvas.hpp"
#include "chartxml.hpp"
#include <iostream>

using namespace xl;

static int P = 0, N = 0;
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}
static void EQS(const std::string& g, const std::string& e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望='" << e << "' 实际='" << g << "'\n"; }
}
static void EQI(long long g, long long e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望=" << e << " 实际=" << g << "\n"; }
}

// 用画布生成一张真实 PNG（不是占位字节），这样能验证二进制确实没被破坏
static std::vector<uint8_t> makePng(int w, int h) {
    Canvas cv(w, h);
    cv.fillRect(0, 0, w, h, rgb(30, 80, 200));
    cv.fillRect(4, 4, w - 8, h - 8, rgb(255, 200, 40));
    std::vector<uint8_t> out;
    cv.toPNG(out);
    return out;
}

// 手工拼一张图片部件（ImagePart 是纯数据结构，直接填字段即可）
static ImagePart makeImage(int c0, int r0, int c1, int r1,
                           const std::vector<uint8_t>& data,
                           const std::string& ext, const std::string& name) {
    ImagePart im;
    im.fromCol = c0; im.fromRow = r0; im.toCol = c1; im.toRow = r1;
    im.data = data;  im.ext = ext;   im.name = name;
    return im;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    std::cout << "== 类型映射 ==\n";
    {
        EQS(imageContentType("png"), "image/png", "png");
        EQS(imageContentType("PNG"), "image/png", "大写 PNG 也认");
        EQS(imageContentType("jpg"), "image/jpeg", "jpg");
        EQS(imageContentType("jpeg"), "image/jpeg", "jpeg");
        EQS(imageContentType("gif"), "image/gif", "gif");
        // 未知类型必须返回空串，不能猜
        EQS(imageContentType("xyz"), "", "未知类型返回空串（不猜）");
        EQS(imageContentType(""), "", "空串返回空串");
    }

    std::cout << "== XML 片段 ==\n";
    {
        ImagePart im = makeImage(0, 0, 3, 6, makePng(40, 30), "png", "示意图");
        std::string x = buildPicAnchorXml(im, 2, 1000);
        OK(x.find("r:embed=\"rId2\"") != std::string::npos, "用 r:embed（不是 r:id）");
        OK(x.find("r:id") == std::string::npos, "不含 r:id");
        OK(x.find("<xdr:pic>") != std::string::npos, "含 xdr:pic");
        OK(x.find("<a:blip") != std::string::npos, "含 a:blip");
        OK(x.find("name=\"示意图\"") != std::string::npos, "含图片名");
        OK(x.find("<xdr:col>0</xdr:col>") != std::string::npos, "锚点起始列");
        OK(x.find("<xdr:col>3</xdr:col>") != std::string::npos, "锚点结束列");

        // 空数据必须返回空串：写出 0 字节 media 会让 Excel 报文件损坏
        ImagePart empty;
        EQS(buildPicAnchorXml(empty, 1, 1000), "", "空数据不输出");
    }

    std::cout << "== PicRef 解析 ==\n";
    {
        ImagePart im = makeImage(1, 2, 5, 9, makePng(20, 20), "png", "测试图");
        std::vector<ImagePart> imgs{im};
        std::string err;
        std::string dxml = buildDrawingXml({}, imgs, err);
        OK(err.empty(), "生成无错: " + err);

        auto refs = parsePicRefs(dxml);
        EQI(refs.size(), 1, "解析出 1 张");
        if (!refs.empty()) {
            EQS(refs[0].embedId, "rId1", "embedId（无图表时从 1 开始）");
            EQS(refs[0].name, "测试图", "图片名");
            EQI(refs[0].fromCol, 1, "起始列");
            EQI(refs[0].fromRow, 2, "起始行");
            EQI(refs[0].toCol, 5, "结束列");
            EQI(refs[0].toRow, 9, "结束行");
        }
        // 空 XML 不崩
        EQI(parsePicRefs("").size(), 0, "空 XML 得 0");
        // 只有图表的 drawing 不该解析出图片
        ChartPart cp;
        cp.chart = Chart();
        cp.sheetIndex = 0;
        cp.title = "图表 1";
        std::string err2;
        std::string onlyChart = buildDrawingXml({cp}, {}, err2);
        EQI(parsePicRefs(onlyChart).size(), 0, "纯图表 drawing 无图片");
    }

    std::cout << "== xlsx 往返（只有图片、没有图表） ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("带图片");
        sh.setValue(0, 0, Value::str("上方是图片"));
        sh.recalc();

        std::vector<uint8_t> png = makePng(64, 48);
        OK(!png.empty(), "生成 PNG 非空");
        // PNG 魔数：用来确认二进制没被文本化破坏
        OK(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G',
           "PNG 魔数正确");

        ImagePart im;
        im.fromCol = 1; im.fromRow = 1; im.toCol = 5; im.toRow = 10;
        im.data = png; im.ext = "png"; im.name = "示意图";
        wb.addImage(0, im);

        std::string err;
        OK(wb.save("/tmp/xl_img.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_img.xlsx", err), "加载: " + err);
        EQI(rb.allImages()[0].size(), 1, "读回 1 张");
        if (!rb.allImages()[0].empty()) {
            const ImagePart& got = rb.allImages()[0][0];
            EQI(got.data.size(), (long long)png.size(), "字节数一致（二进制未被破坏）");
            OK(got.data == png, "内容逐字节一致");
            EQS(got.ext, "png", "扩展名");
            EQS(got.name, "示意图", "图片名");
            EQI(got.fromCol, 1, "起始列");
            EQI(got.toRow, 10, "结束行");
        }
    }

    std::cout << "== 图片与图表共存（rId 不撞车） ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        for (int i = 0; i < 4; i++) {
            sh.setValue(0, i, Value::num(i + 1));
            sh.setValue(1, i, Value::num((i + 1) * 10));
        }
        sh.recalc();
        {
            Chart c;
            c.type = ChartType::Column;
            c.title = "销售";
            c.categories = {"Q1", "Q2", "Q3", "Q4"};
            DataSeries ds;
            ds.name = "销售额";
            ds.values = {10, 20, 30, 40};
            c.series.push_back(ds);
            wb.addChart(0, c, "销售");
        }
        ImagePart im;
        im.fromCol = 6; im.fromRow = 1; im.toCol = 10; im.toRow = 8;
        im.data = makePng(32, 24); im.ext = "png"; im.name = "附图";
        wb.addImage(0, im);

        std::string err;
        OK(wb.save("/tmp/xl_img2.xlsx", err), "保存: " + err);
        Workbook rb;
        OK(rb.load("/tmp/xl_img2.xlsx", err), "加载: " + err);
        EQI(rb.allImages()[0].size(), 1, "图片读回 1 张");
        EQI(rb.chartCount(0), 1, "图表仍是 1 个");
        OK(!rb.allImages()[0].empty() && rb.allImages()[0][0].data.size() > 8,
           "图片字节完好");
    }

    std::cout << "== 边界与非法输入 ==\n";
    {
        Workbook wb;
        // 空数据不入表
        ImagePart e1;
        e1.data.clear();
        wb.addImage(0, e1);
        EQI(wb.allImages().empty() ? 0 : 1, 0, "空数据不入表（容器未创建）");
        // 越界表号不崩
        wb.addImage(-1, e1);
        OK(true, "负表号不崩");

        // 未知扩展名：写盘时应被跳过并告警，而不是产出打不开的文件
        Workbook wb2;
        wb2.sheet(0).setValue(0, 0, Value::num(1));
        ImagePart bad;
        bad.data = makePng(16, 16);
        bad.ext = "xyz";
        wb2.addImage(0, bad);
        std::string err;
        OK(wb2.save("/tmp/xl_img3.xlsx", err), "未知类型仍能保存（跳过该图）");
        bool warned = false;
        for (const std::string& w : wb2.warnings())
            if (w.find("不支持的图片类型") != std::string::npos) warned = true;
        OK(warned, "给出不支持类型的告警");

        // 清除
        Workbook wb3;
        ImagePart im; im.data = makePng(8, 8);
        wb3.addImage(0, im);
        EQI((int)wb3.allImages()[0].size(), 1, "已加入");
        wb3.clearImages(0);
        EQI((int)wb3.allImages()[0].size(), 0, "已清空");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
