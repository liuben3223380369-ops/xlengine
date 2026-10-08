// 终端界面测试
//
// 重点不是"画得好不好看"，而是那些错了不会被肉眼立刻发现、
// 但会让整个网格错位或数据丢失的地方：
//   - CJK 按 2 列算宽：一个"销售额"按 3 列裁切会吃掉后面一列
//   - 退格按码点删：按字节删会留下半个汉字
//   - 输入 12abc 不能静默变成 12
//   - 光标移出视口时要滚动，不是消失
//   - 编辑公式后必须真的重算
#include "tui.hpp"
#include "sheet.hpp"
#include <iostream>
#include <cmath>
#include <fstream>

using namespace xl;

static int P = 0, N = 0;
static void OK(bool c, const std::string& what) {
    if (c) P++; else { N++; std::cout << "  FAIL " << what << "\n"; }
}
static void EQI(int g, int e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望=" << e << " 实际=" << g << "\n"; }
}
static void EQD(double g, double e, const std::string& what) {
    if (std::fabs(g - e) < 1e-6) P++;
    else { N++; std::cout << "  FAIL " << what << " 期望=" << e << " 实际=" << g << "\n"; }
}
static void EQS(const std::string& g, const std::string& e, const std::string& what) {
    if (g == e) P++; else { N++; std::cout << "  FAIL " << what << " 期望='" << e << "' 实际='" << g << "'\n"; }
}

static KeyEvent keyOf(const std::string& bytes) {
    auto v = parseKeys(bytes);
    return v.empty() ? KeyEvent{} : v[0];
}

