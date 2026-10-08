// PDF 导出测试
//
// 分两类：
//   A. 纯 C++ 结构校验 —— 不依赖任何 PDF 库，直接读字节验证 xref 偏移。
//      xref 是"错了以后文件照样能生成、但谁都打不开"的典型，
//      必须用真实字节偏移来断言，不能只看"文件存在"。
//   B. 端到端 —— 生成的样例文件由 make verify 用 pypdf 复核（文本可提取 = 字体嵌入正确）
#include "pdf.hpp"
#include "ttf.hpp"
#include "sheetpdf.hpp"
#include "chartpdf.hpp"
#include "sheet.hpp"
#include "chart.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>

using namespace xl;

static int P = 0, N = 0;
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}
static void EQ(const std::string& g, const std::string& e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望='" << e << "' 实际='" << g << "'\n"; }
}

static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o; o << f.rdbuf(); return o.str();
}

static const char* kFont = "/usr/share/fonts/truetype/alibaba-puhuiti/AlibabaPuHuiTi-2-35-Thin.ttf";
static const std::string kTmp = "/tmp/xl_pdf_test";

// 校验 PDF 的 xref：每个偏移处必须真的是 "<n> 0 obj"
static bool checkXref(const std::string& d, std::string& why) {
    // 注意顺序：必须先找 startxref，再在它之前找 xref。
    // 直接 rfind("xref") 会命中 "startxref" 里的子串，得到完全错误的位置。
    size_t sx = d.rfind("startxref");
    if (sx == std::string::npos) { why = "没有 startxref"; return false; }
    size_t xr = d.rfind("xref", sx - 1);
    if (xr == std::string::npos) { why = "没有 xref"; return false; }
    // 且它必须是独立一行（前一字符是换行）
    if (xr > 0 && d[xr - 1] != '\n') { why = "xref 不在行首"; return false; }
    // startxref 后面的数字必须正好指向 "xref"
    size_t p = sx + 9;
    while (p < d.size() && (d[p] == '\n' || d[p] == '\r' || d[p] == ' ')) p++;
    long long val = 0;
    while (p < d.size() && d[p] >= '0' && d[p] <= '9') { val = val * 10 + (d[p] - '0'); p++; }
    if (val != (long long)xr) { why = "startxref=" + std::to_string(val) + " 与实际 xref 位置 " + std::to_string(xr) + " 不符"; return false; }
    // 逐条检查
    size_t q = xr + 4;
    while (q < d.size() && (d[q] == '\n' || d[q] == '\r')) q++;
    // "0 <count>"
    size_t cnt = 0;
    while (q < d.size() && d[q] != '\n') q++;
    q++;
    // 解析 count
    {
        size_t s2 = xr + 4;
        while (s2 < d.size() && (d[s2] == '\n' || d[s2] == '\r')) s2++;
        // 跳过 "0 "
        if (d[s2] == '0' && d[s2 + 1] == ' ') s2 += 2;
        while (s2 < d.size() && d[s2] >= '0' && d[s2] <= '9') { cnt = cnt * 10 + (d[s2] - '0'); s2++; }
    }
    int checked = 0;
    // count 含空闲条目，因此最后一个对象号是 cnt-1，不是 cnt
    for (size_t i = 1; i < cnt; i++) {
        // 记录 j 描述对象 j（记录 0 是空闲条目），所以对象 i 在第 i 条。
        // 写成 (i-1)*20 会整体错位一条，读到的全是"上一个对象"的偏移。
        size_t rec = q + i * 20;
        if (rec + 20 > d.size()) { why = "xref 条目越界"; return false; }
        std::string offS = d.substr(rec, 10);
        long long off = 0;
        for (char c : offS) { if (c < '0' || c > '9') { why = "偏移不是数字: '" + offS + "'"; return false; } off = off * 10 + (c - '0'); }
        if (i == 1) continue;                      // 第 0 条是空闲条目
        std::string want = std::to_string(i) + " 0 obj";
        if (d.compare((size_t)off, want.size(), want) != 0) {
            why = "对象 " + std::to_string(i) + " 的偏移 " + std::to_string(off) + " 处不是 '" + want + "'";
            return false;
        }
        checked++;
    }
    if (checked < 3) { why = "对象太少"; return false; }
    return true;
}

