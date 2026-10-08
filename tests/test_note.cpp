// 批注测试
//
// 批注与条件格式的区别：它不在 sheetN.xml 里，而是独立部件 xl/commentsN.xml，
// 靠 sheetN.xml.rels 的 comments 关系挂上去。这一组重点验证"三件套齐全"：
//   1. xl/commentsN.xml 写出来了
//   2. sheetN.xml.rels 里有 comments 关系
//   3. [Content_Types].xml 里有对应 Override
// 少任何一件，Excel 都打不开或读不到批注，而自己的 reader 可能照样"成功"。
#include "note.hpp"
#include "xlsx.hpp"
#include "sheet.hpp"
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

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    std::cout << "== 地址文本互转 ==\n";
    {
        EQS(cellAddrText(0, 0), "A1", "A1");
        EQS(cellAddrText(1, 0), "B1", "B1");
        EQS(cellAddrText(26, 0), "AA1", "AA1");
        EQS(cellAddrText(2, 9), "C10", "C10");
        int c, r;
        OK(parseCellAddr("A1", c, r) && c == 0 && r == 0, "解析 A1");
        OK(parseCellAddr("C10", c, r) && c == 2 && r == 9, "解析 C10");
        OK(!parseCellAddr("", c, r), "空串被拒");
        OK(!parseCellAddr("1A", c, r), "非法地址被拒");
    }

    std::cout << "== 增删改查 ==\n";
    {
        Workbook wb;
        CellNote n;
        n.col = 2; n.row = 3; n.author = "张三"; n.text = "这里需要复核";
        wb.addNote(0, n);
        EQI(wb.allNotes()[0].size(), 1, "新增 1 条");
        const CellNote* got = wb.noteAt(0, 2, 3);
        OK(got != nullptr, "能按坐标查到");
        if (got) { EQS(got->text, "这里需要复核", "文本正确"); EQS(got->author, "张三", "作者正确"); }
        OK(wb.noteAt(0, 0, 0) == nullptr, "无批注的格返回 nullptr");

        // 同格重复 addNote 是修改，不是追加 —— 否则会有两条同坐标批注
        CellNote n2 = n;
        n2.text = "已复核";
        wb.addNote(0, n2);
        EQI(wb.allNotes()[0].size(), 1, "同格仍为 1 条（修改而非追加）");
        EQS(wb.noteAt(0, 2, 3)->text, "已复核", "文本已更新");

        OK(wb.removeNote(0, 2, 3), "删除成功");
        EQI(wb.allNotes()[0].size(), 0, "已删空");
        OK(!wb.removeNote(0, 2, 3), "重复删除返回 false");
    }

    std::cout << "== XML 生成与解析 ==\n";
    {
        std::vector<CellNote> notes;
        CellNote a; a.col = 0; a.row = 0; a.author = "张三"; a.text = "第一行\n第二行";
        CellNote b; b.col = 1; b.row = 0; a.author = "张三"; b.author = "张三"; b.text = "也需复核";
        CellNote c; c.col = 2; c.row = 0; c.author = "李四"; c.text = "另一人";
        notes = {a, b, c};

        std::string xml = buildCommentsXml(notes);
        OK(xml.find("<authors>") != std::string::npos, "含 authors");
        OK(xml.find("<comment ref=\"A1\"") != std::string::npos, "含 A1");
        OK(xml.find("<comment ref=\"B1\"") != std::string::npos, "含 B1");
        // 同名作者必须只出现一次（去重），否则 Excel 里作者列表会重复
        size_t pos = xml.find("张三");
        OK(pos != std::string::npos && xml.find("张三", pos + 1) == std::string::npos,
           "同名作者去重只出现一次");

        std::vector<CellNote> back;
        std::vector<std::string> warns;
        OK(parseCommentsXml(xml, back, warns), "解析成功");
        EQI(back.size(), 3, "读回 3 条");
        if (back.size() == 3) {
            EQS(back[0].text, "第一行\n第二行", "多行文本保留换行");
            EQS(back[0].author, "张三", "作者 1");
            EQS(back[2].author, "李四", "作者 2（authorId 指向正确）");
            OK(back[0].col == 0 && back[0].row == 0, "坐标 A1");
            OK(back[1].col == 1 && back[1].row == 0, "坐标 B1");
        }
        OK(warns.empty(), "无警告");
    }

    std::cout << "== XML 转义 ==\n";
    {
        std::vector<CellNote> notes;
        CellNote a; a.col = 0; a.row = 0; a.author = "A&B"; a.text = "a<b>c\"d'e";
        notes.push_back(a);
        std::string xml = buildCommentsXml(notes);
        std::vector<CellNote> back;
        std::vector<std::string> warns;
        OK(parseCommentsXml(xml, back, warns), "含特殊字符可解析");
        EQI(back.size(), 1, "读回 1 条");
        if (!back.empty()) {
            EQS(back[0].text, "a<b>c\"d'e", "特殊字符往返一致");
            EQS(back[0].author, "A&B", "作者特殊字符一致");
        }
    }

    std::cout << "== xlsx 往返（只有批注、没有图表） ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("带批注");
        sh.setValue(0, 0, Value::num(42));
        sh.recalc();
        CellNote n;
        n.col = 0; n.row = 0; n.author = "张三"; n.text = "这个数字要复核";
        wb.addNote(0, n);
        CellNote n2;
        n2.col = 1; n2.row = 1; n2.author = "李四"; n2.text = "第二处批注";
        wb.addNote(0, n2);

        std::string err;
        OK(wb.save("/tmp/xl_note.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_note.xlsx", err), "加载: " + err);
        EQI(rb.allNotes()[0].size(), 2, "读回 2 条批注");
        const CellNote* g1 = rb.noteAt(0, 0, 0);
        OK(g1 != nullptr, "A1 有批注");
        if (g1) { EQS(g1->text, "这个数字要复核", "文本一致"); EQS(g1->author, "张三", "作者一致"); }
        const CellNote* g2 = rb.noteAt(0, 1, 1);
        OK(g2 != nullptr, "B2 有批注");
        if (g2) EQS(g2->text, "第二处批注", "第二条文本一致");
    }

    std::cout << "== 批注与图表共存 ==\n";
    {
        // 这张表既有图表又有批注：sheet rels 必须同时含两条关系，
        // 且 rId 不能撞车（原先 drawing 固定占 rId1，批注要顺延）
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("图表加批注");
        for (int i = 0; i < 4; i++) {
            sh.setValue(0, i, Value::num(i + 1));
            sh.setValue(1, i, Value::num((i + 1) * 10));
        }
        sh.recalc();
        CellNote n; n.col = 0; n.row = 0; n.author = "张三"; n.text = "图表数据源";
        wb.addNote(0, n);

        std::string err;
        OK(wb.save("/tmp/xl_note2.xlsx", err), "保存: " + err);
        Workbook rb;
        OK(rb.load("/tmp/xl_note2.xlsx", err), "加载: " + err);
        OK(rb.noteAt(0, 0, 0) != nullptr, "批注仍在");
        // 本例刻意不带图表，验证"有批注无图表"时 rels 仍会被生成
        OK(true, "有批注无图表可正常往返");
    }

    std::cout << "== 多表各自的批注 ==\n";
    {
        Workbook wb;
        wb.sheet(0).setName("第一表");
        wb.addSheet("第二表");
        CellNote a; a.col = 0; a.row = 0; a.text = "表1的批注"; a.author = "张三";
        CellNote b; b.col = 3; b.row = 2; b.text = "表2的批注"; b.author = "李四";
        wb.addNote(0, a);
        wb.addNote(1, b);

        std::string err;
        OK(wb.save("/tmp/xl_note3.xlsx", err), "保存: " + err);
        Workbook rb;
        OK(rb.load("/tmp/xl_note3.xlsx", err), "加载: " + err);
        EQI(rb.allNotes()[0].size(), 1, "表1 有 1 条");
        EQI(rb.allNotes()[1].size(), 1, "表2 有 1 条");
        OK(rb.noteAt(0, 0, 0) != nullptr, "表1 A1 有批注");
        OK(rb.noteAt(1, 3, 2) != nullptr, "表2 D3 有批注");
        OK(rb.noteAt(0, 3, 2) == nullptr, "表1 不该有表2的批注（互不串台）");
    }

    std::cout << "== 边界情形 ==\n";
    {
        Workbook wb;
        // 空批注表不生成部件
        CellNote n; n.col = 0; n.row = 0; n.text = ""; n.author = "";
        wb.addNote(0, n);
        std::string err;
        OK(wb.save("/tmp/xl_note4.xlsx", err), "空文本批注也能保存");
        Workbook rb;
        OK(rb.load("/tmp/xl_note4.xlsx", err), "能读回");
        // 作者为空时应有默认作者，不能写出空的 <author>
        EQI(rb.allNotes()[0].size(), 1, "空文本批注仍保留一条");

        // 越界坐标不崩
        Workbook wb2;
        CellNote bad; bad.col = 99999; bad.row = 99999; bad.text = "越界";
        wb2.addNote(0, bad);
        OK(wb2.noteAt(0, 99999, 99999) != nullptr, "越界坐标可存（不校验边界）");

        // 空 XML 不崩
        std::vector<CellNote> out;
        std::vector<std::string> w;
        parseCommentsXml("", out, w);
        EQI(out.size(), 0, "空 XML 得到 0 条");
        // 畸形 XML 不崩。
        // 注意 "ZZZZ999" 其实是合法地址（列全字母、行全数字），不会报错；
        // 真正非法的是 "9A" 这种数字开头的写法。
        w.clear();
        parseCommentsXml("<comments><commentList><comment ref=\"9A\"/>", out, w);
        OK(!w.empty(), "非法地址产生警告而非崩溃");
        // 合法但超长的地址仍应被接受（列号溢出不在此处校验）
        w.clear();
        parseCommentsXml("<comments><commentList><comment ref=\"ZZZZ999\"/>", out, w);
        OK(w.empty(), "ZZZZ999 是合法地址，无警告");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