int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    // ------------------------------------------------------------------
    std::cout << "== 显示宽度 ==\n";
    {
        EQI(displayWidth("abc"), 3, "ASCII 宽度");
        EQI(displayWidth("销售额"), 6, "3 个汉字宽 6 列");
        EQI(displayWidth(""), 0, "空串宽 0");
        EQI(displayWidth("a销"), 3, "混合：1 + 2");
        EQI(codePointWidth(U'A'), 1, "'A' 宽 1");
        EQI(codePointWidth(0x4E00), 2, "CJK 宽 2");

        // 裁切：宽度不足时加省略号，且结果不能超过目标宽度
        std::string f = fitToWidth("销售额度统计", 7);
        EQI(displayWidth(f), 7, "裁切后正好 7 列");
        OK(f.find("\xE2\x80\xA6") != std::string::npos, "裁切带省略号");
        EQS(fitToWidth("ab", 5), "ab", "不超宽时原样返回");
        EQI(displayWidth(fitToWidth("abc", 0)), 0, "宽度 0 返回空");

        // 右对齐
        EQS(padLeftTo("7", 4), "   7", "右对齐补空格");
        EQI(displayWidth(padLeftTo("销售额", 10)), 10, "CJK 右对齐按显示宽度");
    }

    // ------------------------------------------------------------------
    std::cout << "== 屏幕缓冲 ==\n";
    {
        Screen s(20, 5);
        s.putStr(0, 0, "hello", {});
        char32_t c; Attr a;
        s.at(0, 0, c, a); EQI((int)c, (int)'h', "putStr 写入 h");
        s.at(4, 0, c, a); EQI((int)c, (int)'o', "putStr 写入 o");
        s.at(5, 0, c, a); EQI((int)c, (int)' ', "越界位置仍是空格");
        // CJK 占两列，第二列是占位符 0
        Screen s2(20, 3);
        s2.putStr(0, 0, "销", {});
        s2.at(0, 0, c, a); EQI((int)c, (int)0x9500, "汉字写入首列");
        s2.at(1, 0, c, a); EQI((int)c, 0, "第二列是占位 0");
        // toPlainText 去尾空格且跳过占位
        EQS(s2.toPlainText().substr(0, 3), "\xe9\x94\x80", "纯文本输出汉字");

        Screen s3(10, 2);
        s3.fillRow(0, "ab", {});
        std::string t = s3.toPlainText();
        OK(t.find('\n') != std::string::npos, "两行之间换行");
        EQS(s3.toPlainText().substr(0, 2), "ab", "fillRow 内容");
    }

    // ------------------------------------------------------------------
    std::cout << "== 按键解析 ==\n";
    {
        EQI((int)keyOf("\x1b[A").key, (int)Key::Up, "上箭头");
        EQI((int)keyOf("\x1b[B").key, (int)Key::Down, "下箭头");
        EQI((int)keyOf("\x1b[C").key, (int)Key::Right, "右箭头");
        EQI((int)keyOf("\x1b[D").key, (int)Key::Left, "左箭头");
        EQI((int)keyOf("\r").key, (int)Key::Enter, "回车");
        EQI((int)keyOf("\x1b").key, (int)Key::Escape, "ESC");
        EQI((int)keyOf("\x7f").key, (int)Key::Backspace, "退格");
        EQI((int)keyOf("\x1b[5~").key, (int)Key::PageUp, "PageUp");
        EQI((int)keyOf("\x1b[6~").key, (int)Key::PageDown, "PageDown");
        KeyEvent kc = keyOf("A");
        EQI((int)kc.key, (int)Key::Char, "'A' 是字符");
        EQI((int)kc.ch, (int)'A', "'A' 的码点");
        // 多字节字符
        KeyEvent kz = keyOf("\xe4\xbd\xa0");
        EQI((int)kz.ch, (int)0x4F60, "汉字 '你' 解析为 U+4F60");
        // Ctrl+C
        KeyEvent kctrl = keyOf("\x03");
        OK(kctrl.ctrl, "Ctrl+C 标记为 ctrl");
        EQI((int)kctrl.ch, (int)'c', "Ctrl+C 的字符");
        // 连续两个按键
        auto two = parseKeys("A\r");
        EQI((int)two.size(), 2, "一次解析出两个按键");
    }

    // ------------------------------------------------------------------
    std::cout << "== 光标与滚动 ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        GridUI ui; ui.attach(&wb);
        ui.resize(100, 20);
        EQI(ui.cursorCol(), 0, "初始在 A");
        EQI(ui.cursorRow(), 0, "初始在第 1 行");

        ui.handleKey(keyOf("\x1b[C"));
        EQI(ui.cursorCol(), 1, "右移一列");
        ui.handleKey(keyOf("\x1b[B"));
        EQI(ui.cursorRow(), 1, "下移一行");
        ui.handleKey(keyOf("\x1b[D"));
        EQI(ui.cursorCol(), 0, "左移回到 A");
        ui.handleKey(keyOf("\x1b[A"));
        EQI(ui.cursorRow(), 0, "上移回到第 1 行");
        // 边界不越界
        ui.handleKey(keyOf("\x1b[A"));
        EQI(ui.cursorRow(), 0, "顶部不越界");
        ui.handleKey(keyOf("\x1b[D"));
        EQI(ui.cursorCol(), 0, "左侧不越界");

        // 向下移动到视口外应触发滚动
        for (int i = 0; i < 40; i++) ui.handleKey(keyOf("\x1b[B"));
        EQI(ui.cursorRow(), 40, "移动 40 行");
        OK(ui.topRow() > 0, "视口已滚动（topRow > 0）");
        OK(ui.topRow() <= 40, "滚动位置不超过光标");
        // 向上回到顶部应滚回
        for (int i = 0; i < 40; i++) ui.handleKey(keyOf("\x1b[A"));
        EQI(ui.cursorRow(), 0, "回到第 1 行");
        EQI(ui.topRow(), 0, "视口滚回顶部");
    }

    // ------------------------------------------------------------------
    std::cout << "== 编辑与求值 ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        GridUI ui; ui.attach(&wb);
        ui.resize(100, 20);

        // 输入数字
        ui.handleKey(keyOf("1"));
        ui.handleKey(keyOf("2"));
        ui.handleKey(keyOf("3"));
        EQI((int)ui.mode(), (int)GridUI::Mode::Edit, "直接输入进入编辑态");
        EQS(ui.editBuffer(), "123", "编辑缓冲内容");
        ui.handleKey(keyOf("\r"));
        EQI((int)ui.mode(), (int)GridUI::Mode::Normal, "回车后回到浏览态");
        EQS(sh.display(0, 0), "123", "A1 写入 123");
        OK(sh.valueAt(0, 0).isNum(), "123 存为数值不是文本");

        // 输入公式
        ui.handleKey(keyOf("\x1b[B"));          // 到 A2
        ui.handleKey(keyOf("="));
        ui.handleKey(keyOf("A"));
        ui.handleKey(keyOf("1"));
        ui.handleKey(keyOf("*"));
        ui.handleKey(keyOf("2"));
        ui.handleKey(keyOf("\r"));
        EQS(sh.display(0, 1), "246", "公式 =A1*2 求出 246");

        // 12abc 不能变成 12
        ui.handleKey(keyOf("\x1b[B"));          // A3
        ui.handleKey(keyOf("1"));
        ui.handleKey(keyOf("2"));
        ui.handleKey(keyOf("a"));
        ui.handleKey(keyOf("b"));
        ui.handleKey(keyOf("c"));
        EQS(ui.editBuffer(), "12abc", "编辑缓冲是 12abc");
        ui.handleKey(keyOf("\r"));
        OK(!sh.valueAt(0, 2).isNum(), "12abc 不是数值");
        EQS(sh.display(0, 2), "12abc", "12abc 原样存为文本");

        // 汉字退格按码点删，不留半个字
        ui.handleKey(keyOf("\x1b[B"));          // A4
        ui.handleKey(keyOf("\xe4\xbd\xa0"));
        ui.handleKey(keyOf("\xe5\xa5\xbd"));
        ui.handleKey(keyOf("\x7f"));            // 退格
        EQS(ui.editBuffer(), "\xe4\xbd\xa0", "退格后只剩 '你'");
        EQI(displayWidth(ui.editBuffer()), 2, "退格后是完整的一个字");

        // ESC 取消编辑
        ui.handleKey(keyOf("\x1b"));
        EQI((int)ui.mode(), (int)GridUI::Mode::Normal, "ESC 退出编辑");

        // 公式栏显示原始公式
        std::string content = ui.currentContent();
        ui.handleKey(keyOf("\x1b[A"));          // 回到 A3（12abc）
        OK(!ui.currentContent().empty(), "公式栏有内容");
        (void)content;
    }

    // ------------------------------------------------------------------
    std::cout << "== 命令 ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setName("T");
        sh.setValue(0, 0, Value::str("地区"));
        sh.setValue(1, 0, Value::num(42));
        sh.setFormula(1, 1, "B1*2");
        sh.recalc();
        GridUI ui; ui.attach(&wb);
        ui.resize(100, 20);

        std::string msg;
        // 函数总数
        OK(ui.runCommand("funcs", msg), ":funcs 可执行");
        OK(msg.find("函数总数") != std::string::npos, ":funcs 返回总数: " + msg);
        // 跳转
        OK(ui.runCommand("goto C5", msg), ":goto 可执行");
        EQI(ui.cursorCol(), 2, "跳转到 C 列");
        EQI(ui.cursorRow(), 4, "跳转到第 5 行");
        OK(!ui.runCommand("goto zzz", msg), "无效地址被拒绝");
        // 未知命令
        OK(!ui.runCommand("nosuch", msg), "未知命令返回 false");
        OK(msg.find("未知命令") != std::string::npos, "未知命令有提示");

        // 保存 xlsx
        std::string p = "/tmp/xl_tui_test.xlsx";
        OK(ui.runCommand("w " + p, msg), ":w 保存: " + msg);
        {
            std::ifstream f(p, std::ios::binary);
            OK(f.good(), "保存的文件存在");
        }
        OK(!ui.dirty(), "保存后 dirty 清除");

        // 导出 PDF
        std::string pdf = "/tmp/xl_tui_test.pdf";
        OK(ui.runCommand("pdf " + pdf, msg), ":pdf 导出: " + msg);
        {
            std::ifstream f(pdf, std::ios::binary);
            OK(f.good(), "导出的 PDF 存在");
            if (f.good()) {
                f.seekg(0, std::ios::end);
                OK(f.tellg() > 500, "PDF 非空");
            }
        }

        // 重新加载
        Workbook wb2; Sheet& sh2 = wb2.sheet(0);
        GridUI ui2; ui2.attach(&wb2);
        ui2.resize(100, 20);
        OK(ui2.runCommand("e " + p, msg), ":e 加载: " + msg);
        // 注意：:e 是整体替换工作簿，之前拿到的 Sheet& 会失效，必须重新取
        Sheet& sh2b = wb2.sheet(0);
        EQS(sh2b.display(1, 0), "42", "加载后 B1 = 42");
        EQS(sh2b.display(1, 1), "84", "加载后公式重算 B2 = 84");

        // 退出
        OK(ui2.runCommand("q", msg), ":q 返回 true");
        OK(ui2.quitRequested(), ":q 置退出标志");
    }

    // ------------------------------------------------------------------
    std::cout << "== 渲染 ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setName("销售");
        sh.setValue(0, 0, Value::str("地区"));
        sh.setValue(1, 0, Value::str("销售额"));
        sh.setValue(0, 1, Value::str("华北"));
        sh.setValue(1, 1, Value::num(1200));
        sh.setFormula(1, 2, "B2/0");            // 制造一个错误值
        sh.recalc();
        GridUI ui; ui.attach(&wb);
        ui.resize(100, 24);
        Screen s(100, 24);
        ui.render(s);

        std::string txt = s.toPlainText();
        OK(txt.find("销售") != std::string::npos, "标题栏含表名");
        OK(txt.find("华北") != std::string::npos, "网格含中文内容");
        OK(txt.find("1200") != std::string::npos, "网格含数值");
        OK(txt.find("#DIV/0!") != std::string::npos, "错误值显示出来");
        OK(txt.find("地区") != std::string::npos, "表头显示");
        // 列标 A B C
        OK(txt.find("A") != std::string::npos, "有列标 A");
        // 每行不超过屏幕宽度（CJK 宽度处理正确才不会溢出）
        size_t pos = 0; bool noOverflow = true;
        while (pos < txt.size()) {
            size_t nl = txt.find('\n', pos);
            std::string line = txt.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            if (displayWidth(line) > 100) { noOverflow = false; break; }
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        OK(noOverflow, "所有行都不超出屏幕宽度（CJK 宽度正确）");
    }


    // ------------------------------------------------------------------
    std::cout << "== 修饰键解析 ==\n";
    {
        KeyEvent e = keyOf("\x1b[1;2A");
        EQI((int)e.key, (int)Key::Up, "Shift+上 仍是上");
        OK(e.shift, "Shift+上 带 shift 标记");
        KeyEvent e2 = keyOf("\x1b[A");
        OK(!e2.shift, "裸上箭头没有 shift");
        KeyEvent e3 = keyOf("\x1b[1;2C");
        EQI((int)e3.key, (int)Key::Right, "Shift+右");
        OK(e3.shift, "Shift+右 带 shift");
        // Ctrl+PageUp/PageDown -> 切表
        KeyEvent e4 = keyOf("\x1b[5;5~");
        EQI((int)e4.key, (int)Key::SheetPrev, "Ctrl+PageUp 切上一张表");
        KeyEvent e5 = keyOf("\x1b[6;5~");
        EQI((int)e5.key, (int)Key::SheetNext, "Ctrl+PageDown 切下一张表");
        KeyEvent e6 = keyOf("\x1b[5~");
        EQI((int)e6.key, (int)Key::PageUp, "裸 PageUp 是翻页");
    }

    // ------------------------------------------------------------------
    std::cout << "== 多表 ==\n";
    {
        Workbook wb;
        Sheet& s0 = wb.sheet(0); s0.setName("一月");
        s0.setValue(0, 0, Value::str("A表"));
        wb.addSheet("二月");
        wb.sheet(1).setValue(0, 0, Value::str("B表"));
        GridUI ui; ui.attach(&wb);
        ui.resize(100, 20);

        EQI(ui.sheetCount(), 2, "两张表");
        EQI(ui.sheetIndex(), 0, "初始在第 1 张");
        EQS(ui.sheetName(), "一月", "表名正确");

        std::string msg;
        OK(ui.runCommand("sheet 2", msg), ":sheet 2 切换: " + msg);
        EQI(ui.sheetIndex(), 1, "切到第 2 张");
        EQS(ui.sheetName(), "二月", "第 2 张表名");
        EQS(ui.currentSheet()->display(0, 0), "B表", "第 2 张的内容");

        // 按名字切
        OK(ui.runCommand("sheet 一月", msg), ":sheet 一月: " + msg);
        EQI(ui.sheetIndex(), 0, "按名字切回第 1 张");
        OK(!ui.runCommand("sheet 99", msg), "超出范围被拒绝");
        OK(!ui.runCommand("sheet 不存在", msg), "不存在的表名被拒绝");

        // 新建
        OK(ui.runCommand("newsheet 三月", msg), ":newsheet: " + msg);
        EQI(ui.sheetCount(), 3, "变成三张表");
        EQI(ui.sheetIndex(), 2, "新建后停在新表");
        OK(!ui.runCommand("newsheet 三月", msg), "同名表被拒绝: " + msg);

        // Ctrl+PageUp / PageDown 切表
        ui.handleKey(keyOf("\x1b[6;5~"));
        EQI(ui.sheetIndex(), 2, "已在末表，再往后不动");
        ui.handleKey(keyOf("\x1b[5;5~"));
        EQI(ui.sheetIndex(), 1, "Ctrl+PageUp 往前切");
        ui.handleKey(keyOf("\x1b[5;5~"));
        EQI(ui.sheetIndex(), 0, "再往前到第 1 张");
        ui.handleKey(keyOf("\x1b[5;5~"));
        EQI(ui.sheetIndex(), 0, "已在首表，不再往前");

        // 表签渲染
        Screen sc(100, 20);
        ui.render(sc);
        std::string txt = sc.toPlainText();
        OK(txt.find("一月") != std::string::npos, "表签显示 一月");
        OK(txt.find("三月") != std::string::npos, "表签显示 三月");
    }

    // ------------------------------------------------------------------
    std::cout << "== 选区 ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setValue(0, 0, Value::num(1));
        sh.setValue(1, 0, Value::num(2));
        sh.setValue(0, 1, Value::num(3));
        sh.setValue(1, 1, Value::num(4));
        GridUI ui; ui.attach(&wb);
        ui.resize(100, 20);

        EQI(ui.selection().count(), 1, "初始是单格");
        OK(ui.selection().isSingle(), "初始选区 single");

        // Shift+右 扩到 B1
        ui.handleKey(keyOf("\x1b[1;2C"));
        EQI(ui.selection().count(), 2, "选区扩到 2 格");
        EQI(ui.selection().cols(), 2, "两列");
        EQI(ui.selection().rows(), 1, "一行");
        // Shift+下 扩到 B2
        ui.handleKey(keyOf("\x1b[1;2B"));
        EQI(ui.selection().count(), 4, "选区扩到 4 格");
        EQI(ui.selection().cols(), 2, "两列");
        EQI(ui.selection().rows(), 2, "两行");
        EQS(ui.selection().addr(), "A1:B2", "选区地址 A1:B2");

        // 不带 shift 的移动会折叠选区
        ui.handleKey(keyOf("\x1b[C"));
        EQI(ui.selection().count(), 1, "普通移动折叠选区");

        // 手动设置
        ui.setSelection(2, 2, 4, 5);
        EQI(ui.selection().count(), 12, "3 列 x 4 行 = 12 格");
        EQS(ui.selection().addr(), "C3:E6", "选区地址");
        EQI(ui.selection().cols(), 3, "3 列");
        EQI(ui.selection().rows(), 4, "4 行");
        OK(ui.selection().contains(3, 4), "contains 命中");
        OK(!ui.selection().contains(9, 9), "contains 未命中");
        // 反向设置也要归一
        ui.setSelection(4, 5, 2, 2);
        EQS(ui.selection().addr(), "C3:E6", "反向设置被归一化");

        // 清空选区
        ui.setSelection(0, 0, 1, 1);
        ui.handleKey(keyOf("\x1b[3~"));   // Delete
        EQI(sh.cellCount(), 0, "选区 4 格全部清空");

        // 撤销恢复
        std::string msg;
        OK(ui.undo(msg), "撤销: " + msg);
        EQI(sh.cellCount(), 4, "撤销后 4 格恢复");
        EQS(sh.display(1, 1), "4", "B2 恢复为 4");
    }

    // ------------------------------------------------------------------
    std::cout << "== 撤销与重做 ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        GridUI ui; ui.attach(&wb);
        ui.resize(100, 20);
        std::string msg;

        EQI((int)ui.undoDepth(), 0, "初始无撤销记录");
        OK(!ui.undo(msg), "空栈撤销返回 false");
        OK(!ui.redo(msg), "空栈重做返回 false");

        // 编辑 A1
        OK(ui.editCell(0, 0, "10", msg), "写入 A1=10");
        EQI((int)ui.undoDepth(), 1, "一次编辑 -> 一条撤销记录");
        EQS(sh.display(0, 0), "10", "A1 = 10");

        // 再编辑 B1
        ui.editCell(1, 0, "20", msg);
        ui.editCell(2, 0, "=A1+B1", msg);
        EQS(sh.display(2, 0), "30", "C1 = A1+B1 = 30");
        EQI((int)ui.undoDepth(), 3, "三次编辑 -> 三条记录");

        // 撤销回退
        OK(ui.undo(msg), "撤销");
        EQS(sh.display(2, 0), "", "C1 被撤销");
        EQI((int)ui.redoDepth(), 1, "重做栈有 1 条");
        OK(ui.redo(msg), "重做");
        EQS(sh.display(2, 0), "30", "C1 重做后恢复");

        OK(ui.undo(msg), "再撤销");
        OK(ui.undo(msg), "再撤销");
        EQS(sh.display(1, 0), "", "B1 被撤销");
        EQS(sh.display(0, 0), "10", "A1 还在");
        OK(ui.undo(msg), "撤销最后一次");
        EQS(sh.display(0, 0), "", "A1 被撤销");
        EQI(sh.cellCount(), 0, "全部撤销后无单元格");
        OK(!ui.undo(msg), "撤销栈已空");

        // 一直重做回最终状态
        while (ui.redo(msg)) {}
        EQS(sh.display(2, 0), "30", "全部重做后 C1 = 30");

        // 新操作会作废重做栈
        ui.undo(msg);
        EQI((int)ui.redoDepth(), 1, "撤销后有 1 条可重做");
        ui.editCell(5, 5, "99", msg);
        EQI((int)ui.redoDepth(), 0, "新编辑清空重做栈");

        // 公式写错不应进入撤销栈
        size_t before = ui.undoDepth();
        ui.editCell(8, 8, "=SUM(", msg);
        EQI((int)ui.undoDepth(), (int)before, "错误公式不产生撤销记录");
        OK(msg.find("公式错误") != std::string::npos, "错误有提示: " + msg);

        // Ctrl+Z / Ctrl+Y
        size_t d = ui.undoDepth();
        ui.handleKey(keyOf("\x1a"));      // Ctrl+Z
        EQI((int)ui.undoDepth(), (int)d - 1, "Ctrl+Z 撤销一条");
        ui.handleKey(keyOf("\x19"));      // Ctrl+Y
        EQI((int)ui.undoDepth(), (int)d, "Ctrl+Y 重做一条");
    }

    // ------------------------------------------------------------------
    std::cout << "== 复制与粘贴 ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setName("数据");
        sh.setValue(0, 0, Value::str("甲"));
        sh.setValue(1, 0, Value::num(100));
        sh.setValue(0, 1, Value::str("乙"));
        sh.setValue(1, 1, Value::num(200));
        GridUI ui; ui.attach(&wb);
        ui.resize(100, 20);
        std::string msg;

        // 全选 A1:B2 并复制
        ui.setSelection(0, 0, 1, 1);
        ui.copySelection();
        OK(!ui.clipboardEmpty(), "剪贴板非空");

        // 光标移到 D5 粘贴
        ui.setSelection(3, 4, 3, 4);
        OK(ui.runCommand("paste", msg), ":paste: " + msg);
        EQS(sh.display(3, 4), "甲", "D5 = 甲");
        EQS(sh.display(4, 4), "100", "E5 = 100");
        EQS(sh.display(3, 5), "乙", "D6 = 乙");
        EQS(sh.display(4, 5), "200", "E6 = 200");

        // 撤销粘贴
        ui.undo(msg);
        EQS(sh.display(3, 4), "", "撤销后 D5 清空");
        EQS(sh.display(0, 0), "甲", "原位置不受影响");

        // 空剪贴板粘贴被拒
        Workbook wb2; GridUI u2; u2.attach(&wb2); u2.resize(100, 20);
        OK(!u2.runCommand("paste", msg), "空剪贴板粘贴被拒");
    }


    // ------------------------------------------------------------------
    std::cout << "== 填充（Ctrl+D / Ctrl+R） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setFormula(0, 0, "B1*2");        // A1 = B1*2
        sh.setValue(1, 0, Value::num(10));  // B1
        sh.setValue(1, 1, Value::num(20));  // B2
        sh.setValue(1, 2, Value::num(30));  // B3
        sh.recalc();
        EQS(sh.display(0, 0), "20", "A1 = 20");

        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        std::string msg;

        // 选 A1:A3 后 Ctrl+D
        ui.setSelection(0, 0, 0, 2);
        ui.handleKey(keyOf("\x04"));       // Ctrl+D
        EQS(sh.display(0, 1), "40", "Ctrl+D 后 A2 = B2*2 = 40");
        EQS(sh.display(0, 2), "60", "Ctrl+D 后 A3 = B3*2 = 60");
        std::string f;
        sh.cellFormula(0, 2, f);
        EQS(f, "B3*2", "A3 的公式引用已下移");
        OK(msg.find("向下填充") != std::string::npos || ui.status().find("向下填充") != std::string::npos,
           "状态栏提示填充: " + ui.status());

        // 撤销应整块还原
        OK(ui.undo(msg), "撤销填充: " + msg);
        EQS(sh.display(0, 1), "", "撤销后 A2 清空");
        EQS(sh.display(0, 2), "", "撤销后 A3 清空");

        // Ctrl+R
        ui.setSelection(0, 0, 2, 0);
        sh.setFormula(0, 0, "A2*3");
        sh.setValue(0, 1, Value::num(5));
        sh.setValue(1, 1, Value::num(6));
        sh.setValue(2, 1, Value::num(7));
        sh.recalc();
        ui.handleKey(keyOf("\x12"));       // Ctrl+R
        EQS(sh.display(1, 0), "18", "Ctrl+R 后 B1 = B2*3 = 18");
        EQS(sh.display(2, 0), "21", "Ctrl+R 后 C1 = C2*3 = 21");

        // 单行 / 单列不能填充
        ui.setSelection(0, 0, 0, 0);
        OK(!ui.runCommand("fill", msg), "单行不能向下填充: " + msg);
        OK(!ui.runCommand("fillright", msg), "单列不能向右填充: " + msg);

        // 绝对引用在填充后保持不动
        Workbook wb2; Sheet& sh2 = wb2.sheet(0);
        sh2.setValue(5, 0, Value::num(100));
        sh2.setFormula(0, 0, "$F$1*2");
        sh2.recalc();
        GridUI u2; u2.attach(&wb2); u2.resize(100, 20);
        u2.setSelection(0, 0, 0, 2);
        OK(u2.runCommand("fill", msg), ":fill: " + msg);
        std::string g;
        sh2.cellFormula(0, 2, g);
        EQS(g, "$F$1*2", "填充后绝对引用仍指向 $F$1");
        EQS(sh2.display(0, 2), "200", "A3 = 200");
    }


    // ------------------------------------------------------------------
    std::cout << "== 数字格式（:fmt） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setValue(0, 0, Value::num(1234.5678));
        sh.setValue(1, 0, Value::num(0.1234));
        sh.recalc();
        EQS(sh.display(0, 0), "1234.5678", "默认显示是原值");

        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        std::string msg;

        // 查询当前格式
        OK(ui.runCommand("fmt", msg), ":fmt 查询: " + msg);
        OK(msg.find("General") != std::string::npos, "未设格式时显示 General: " + msg);

        // 设置
        ui.setSelection(0, 0, 0, 0);
        OK(ui.runCommand("fmt #,##0.00", msg), ":fmt 设置: " + msg);
        EQS(sh.numFmtAt(0, 0), "#,##0.00", "格式码已写入");
        EQS(sh.display(0, 0), "1,234.57", "显示套用了格式");
        OK(msg.find("1,234.57") != std::string::npos, "提示里带显示效果: " + msg);

        // 百分比
        ui.setSelection(1, 0, 1, 0);
        OK(ui.runCommand("fmt 0.00%", msg), ":fmt 百分比: " + msg);
        EQS(sh.display(1, 0), "12.34%", "百分比显示");

        // 非法格式码被拒绝，且不污染已有格式
        OK(!ui.runCommand("fmt [abc]0", msg), "非法格式码被拒绝: " + msg);
        EQS(sh.numFmtAt(1, 0), "0.00%", "非法码不影响已有格式");

        // 选区批量设置
        ui.setSelection(0, 0, 1, 0);
        OK(ui.runCommand("fmt 0.0", msg), ":fmt 批量: " + msg);
        EQS(sh.numFmtAt(0, 0), "0.0", "A1 被设置");
        EQS(sh.numFmtAt(1, 0), "0.0", "B1 被设置");

        // 撤销应还原两套旧格式（不是统一还原成某一个）
        OK(ui.undo(msg), "撤销格式: " + msg);
        EQS(sh.numFmtAt(0, 0), "#,##0.00", "撤销后 A1 回到 #,##0.00");
        EQS(sh.numFmtAt(1, 0), "0.00%", "撤销后 B1 回到 0.00%");
        EQS(sh.display(0, 0), "1,234.57", "撤销后显示正确");
        // 重做
        OK(ui.redo(msg), "重做格式: " + msg);
        EQS(sh.numFmtAt(0, 0), "0.0", "重做后 A1 = 0.0");
        EQS(sh.numFmtAt(1, 0), "0.0", "重做后 B1 = 0.0");

        // 清除
        OK(ui.runCommand("fmt clear", msg), ":fmt clear: " + msg);
        EQS(sh.numFmtAt(0, 0), "", "清除后格式码为空");
        EQS(sh.display(0, 0), "1234.5678", "清除后回到通用显示");
    }


    // ------------------------------------------------------------------
    std::cout << "== 样式与合并（:style / :merge） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setValue(0, 0, Value::str("标题"));
        sh.setValue(1, 0, Value::num(42));
        sh.recalc();

        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        std::string msg;

        // 单次设置多个属性
        ui.setSelection(0, 0, 0, 0);
        OK(ui.runCommand("style bold center", msg), ":style 多属性: " + msg);
        OK(sh.styleAt(0, 0).bold, "粗体生效");
        OK(sh.styleAt(0, 0).hAlign == HAlign::Center, "居中生效");

        // 叠加：第二次设置不应清掉第一次的
        OK(ui.runCommand("style fill=FFFF00", msg), ":style 叠加: " + msg);
        OK(sh.styleAt(0, 0).bold, "叠加后粗体仍在");
        EQS(sh.styleAt(0, 0).fillColor, "FFFF00", "叠加后填充色生效");

        // 颜色名可用
        OK(ui.runCommand("style color=red", msg), ":style 颜色名: " + msg);
        EQS(sh.styleAt(0, 0).fontColor, "FF0000", "颜色名解析为 FF0000");

        // 非法颜色被拒绝
        OK(!ui.runCommand("style fill=不是颜色", msg), "非法颜色被拒绝: " + msg);
        EQS(sh.styleAt(0, 0).fillColor, "FFFF00", "非法颜色不影响已有样式");

        // 未知样式项被拒绝
        OK(!ui.runCommand("style 瞎写", msg), "未知样式被拒绝: " + msg);

        // clear 清空
        OK(ui.runCommand("style clear", msg), ":style clear: " + msg);
        OK(sh.styleAt(0, 0).empty(), "clear 后样式为空");

        // 撤销能还原叠加前的完整样式
        OK(ui.undo(msg), "撤销样式: " + msg);
        OK(sh.styleAt(0, 0).bold, "撤销后回到叠加态（粗体）");
        EQS(sh.styleAt(0, 0).fillColor, "FFFF00", "撤销后回到叠加态（填充）");
        OK(ui.redo(msg), "重做样式: " + msg);
        OK(sh.styleAt(0, 0).empty(), "重做后回到 clear 态");

        // 合并
        ui.setSelection(0, 0, 3, 0);
        OK(ui.runCommand("merge", msg), ":merge: " + msg);
        OK(wb.isMergedAway(0, 1, 0), "B1 被合并吞掉");
        auto sp = wb.mergeSpan(0, 0, 0);
        EQI(sp.first, 4, "合并宽度 4");

        // 取消合并：只需选区内任意一格
        ui.setSelection(2, 0, 2, 0);
        OK(ui.runCommand("unmerge", msg), ":unmerge: " + msg);
        OK(!wb.isMergedAway(0, 1, 0), "取消后 B1 不再被吞");
        EQI(wb.allMerges()[0].size(), 0, "合并列表已空");

        // 单行单列不能合并
        ui.setSelection(0, 0, 0, 0);
        OK(!ui.runCommand("merge", msg), "1x1 不能合并: " + msg);

        // 没有合并时取消应提示而非崩溃
        OK(!ui.runCommand("unmerge", msg), "无合并时取消被拒: " + msg);
    }


    // ------------------------------------------------------------------
    std::cout << "== 条件格式（:cf） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        for (int i = 0; i < 5; i++) sh.setValue(0, i, Value::num(i + 1));   // 1..5
        sh.recalc();

        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        std::string msg;

        // 大于 3
        ui.setSelection(0, 0, 0, 4);
        OK(ui.runCommand("cf >3 fill=FF0000", msg), ":cf >3: " + msg);
        EQI(wb.allCfs()[0].size(), 1, "登记了 1 条");
        OK(wb.allCfs()[0][0].rects.size() == 1, "区域已记录");
        EQI(wb.allCfs()[0][0].rects[0][3], 4, "区域到第 5 行");

        // 判定结果正确
        std::map<std::pair<int,int>, CfStyle> res;
        resolveCfStyles(sh, wb.allCfs()[0], res);
        EQI(res.size(), 2, "4 和 5 命中");
        EQS(res[{0,4}].fillColor, "FF0000", "填充色正确");

        // 默认样式：不给样式时也要能看出来（红色填充）
        ui.setSelection(0, 0, 0, 4);
        OK(ui.runCommand("cf <2", msg), ":cf 无样式参数: " + msg);
        EQI(wb.allCfs()[0].size(), 2, "第二条已登记");
        EQS(wb.allCfs()[0][1].rules[0].style.fillColor, "FF0000", "默认给了红色填充");

        // top / bottom
        ui.setSelection(0, 0, 0, 4);
        OK(ui.runCommand("cf top 2 bold", msg), ":cf top: " + msg);
        OK(wb.allCfs()[0][2].rules[0].type == CfType::Top10, "类型为 Top10");
        EQI(wb.allCfs()[0][2].rules[0].rank, 2, "rank=2");
        OK(wb.allCfs()[0][2].rules[0].style.bold, "粗体已设");

        // between
        ui.setSelection(0, 0, 0, 4);
        OK(ui.runCommand("cf between 2 4 fill=FFFF00", msg), ":cf between: " + msg);
        EQI(wb.allCfs()[0][3].rules[0].formulas.size(), 2, "between 有两条公式");

        // 表达式
        ui.setSelection(0, 0, 0, 4);
        OK(ui.runCommand("cf expr A1>2", msg), ":cf expr: " + msg);
        OK(wb.allCfs()[0][4].rules[0].type == CfType::Expression, "类型为 Expression");

        // 非法判定被拒
        OK(!ui.runCommand("cf 瞎写 5", msg), "未知判定被拒: " + msg);
        // 数值缺失被拒
        OK(!ui.runCommand("cf >", msg), "缺数值被拒: " + msg);
        // 非法颜色被拒
        OK(!ui.runCommand("cf >1 fill=不是颜色", msg), "非法颜色被拒: " + msg);

        // list
        OK(ui.runCommand("cf", msg), ":cf 无参列出: " + msg);
        OK(msg.find("条件格式") != std::string::npos, "列出内容含标题");

        // 渲染：条件格式参与绘制，这里验证不崩且画面非空
        Screen scr(80, 20);
        ui.render(scr);
        std::string dump = scr.toPlainText();
        OK(!dump.empty(), "渲染非空");

        // clear
        OK(ui.runCommand("cf clear", msg), ":cf clear: " + msg);
        EQI(wb.allCfs()[0].size(), 0, "已清空");
        OK(!ui.runCommand("cf clear", msg), "无内容时清除被拒: " + msg);
    }


    // ------------------------------------------------------------------
    std::cout << "== 数据验证（:dv） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.recalc();
        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        std::string msg;

        // 光标在 A1，先移到目标格
        auto goTo = [&](int c, int r) {
            ui.setCursor(c, r);
        };
        // 键入一段文本并提交：走真实的按键路径，不走内部接口。
        //
        // handleKey 的返回值表示"是否继续运行"，不是"输入是否被接受"，
        // 所以判定只能看状态栏 —— 先把状态清空，再看是否出现"拒绝"。
        auto type = [&](const std::string& text) -> bool {
            ui.setMessage("");
            ui.handleKey(keyOf("\r"));            // Enter 进入编辑
            // 必须按完整的 UTF-8 序列逐字送入，不能逐字节 ——
            // 逐字节会让 parseKeys 把每个字节当成一个码点，
            // 中文进去就变成乱码，随后被 list 规则判为"不在候选项内"。
            for (size_t k = 0; k < text.size();) {
                unsigned char c = (unsigned char)text[k];
                size_t adv = 1;
                if (c >= 0xF0) adv = 4; else if (c >= 0xE0) adv = 3;
                else if (c >= 0xC0) adv = 2;
                if (k + adv > text.size()) adv = 1;
                ui.handleKey(keyOf(text.substr(k, adv)));
                k += adv;
            }
            ui.handleKey(keyOf("\r"));            // Enter 提交
            return ui.status().find("拒绝") == std::string::npos;   // true = 被接受
        };

        // 整数区间 1..100
        ui.setSelection(0, 0, 0, 4);
        OK(ui.runCommand("dv whole 1 100 err=\"请输 1 到 100\"", msg), ":dv whole: " + msg);
        EQI(wb.allDvs()[0].size(), 1, "登记 1 条");
        OK(wb.allDvs()[0][0].type == DvType::Whole, "类型 whole");
        EQS(wb.allDvs()[0][0].formula1, "1", "下界");
        EQS(wb.allDvs()[0][0].formula2, "100", "上界");

        // 关键：非法输入必须被拦住，且单元格保持原样
        goTo(0, 0);
        OK(type("50"), "50 应被接受");
        OK(sh.valueAt(0, 0).isNum() && sh.valueAt(0, 0).n == 50, "50 已写入");

        goTo(0, 1);
        OK(!type("999"), "999 应被拒绝");
        OK(!sh.hasCell(0, 1) || sh.valueAt(0, 1).isEmpty(), "被拒后未写入");
        OK(ui.status().find("拒绝") != std::string::npos, "状态含拒绝提示: " + ui.status());

        // 非整数被拒
        goTo(0, 2);
        OK(!type("3.5"), "3.5 不是整数，应被拒");

        // 合法边界
        goTo(0, 3);
        OK(type("1"), "下界 1 应被接受");

        // warning 强度：放行但提示
        ui.setSelection(1, 0, 1, 2);
        OK(ui.runCommand("dv decimal >=0 warn", msg), ":dv warn: " + msg);
        OK(wb.allDvs()[0][1].errorStyle == DvErrorStyle::Warning, "强度为 Warning");
        goTo(1, 0);
        OK(type("-5"), "警告级别应放行");
        OK(sh.valueAt(1, 0).isNum() && sh.valueAt(1, 0).n == -5, "警告级别确实写入了");

        // 序列
        ui.setSelection(2, 0, 2, 2);
        OK(ui.runCommand("dv list 是,否", msg), ":dv list: " + msg);
        EQI(wb.allDvs()[0][2].listItems.size(), 2, "2 个候选项");
        goTo(2, 0);
        OK(type("是"), "候选项内应被接受");
        goTo(2, 1);
        OK(!type("也许"), "非候选项应被拒");

        // 文本长度
        ui.setSelection(3, 0, 3, 2);
        OK(ui.runCommand("dv len <=5", msg), ":dv len: " + msg);
        OK(wb.allDvs()[0][3].type == DvType::TextLength, "类型 textLength");
        goTo(3, 0);
        OK(type("abc"), "3 字符应被接受");
        goTo(3, 1);
        OK(!type("abcdefgh"), "8 字符应被拒");

        // 非法类型被拒
        OK(!ui.runCommand("dv 瞎写 1", msg), "未知类型被拒: " + msg);
        OK(!ui.runCommand("dv whole", msg), "缺参数被拒: " + msg);
        OK(!ui.runCommand("dv list", msg), "list 缺候选项被拒: " + msg);

        // 列出与清空
        OK(ui.runCommand("dv show", msg), ":dv show 列出: " + msg);
        OK(msg.find("数据验证") != std::string::npos, "列出含标题");
        OK(ui.runCommand("dv clear", msg), ":dv clear: " + msg);
        EQI(wb.allDvs()[0].size(), 0, "已清空");
    }


    // ------------------------------------------------------------------
    std::cout << "== 批注（:note） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setValue(0, 0, Value::num(42));
        sh.recalc();
        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        ui.setCursor(0, 0);
        std::string msg;

        // 无批注时查询
        OK(ui.runCommand("note", msg), "查询无批注不报错: " + msg);
        OK(msg.find("没有批注") != std::string::npos, "提示没有批注: " + msg);

        // 添加
        OK(ui.runCommand("note 这个数字要复核", msg), ":note 添加: " + msg);
        EQI(wb.allNotes()[0].size(), 1, "登记 1 条");
        const CellNote* n = wb.noteAt(0, 0, 0);
        OK(n != nullptr, "A1 有批注");
        if (n) EQS(n->text, "这个数字要复核", "文本正确");

        // 查询
        OK(ui.runCommand("note", msg), ":note 查询: " + msg);
        OK(msg.find("这个数字要复核") != std::string::npos, "查询显示内容: " + msg);

        // 同格再次添加 = 修改
        OK(ui.runCommand("note 已复核", msg), ":note 再次添加: " + msg);
        EQI(wb.allNotes()[0].size(), 1, "仍为 1 条（修改而非追加）");
        EQS(wb.noteAt(0, 0, 0)->text, "已复核", "文本已更新");

        // 换一格添加
        ui.setCursor(1, 1);
        OK(ui.runCommand("note 另一处", msg), ":note 第二格: " + msg);
        EQI(wb.allNotes()[0].size(), 2, "共 2 条");

        // 渲染含批注标记
        Screen scr(80, 20);
        ui.render(scr);
        std::string dump = scr.toPlainText();
        // ◆（U+25C2）作为批注角标应出现在画面里
        OK(dump.find("\xe2\x97\x82") != std::string::npos, "画面含批注角标");

        // 删除
        ui.setCursor(0, 0);
        OK(ui.runCommand("note clear", msg), ":note clear: " + msg);
        EQI(wb.allNotes()[0].size(), 1, "删掉 1 条，剩 1 条");
        OK(wb.noteAt(0, 0, 0) == nullptr, "A1 已无批注");
        OK(!ui.runCommand("note clear", msg), "重复删除被拒: " + msg);

        // ":note" 后面只有空格时，body 被 trim 空，走的是"显示"分支而非新增。
        // 这不是 bug：单写 :note 本来就是查看命令，不该报错。
        OK(ui.runCommand("note   ", msg), ":note 后接空格视为查看: " + msg);
        OK(wb.allNotes()[0].size() == 1, "未因此新增空批注");

        // 没有工作簿时不崩
        GridUI ui2; ui2.resize(80, 20);
        OK(!ui2.runCommand("note x", msg), "无工作簿时被拒: " + msg);
    }


    // ------------------------------------------------------------------
    std::cout << "== 视图属性（列宽/行高/冻结/筛选） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        for (int i = 0; i < 3; i++) sh.setValue(i, 0, Value::str("表头"));
        sh.recalc();
        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        std::string msg;

        // 列宽
        ui.setCursor(0, 0);
        OK(ui.runCommand("colw 18.5", msg), ":colw: " + msg);
        EQD(wb.layout(0).colWidths.at(0), 18.5, "列宽已设");

        // 非法宽度被拒
        OK(!ui.runCommand("colw abc", msg), "非数字被拒: " + msg);
        OK(!ui.runCommand("colw 0", msg), "零宽被拒: " + msg);
        OK(!ui.runCommand("colw -3", msg), "负宽被拒: " + msg);

        // 行高
        OK(ui.runCommand("rowh 28", msg), ":rowh: " + msg);
        EQD(wb.layout(0).rowHeights.at(0), 28, "行高已设");
        OK(!ui.runCommand("rowh 0", msg), "零行高被拒: " + msg);

        // 冻结：光标在 A1 时应拒绝（没什么可冻的）
        ui.setCursor(0, 0);
        OK(!ui.runCommand("freeze", msg), "A1 冻结被拒: " + msg);
        // 移到 C3 -> 冻结 2 行 2 列
        ui.setCursor(2, 2);
        OK(ui.runCommand("freeze", msg), ":freeze: " + msg);
        EQI(wb.layout(0).freeze.frozenRows, 2, "冻结 2 行");
        EQI(wb.layout(0).freeze.frozenCols, 2, "冻结 2 列");
        // 取消
        OK(ui.runCommand("freeze clear", msg), ":freeze clear: " + msg);
        OK(!wb.layout(0).freeze.enabled, "冻结已取消");

        // 筛选
        ui.setSelection(0, 0, 2, 4);
        OK(ui.runCommand("filter", msg), ":filter: " + msg);
        OK(wb.layout(0).filter.enabled, "筛选已启用");
        EQI(wb.layout(0).filter.c1, 2, "筛选到 C 列");
        EQI(wb.layout(0).filter.r1, 4, "筛选到第 5 行");
        OK(ui.runCommand("filter clear", msg), ":filter clear: " + msg);
        OK(!wb.layout(0).filter.enabled, "筛选已取消");

        // 未知命令被拒
        OK(!ui.runCommand("colw", msg), "colw 缺参数被拒: " + msg);
    }


    // ------------------------------------------------------------------
    std::cout << "== 嵌入图片（:img） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        for (int i = 0; i < 4; i++) sh.setValue(0, i, Value::num(i + 1));
        sh.recalc();
        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        std::string msg;

        // 无图表时应被拒
        OK(!ui.runCommand("img", msg), "无图表被拒: " + msg);
        OK(ui.runCommand("img list", msg), "空列表查询不报错: " + msg);
        OK(msg.find("没有嵌入图片") != std::string::npos, "提示没有图片: " + msg);

        // 加一个图表
        {
            Chart c;
            c.type = ChartType::Column;
            c.title = "季度";
            c.categories = {"Q1", "Q2", "Q3", "Q4"};
            DataSeries ds; ds.name = "值";
            for (int i = 0; i < 4; i++) ds.values.push_back((i + 1) * 10.0);
            c.series.push_back(ds);
            wb.addChart(0, c, "季度");
        }
        EQI(wb.chartCount(0), 1, "已有 1 个图表");

        // 转成图片嵌入
        ui.setSelection(8, 2, 12, 10);
        OK(ui.runCommand("img", msg), ":img 嵌入: " + msg);
        EQI((int)wb.allImages()[0].size(), 1, "嵌入 1 张");
        const ImagePart& im = wb.allImages()[0][0];
        OK(im.data.size() > 100, "PNG 字节非空");
        OK(im.data.size() > 3 && im.data[0] == 0x89 && im.data[1] == 'P', "PNG 魔数正确");
        EQS(im.ext, "png", "扩展名 png");
        EQI(im.fromCol, 8, "锚点起始列用选区");
        EQI(im.toRow, 10, "锚点结束行用选区");

        // 列出
        OK(ui.runCommand("img list", msg), ":img list: " + msg);
        OK(msg.find("嵌入图片 1 张") != std::string::npos, "列出含数量: " + msg);

        // 序号越界被拒
        OK(!ui.runCommand("img 5", msg), "越界序号被拒: " + msg);
        // 非数字被拒
        OK(!ui.runCommand("img abc", msg), "非数字被拒: " + msg);
        // 合法序号
        OK(ui.runCommand("img 0", msg), ":img 0 有效: " + msg);
        EQI((int)wb.allImages()[0].size(), 2, "再嵌一张，共 2 张");

        // 清除
        OK(ui.runCommand("img clear", msg), ":img clear: " + msg);
        EQI((int)wb.allImages()[0].size(), 0, "已清空");
    }


    // ------------------------------------------------------------------
    std::cout << "== 定义名称（:name） ==\n";
    {
        Workbook wb; Sheet& sh = wb.sheet(0);
        sh.setName("数据");
        for (int i = 0; i < 4; i++) sh.setValue(1, i + 1, Value::num((i + 1) * 10));
        sh.recalc();
        GridUI ui; ui.attach(&wb); ui.resize(100, 20);
        std::string msg;

        // 空列表
        OK(ui.runCommand("name", msg), "空列表查询: " + msg);
        OK(msg.find("没有定义名称") != std::string::npos, "提示为空: " + msg);

        // 用选区定义
        ui.setSelection(1, 1, 1, 4);            // B2:B5
        OK(ui.runCommand("name 销售额", msg), ":name 用选区: " + msg);
        EQI((int)wb.definedNames().size(), 1, "登记 1 条");
        const std::string* d = wb.findDefinedName("销售额");
        OK(d != nullptr, "能查到");
        // 必须带表名与绝对引用
        if (d) EQS(*d, "数据!$B$2:$B$5", "定义文本含表名与绝对引用");

        // 显式区域
        OK(ui.runCommand("name 单价 B2:B3", msg), ":name 显式区域: " + msg);
        const std::string* d2 = wb.findDefinedName("单价");
        OK(d2 && *d2 == "数据!$B$2:$B$3", "显式区域正确");

        // 单格：不应生成冒号段
        OK(ui.runCommand("name 单格 C1", msg), ":name 单格: " + msg);
        const std::string* d3 = wb.findDefinedName("单格");
        OK(d3 && *d3 == "数据!$C$1", "单格不带冒号");

        // 非法名被拒
        OK(!ui.runCommand("name A1", msg), "像地址的名字被拒: " + msg);
        OK(!ui.runCommand("name 1x B2", msg), "数字开头被拒: " + msg);
        // 区域格式错被拒
        OK(!ui.runCommand("name 坏 B2-", msg), "区域格式错被拒: " + msg);

        // 列出
        OK(ui.runCommand("name", msg), ":name 列出: " + msg);
        OK(msg.find("定义名称 3 个") != std::string::npos, "列出含数量: " + msg);

        // 公式里能用
        std::string e = sh.setFormula(3, 0, "SUM(销售额)");
        OK(e.empty(), "公式解析: " + e);
        sh.recalc();
        EQS(sh.display(3, 0), "100", "SUM(销售额)=100");

        // 删除
        OK(ui.runCommand("name del 单价", msg), ":name del: " + msg);
        EQI((int)wb.definedNames().size(), 2, "删后剩 2 条");
        OK(!ui.runCommand("name del 不存在", msg), "删除不存在的被拒: " + msg);
        OK(!ui.runCommand("name del", msg), "del 缺参数被拒: " + msg);
    }

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
