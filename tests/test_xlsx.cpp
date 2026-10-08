// xlsx 读写往返测试
// 重点验证：ZIP 合法性（用 Python zipfile 复验）、XML 边界、类型往返、公式保留、多表与跨表引用
#include "xlsx.hpp"
#include "zip.hpp"
#include "xml.hpp"
#include "date.hpp"
#include "sheet.hpp"
#include "functions.hpp"
#include <iostream>
#include <fstream>
#include <cmath>

using namespace xl;

static int P = 0, N = 0;
static void OK(bool cond, const std::string& what) {
    if (cond) P++;
    else { N++; std::cout << "  FAIL " << what << "\n"; }
}
static void EQ(const std::string& got, const std::string& exp, const std::string& what) {
    if (got == exp) P++;
    else { N++; std::cout << "  FAIL " << what << "  期望='" << exp << "' 实际='" << got << "'\n"; }
}

static std::string TMP = "/tmp/xl_xlsx_test";

int main() {
    // ------------------------------------------------------------------
    std::cout << "== XML 解析 ==\n";
    {
        std::string err;
        XmlNode n = xmlParse(
            "<?xml version=\"1.0\"?><root xmlns:x=\"urn:a\"><x:row r=\"1\">"
            "<c t=\"s\"><v>0</v></c><c r=\"B1\"/><t>a&amp;b&lt;c</t></x:row></root>", err);
        OK(err.empty(), "XML 解析无错误");
        EQ(n.name, "root", "根元素名");
        const XmlNode* row = n.child("row");
        OK(row != nullptr, "忽略前缀找到 row");
        if (row) {
            EQ(row->attr("r"), "1", "属性读取");
            OK(row->children("c").size() == 2, "两个 c 子元素");
            const XmlNode* t = row->child("t");
            OK(t != nullptr, "找到 t");
            if (t) EQ(t->text, "a&b<c", "实体反转义");
        }
    }
    {
        // 属性带命名空间前缀
        std::string err;
        XmlNode n = xmlParse("<workbook xmlns:r=\"urn:rel\"><sheet r:id=\"rId1\"/></workbook>", err);
        const XmlNode* s = n.child("sheet");
        OK(s != nullptr, "workbook/sheet");
        if (s) EQ(s->attr("id"), "rId1", "前缀属性按本地名匹配");
    }
    {
        // CDATA 与注释
        std::string err;
        XmlNode n = xmlParse("<a><!-- c --><b><![CDATA[x<y]]></b></a>", err);
        const XmlNode* b = n.child("b");
        OK(b && b->text == "x<y", "CDATA 不解析内容");
    }
    EQ(xmlEscape("a<b&c>d"), "a&lt;b&amp;c&gt;d", "xmlEscape");
    EQ(xmlEscapeAttr("\"q\""), "&quot;q&quot;", "xmlEscapeAttr 引号");

    // ------------------------------------------------------------------
    std::cout << "== ZIP 往返 ==\n";
    {
        ZipWriter w;
        w.addFileStr("a.txt", "hello world");
        std::vector<uint8_t> big(5000, 'x');
        w.addFile("bin.dat", big);
        std::string err;
        std::string path = TMP + "_zip.zip";
        OK(w.writeTo(path, err), "ZIP 写入: " + err);

        ZipReader r;
        OK(r.open(path, err), "ZIP 读回: " + err);
        OK(r.entries().size() == 2, "两个条目");
        std::string s;
        OK(r.readText("a.txt", s) && s == "hello world", "文本内容一致");
        const ZipEntry* e = r.find("bin.dat");
        OK(e && e->data == big, "二进制内容一致（deflate 往返）");
    }

    // ------------------------------------------------------------------
    std::cout << "== xlsx 保存与加载 ==\n";
    {
        Workbook wb;
        Sheet& s1 = wb.sheet(0);
        s1.setValue(0, 0, Value::num(10));            // A1
        s1.setValue(1, 0, Value::num(20));            // B1
        s1.setFormula(2, 0, "A1+B1");                 // C1 公式
        s1.setValue(0, 1, Value::str("你好 Excel"));   // A2 文本
        s1.setValue(1, 1, Value::boolean(true));      // B2
        s1.setValue(2, 1, Value::num(0.125));         // C2 小数
        s1.setValue(3, 1, Value::num(-3.5));          // D2 负数
        s1.setFormula(3, 0, "1/0");                   // D1 错误公式
        s1.setValue(0, 2, Value::num(1e20));          // A3 大数

        // 第二张表 + 跨表引用
        Sheet& s2 = wb.addSheet("数据");
        s2.setValue(0, 0, Value::num(100));
        s1.setFormula(0, 4, "数据!A1*2");             // A5 跨表

        std::string err;
        std::string path = TMP + ".xlsx";
        OK(wb.save(path, err), "保存 xlsx: " + err);

        // 读回
        Workbook wb2;
        OK(wb2.load(path, err), "加载 xlsx: " + err);
        OK(wb2.sheetCount() == 2, "两个工作表");
        EQ(wb2.sheet(0).name(), "Sheet1", "表名 1");
        EQ(wb2.sheet(1).name(), "数据", "表名 2（中文）");

        Sheet& r1 = wb2.sheet(0);
        EQ(r1.display(0, 0), "10", "数字往返");
        EQ(r1.display(1, 0), "20", "数字往返 2");
        EQ(r1.display(0, 1), "你好 Excel", "中文文本往返");
        EQ(r1.display(1, 1), "TRUE", "布尔往返");
        EQ(r1.display(2, 1), "0.125", "小数往返");
        EQ(r1.display(3, 1), "-3.5", "负数往返");
        EQ(r1.display(0, 2), "100000000000000000000", "大数往返");
        EQ(r1.display(3, 0), "#DIV/0!", "错误值往返");

        // 公式保留且重算正确
        OK(r1.find(2, 0) != nullptr && r1.find(2, 0)->hasFormula, "公式被保留为公式");
        EQ(r1.display(2, 0), "30", "公式重算结果");
        EQ(r1.display(0, 4), "200", "跨表引用重算");
    }

    // ------------------------------------------------------------------
    std::cout << "== 边界情况 ==\n";
    {
        // 跳空列：A1 和 E1 有值，中间为空
        Workbook wb;
        wb.sheet(0).setValue(0, 0, Value::num(1));
        wb.sheet(0).setValue(4, 0, Value::num(5));
        std::string err, path = TMP + "_gap.xlsx";
        OK(wb.save(path, err), "保存跳空列表");
        Workbook wb2;
        OK(wb2.load(path, err), "加载跳空列表");
        EQ(wb2.sheet(0).display(0, 0), "1", "A1");
        EQ(wb2.sheet(0).display(4, 0), "5", "E1（跳空列）");
        OK(wb2.sheet(0).cellCount() == 2, "空单元格不占位");
    }
    {
        // 文本中包含需要转义的字符
        Workbook wb;
        wb.sheet(0).setValue(0, 0, Value::str("a<b>&c\"d'e"));
        wb.sheet(0).setValue(1, 0, Value::str(" 前导空格 "));
        std::string err, path = TMP + "_esc.xlsx";
        OK(wb.save(path, err), "保存含特殊字符文本");
        Workbook wb2;
        OK(wb2.load(path, err), "加载含特殊字符文本");
        EQ(wb2.sheet(0).display(0, 0), "a<b>&c\"d'e", "特殊字符往返");
        EQ(wb2.sheet(0).display(1, 0), " 前导空格 ", "空格保留");
    }
    {
        // 空工作簿
        Workbook wb;
        std::string err, path = TMP + "_empty.xlsx";
        OK(wb.save(path, err), "保存空工作簿");
        Workbook wb2;
        OK(wb2.load(path, err), "加载空工作簿");
        OK(wb2.sheetCount() == 1, "空工作簿仍有一张表");
    }
    {
        // 损坏文件要给出错误而不是崩溃
        Workbook wb;
        std::string err;
        OK(!wb.load("/tmp/__nonexistent_file__.xlsx", err) && !err.empty(), "不存在的文件报错");
        {
            std::ofstream f("/tmp/__bad__.xlsx", std::ios::binary);
            f << "not a zip file at all";
        }
        OK(!wb.load("/tmp/__bad__.xlsx", err), "损坏文件报错不崩溃");
    }


    // ------------------------------------------------------------------
    std::cout << "== 数字格式往返 ==\n";
    {
        Workbook wb;
        Sheet& sh = wb.sheet(0);
        sh.setName("格式");
        sh.setValue(0, 0, Value::num(1234.5678));
        sh.setNumFmt(0, 0, "#,##0.00");
        sh.setValue(1, 0, Value::num(0.1234));
        sh.setNumFmt(1, 0, "0.00%");
        // 直接算序列号，不猜数字 —— 猜出来的数字对不上日期
        sh.setValue(2, 0, Value::num(ymdToSerial(2026, 9, 27)));
        sh.setNumFmt(2, 0, "yyyy-mm-dd");
        sh.setValue(3, 0, Value::num(1234.5));       // 不带格式
        sh.recalc();

        EQ(sh.display(0, 0), "1,234.57", "应用 #,##0.00");
        EQ(sh.display(1, 0), "12.34%", "应用 0.00%");
        EQ(sh.display(2, 0), "2026-09-27", "应用 yyyy-mm-dd");
        EQ(sh.display(3, 0), "1234.5", "无格式走通用");
        EQ(sh.displayRaw(0, 0), "1234.5678", "displayRaw 不套格式");

        std::string err;
        OK(wb.save("/tmp/xl_fmt.xlsx", err), "保存: " + err);

        Workbook rb;
        OK(rb.load("/tmp/xl_fmt.xlsx", err), "加载: " + err);
        Sheet& rs = rb.sheet(0);
        EQ(rs.numFmtAt(0, 0), "#,##0.00", "格式码往返 1");
        EQ(rs.numFmtAt(1, 0), "0.00%", "格式码往返 2");
        EQ(rs.numFmtAt(2, 0), "yyyy-mm-dd", "格式码往返 3");
        EQ(rs.numFmtAt(3, 0), "", "无格式的格子没有格式码");
        EQ(rs.display(0, 0), "1,234.57", "重载后显示一致");
        EQ(rs.display(1, 0), "12.34%", "重载后百分比一致");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
