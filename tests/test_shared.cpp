// 共享公式（t="shared"）偏移展开测试
//
// OOXML 里一块共享公式只写一次文本：
//   主格   <f t="shared" si="0" ref="B1:B5">A1*2</f>   带文本与 ref
//   引用格 <f t="shared" si="0"/>                       只有 si，无文本
//
// 正确做法是把主格公式按与锚点的偏移量平移后写入每个引用格。
// **只保留已算出的值是不够的**：那样改了源数据，引用格不会跟着变，
// 看起来数据对，实际上公式已经死了。
//
// 这一组用手工构造的 xlsx 验证（openpyxl 不产生共享公式）。
#include "xlsx.hpp"
#include "sheet.hpp"
#include "zip.hpp"
#include <iostream>
#include <fstream>

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

// 手工构造一个含共享公式的 xlsx
static bool makeSharedFile(const std::string& path, const std::string& masterRef,
                           const std::string& masterFormula, int rows) {
    // 复用现有 xlsx 的包结构，只替换 sheet1.xml
    std::ifstream probe("out/示例.xlsx", std::ios::binary);
    if (!probe) return false;
    probe.close();

    std::string body;
    body += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    body += "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";
    body += "<sheetData>\n";
    for (int i = 1; i <= rows; i++) {
        body += "<row r=\"" + std::to_string(i) + "\">";
        body += "<c r=\"A" + std::to_string(i) + "\"><v>" + std::to_string(i) + "</v></c>";
        body += "<c r=\"B" + std::to_string(i) + "\">";
        if (i == 1)
            body += "<f t=\"shared\" ref=\"" + masterRef + "\" si=\"0\">" + masterFormula + "</f>";
        else
            body += "<f t=\"shared\" si=\"0\"/>";
        body += "<v>" + std::to_string(i * 2) + "</v></c>";   // 缓存值故意写成 i*2
        body += "</row>\n";
    }
    body += "</sheetData>\n</worksheet>";

    // 用 zip 库重写 sheet1.xml：借助 Workbook 先存一份再替换太绕，
    // 这里直接调 ZipReader / ZipWriter
    ZipReader zr;
    std::string err;
    if (!zr.open("out/示例.xlsx", err)) return false;
    ZipWriter zw;
    for (const ZipEntry& e : zr.entries()) {
        // 除 sheet1.xml 外原样搬过去（用解压后的数据重压，内容等价）
        if (e.name == "xl/worksheets/sheet1.xml") { zw.addFileStr(e.name, body); continue; }
        zw.addFile(e.name, e.data);
    }
    return zw.writeTo(path, err);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    std::cout << "== 共享公式偏移展开 ==\n";
    {
        // 主格 B1 公式 "A1*2"，区域 B1:B5
        OK(makeSharedFile("/tmp/xl_shared1.xlsx", "B1:B5", "A1*2", 5), "构造共享公式文件");
        Workbook wb;
        std::string err;
        OK(wb.load("/tmp/xl_shared1.xlsx", err), "加载: " + err);
        Sheet& sh = wb.sheet(0);
        sh.recalc();

        // 1) 值正确
        for (int i = 0; i < 5; i++) {
            std::string want = std::to_string((i + 1) * 2);
            EQS(sh.display(1, i), want, "B" + std::to_string(i + 1) + " 的值");
        }
        // 2) 公式确实被展开（不是只剩缓存值）
        for (int i = 0; i < 5; i++) {
            const Sheet::CellRec* c = sh.find(1, i);
            OK(c && c->hasFormula, "B" + std::to_string(i + 1) + " 有公式");
            if (c && c->hasFormula)
                EQS(c->formula, "A" + std::to_string(i + 1) + "*2",
                    "B" + std::to_string(i + 1) + " 的公式已按行偏移");
        }
        // 3) 关键：改源数据后引用格要跟着变 —— 只有公式真正生效才会如此
        sh.setValue(0, 0, Value::num(100));
        sh.recalc();
        EQS(sh.display(1, 0), "200", "改 A1 后 B1 跟着变（公式活着，不是死值）");
        EQS(sh.display(1, 4), "10", "B5 仍随 A5=5 计算");
    }

    std::cout << "== 绝对引用不被平移 ==\n";
    {
        OK(makeSharedFile("/tmp/xl_shared2.xlsx", "B1:B3", "A$1*2", 3), "构造 $1 绝对行");
        Workbook wb; std::string err;
        OK(wb.load("/tmp/xl_shared2.xlsx", err), "加载: " + err);
        Sheet& sh = wb.sheet(0);
        sh.recalc();
        // 所有引用格都该引用 A1（行绝对），所以值都是 1*2=2
        EQS(sh.display(1, 0), "2", "B1 = A1*2");
        EQS(sh.display(1, 1), "2", "B2 = A$1*2（行绝对，不平移）");
        EQS(sh.display(1, 2), "2", "B3 = A$1*2（行绝对，不平移）");
        const Sheet::CellRec* c = sh.find(1, 2);
        OK(c && c->hasFormula, "B3 有公式");
        if (c && c->hasFormula) EQS(c->formula, "A$1*2", "B3 公式保持 $1");
    }

    std::cout << "== 找不到主格时降级并告警 ==\n";
    {
        // si="9" 但没有任何主格
        std::string body =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<sheetData>"
            "<row r=\"1\"><c r=\"A1\"><v>7</v></c><c r=\"B1\"><f t=\"shared\" si=\"9\"/><v>14</v></c></row>"
            "</sheetData></worksheet>";
        ZipReader zr; std::string err;
        if (zr.open("out/示例.xlsx", err)) {
            ZipWriter zw;
            for (const ZipEntry& e : zr.entries()) {
                if (e.name == "xl/worksheets/sheet1.xml") { zw.addFileStr(e.name, body); continue; }
                zw.addFile(e.name, e.data);
            }
            zw.writeTo("/tmp/xl_shared3.xlsx", err);
        }
        Workbook wb;
        OK(wb.load("/tmp/xl_shared3.xlsx", err), "加载: " + err);
        // 不崩、值保留为常量
        EQS(wb.sheet(0).display(1, 0), "14", "降级为已算出的值");
        bool warned = false;
        for (const std::string& w : wb.warnings())
            if (w.find("找不到主格") != std::string::npos) warned = true;
        OK(warned, "给出找不到主格的告警（不静默）");
    }

    std::cout << "== 主格自身 si=0 也能正确处理 ==\n";
    {
        // 判别主格靠"有没有 ref"，不能靠 si 的值：
        // si="0" 既可能是主格也可能是引用格，用 si 判断会漏掉真正的引用格
        OK(makeSharedFile("/tmp/xl_shared4.xlsx", "B1:B4", "A1+1", 4), "构造");
        Workbook wb; std::string err;
        OK(wb.load("/tmp/xl_shared4.xlsx", err), "加载: " + err);
        Sheet& sh = wb.sheet(0);
        sh.recalc();
        EQS(sh.display(1, 0), "2", "B1 = A1+1 = 2");
        const Sheet::CellRec* c = sh.find(1, 3);
        OK(c && c->hasFormula, "B4 有公式（说明引用格未被漏掉）");
        if (c && c->hasFormula) EQS(c->formula, "A4+1", "B4 公式已偏移");
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