int main() {
    // ------------------------------------------------------------------
    std::cout << "== TTF 解析 ==\n";
    {
        TtfFont f; std::string e;
        bool ok = f.load(kFont, e);
        if (!ok) { std::cout << "  （跳过：字体不可用 " << e << "）\n"; return 0; }
        OK(f.metrics().numGlyphs > 1000, "字形数 > 1000");
        OK(f.metrics().unitsPerEm > 0, "unitsPerEm 有效");
        // 'A' 必须有字形；一个不存在的码点应返回 0
        OK(f.glyphFor('A') != 0, "有 'A' 的字形");
        OK(f.glyphFor(0xE000) == 0, "私用区码点无字形");
        OK(f.glyphFor(0x4F60) != 0, "有 '你'(U+4F60) 的字形");
        // 中文应比 ASCII 宽（大致）
        OK(f.textWidthEm("你") > f.textWidthEm("A") * 0.9, "汉字宽度不小于拉丁字母");
        OK(f.textWidthEm("") == 0.0, "空串宽度为 0");

        // 子集
        std::vector<uint32_t> cps;
        for (char c = ' '; c <= '~'; c++) cps.push_back((uint32_t)c);
        for (uint32_t c : TtfFont::decodeUtf8("你好世界")) cps.push_back(c);
        std::vector<uint8_t> out; std::map<uint32_t, uint16_t> cids;
        OK(f.subset(cps, out, cids, e), "子集化成功: " + e);
        OK(out.size() > 1000, "子集非空");
        // 关键回归：原样搬运 post 表会让"只嵌一个字"的子集也达到 291 KB
        OK(out.size() < 100 * 1024, "子集体积合理（<100KB），实际 " + std::to_string(out.size()));
        OK(out.size() < 40 * 1024, "子集体积较优（<40KB），实际 " + std::to_string(out.size()));
        OK(cids.count('A') == 1, "'A' 在子集里");
        OK(cids.count(0x4F60) == 1, "'你' 在子集里");
        // 子集必须是合法 sfnt
        OK(out.size() > 12 && out[0] == 0x00 && out[1] == 0x01, "子集是合法 sfnt 头");
        // 单字子集应远小于全量
        std::vector<uint8_t> one; std::map<uint32_t, uint16_t> c1;
        std::vector<uint32_t> oneCp = {0x4F60};
        f.subset(oneCp, one, c1, e);
        OK(one.size() < out.size(), "单字子集小于多字子集");
    }

    // ------------------------------------------------------------------
    std::cout << "== PDF 结构 ==\n";
    std::string pdfPath = kTmp + ".pdf";
    {
        PdfWriter w;
        w.setPageSize(595.28, 841.89);
        std::string fn, err;
        OK(w.embedFont(kFont, fn, err), "嵌入字体: " + err);
        EQ(fn, "F1", "第一个字体资源名");
        w.newPage();     // 首帧为空 -> 不应产生空白页
        w.text(60, 80, "Hello 你好", 14);
        w.fillRect(60, 120, 100, 30, PdfColor::rgb8(0xED, 0x7D, 0x31));
        w.newPage();
        w.text(60, 80, "第二页", 14);
        OK(w.save(pdfPath, err), "保存: " + err);
        OK(w.pageCount() == 2, "两页（空白首帧不计）");
    }
    {
        std::string d = readFile(pdfPath);
        OK(d.size() > 500, "文件非空");
        OK(d.compare(0, 5, "%PDF-") == 0, "以 %PDF- 开头");
        OK(d.rfind("%%EOF") != std::string::npos, "以 %%EOF 结尾");
        std::string why;
        { bool xok = checkXref(d, why); OK(xok, "xref 偏移全部正确: " + why); }
        // 必须有 Type0 + CIDFontType2 + ToUnicode
        OK(d.find("/Subtype /Type0") != std::string::npos, "有 Type0 字体");
        OK(d.find("/CIDFontType2") != std::string::npos, "有 CIDFontType2");
        OK(d.find("/ToUnicode") != std::string::npos, "有 ToUnicode（决定能否选中/搜索）");
        OK(d.find("/FontFile2") != std::string::npos, "有 FontFile2（嵌入字形数据）");
        OK(d.find("/Identity-H") != std::string::npos, "使用 Identity-H 编码");
        // /W 必须是 [c [...]] 分组格式，不能是裸数字串
        {
            size_t i = d.find("/DW 1000 /W");
            OK(i != std::string::npos, "有 /W 宽度数组");
            if (i != std::string::npos) {
                size_t j = d.find("[", i);
                OK(d[j + 1] != ' ' && (d[j + 1] >= '0' && d[j + 1] <= '9'), "/W 以起始 CID 开头");
                // 紧随数字之后应是空格再 '['
                size_t k = j + 1;
                while (d[k] >= '0' && d[k] <= '9') k++;
                OK(d[k] == ' ' && d[k + 1] == '[', "/W 格式为 [起始CID [宽度...]]");
            }
        }
        // 每个 stream 的 /Length 必须真实
        {
            size_t pos = 0; int nStream = 0; bool allOk = true;
            while ((pos = d.find(" /Length ", pos)) != std::string::npos) {
                size_t s = pos + 9;
                long long len = 0;
                while (d[s] >= '0' && d[s] <= '9') { len = len * 10 + (d[s] - '0'); s++; }
                size_t st = d.find("stream\n", s);
                if (st == std::string::npos) { allOk = false; break; }
                size_t dataStart = st + 7;
                // 从 dataStart 起 len 字节之后应紧跟 endstream（可有一个换行）
                size_t e = dataStart + (size_t)len;
                if (e > d.size()) { allOk = false; break; }
                std::string tail = d.substr(e, 12);
                if (tail.compare(0, 9, "endstream") != 0 &&
                    tail.compare(1, 9, "endstream") != 0) { allOk = false; break; }
                nStream++;
                pos = s;
            }
            OK(nStream >= 2, "至少 2 个 stream（内容流 + 字体 + ToUnicode）");
            OK(allOk, "所有 /Length 都指向真实的 endstream 位置");
        }
    }

    // ------------------------------------------------------------------
    std::cout << "== 表格 -> PDF ==\n";
    {
        Sheet sh;
        sh.setName("数据");
        sh.setValue(0, 0, Value::str("地区"));
        sh.setValue(1, 0, Value::str("销售额"));
        sh.setValue(0, 1, Value::str("华北"));
        sh.setValue(1, 1, Value::num(1200));
        std::string e;
        sh.setFormula(1, 2, "SUM(B2:B2)");
        // 造够多行触发分页
        for (int r = 5; r < 80; r++) sh.setValue(0, r, Value::str(std::string("行") + std::to_string(r)));

        SheetPdfOptions o;
        o.fontPath = kFont;
        o.title = "测试导出";
        std::string err;
        std::string p2 = kTmp + "_sheet.pdf";
        OK(sheetToPdf(sh, p2, o, err), "导出: " + err);
        std::string d = readFile(p2);
        OK(d.compare(0, 5, "%PDF-") == 0, "表格 PDF 头正确");
        std::string why;
        { bool xok = checkXref(d, why); OK(xok, "表格 PDF xref 正确: " + why); }
        // 分页：70+ 行在 A4 纵向必然超过一页
        OK(d.find("/Count 1") == std::string::npos, "发生了分页（不是单页）");
        // 空表要报错而不是生成空文件
        Sheet empty;
        OK(!sheetToPdf(empty, kTmp + "_empty.pdf", o, err), "空表拒绝导出");
    }

    // ------------------------------------------------------------------
    std::cout << "== 图表 -> PDF ==\n";
    {
        Chart c;
        c.type = ChartType::Column;
        c.title = "季度销售";
        c.categories = {"华北", "华东", "华南"};
        DataSeries s; s.name = "线上"; s.values = {120, 165, 143};
        c.series.push_back(s);
        c.width = 900; c.height = 560;
        ChartPdfOptions o; o.fontPath = kFont;
        std::string err;
        std::string p3 = kTmp + "_chart.pdf";
        OK(chartToPdf(c, p3, o, err), "导出: " + err);
        std::string d = readFile(p3);
        std::string why;
        { bool xok = checkXref(d, why); OK(xok, "图表 PDF xref 正确: " + why); }
        OK(d.find("/Type0") != std::string::npos, "图表 PDF 有嵌入字体");

        // 全部类型都能导出
        ChartType types[] = {ChartType::Column, ChartType::StackedColumn, ChartType::Bar,
                             ChartType::Line, ChartType::Area, ChartType::StackedArea,
                             ChartType::Pie, ChartType::Scatter, ChartType::Histogram};
        for (auto t : types) {
            Chart c2; c2.type = t;
            c2.categories = {"甲", "乙", "丙", "丁"};
            c2.title = "T";
            DataSeries a; a.name = "A"; a.values = {3, 7, 5, 9}; a.xs = {1, 2, 3, 4};
            DataSeries b; b.name = "B"; b.values = {4, 6, 8, 2}; b.xs = {1, 2, 3, 4};
            c2.series = {a, b};
            c2.width = 800; c2.height = 500;
            std::string e2;
            OK(chartToPdf(c2, kTmp + "_t.pdf", o, e2), "类型导出成功: " + e2);
        }
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
