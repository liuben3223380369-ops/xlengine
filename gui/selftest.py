"""
图形界面桥接层的自检。

分两层：
  1. C 桥接层（ctypes）—— 不需要 Qt，验证引擎接口本身正确
  2. Qt 控件 —— 用 offscreen 平台，不需要显示器

用法：
    make lib
    python3 -m gui.selftest
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from gui import engine  # noqa: E402

PASS = 0
FAIL = 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  [OK]   %s" % name)
    else:
        FAIL += 1
        print("  [FAIL] %s   %s" % (name, detail))


def test_bridge():
    print("== C 桥接层 ==")
    check("函数总数 495", engine.func_count() == 495, "实际 %d" % engine.func_count())
    check("函数列表非空", len(engine.func_names()) > 0)

    wb = engine.Workbook()
    sh = wb.sheet(0)

    # 写入与求值
    sh.set_num(0, 0, 10)
    sh.set_num(0, 1, 20)
    ok = sh.set_formula(1, 0, "=A1+A2")
    check("公式接受前导 =", ok, wb.last_error)
    wb.recalc()
    check("A1+A2 = 30", sh.display(1, 0) == "30", "得到 %r" % sh.display(1, 0))

    # 关键：写出的公式不能带双等号
    check("存的是无等号的公式", sh.formula(1, 0) == "A1+A2",
          "得到 %r" % sh.formula(1, 0))
    check("raw() 只加一个 =", sh.raw(1, 0) == "=A1+A2", "得到 %r" % sh.raw(1, 0))

    # 智能写入的类型判定
    sh.set(2, 0, "123")
    check("'123' 判为数字", sh.value_type(2, 0) == 1)
    sh.set(2, 1, "abc")
    check("'abc' 判为文本", sh.value_type(2, 1) == 2)
    sh.set(2, 2, "TRUE")
    check("'TRUE' 判为布尔", sh.value_type(2, 2) == 3)
    sh.set(2, 3, "=1/0")
    check("'1/0' 得到错误值", sh.value_type(2, 3) == 4)
    # float() 会接受的怪东西，不该被当成数字
    sh.set(2, 4, "nan")
    check("'nan' 不当数字", sh.value_type(2, 4) == 2)
    sh.set(2, 5, "1_000")
    check("'1_000' 不当数字", sh.value_type(2, 5) == 2)

    # 格式
    sh.set_num(3, 0, 1234.5678)
    sh.set_numfmt(3, 0, "#,##0.00")
    check("千分位格式", sh.display(3, 0) == "1,234.57", "得到 %r" % sh.display(3, 0))

    # 样式
    sh.style(4, 0, bold=True, fill="FF0000", halign=2)
    st = sh.get_style(4, 0)
    check("粗体", st.get("bold") is True)
    check("填充色", st.get("fill") == "FF0000", "得到 %r" % st.get("fill"))
    check("居中", st.get("halign") == 2)

    # 列宽行高
    sh.set_col_width(0, 20)
    check("列宽读回", abs(sh.col_width(0) - 20) < 2, "得到 %r" % sh.col_width(0))
    sh.set_row_height(0, 40)
    check("行高读回", abs(sh.row_height(0) - 40) < 1, "得到 %r" % sh.row_height(0))

    # 合并
    sh.merge(5, 0, 6, 1)
    check("左上角是合并锚点", sh.merge_info(5, 0)[0] == 2)
    check("跨度为 2x2", sh.merge_info(5, 0)[1:] == (2, 2),
          "得到 %r" % (sh.merge_info(5, 0)[1:],))
    check("被吞掉的格子", sh.merge_info(6, 1)[0] == 1)

    # 已用区域
    # 注意：合并区域本身不产生单元格，所以已用区域只到真正写过内容的格子。
    # 上面最后写的是 (4,0) 样式 / (2,5) 值 / (3,0) 数值，故 c1 应 >= 4。
    ur = sh.used_range()
    check("已用区域覆盖写入范围", ur[2] >= 4 and ur[3] >= 5, "得到 %r" % (ur,))

    # ---- 结构性编辑 ----
    wb3 = engine.Workbook()
    s3 = wb3.sheet(0)
    s3.set_num(0, 0, 1)
    s3.set_num(0, 1, 2)
    s3.set_num(0, 2, 3)
    s3.set_formula(1, 0, "=A1+A2")      # 引用第 1、2 行
    s3.set_formula(1, 1, "=A2+A3")      # 引用第 2、3 行
    wb3.recalc()
    check("插入前 B1 = 3", s3.display(1, 0) == "3", s3.display(1, 0))

    # 在第 2 行（index 1）处插入一行
    s3.insert_rows(1, 1)
    wb3.recalc()
    check("插入后 A1 仍是 1", s3.display(0, 0) == "1", s3.display(0, 0))
    check("插入后原第2行被挤到第3行", s3.display(0, 2) == "2", s3.display(0, 2))
    check("插入后原第3行到第4行", s3.display(0, 3) == "3", s3.display(0, 3))
    # 关键语义：Excel 的插入是"引用跟着数据走"，不是"引用位置固定"。
    #   A1 在插入点之前 -> 不动
    #   A2 正好在插入点 -> 它指向的是"原来的第 2 行"，那行数据被挤到第 3 行，
    #                      所以引用必须跟着变成 A3，公式结果才不变。
    # 我最初把这条写成 "A1+A2"（以为 A2 在插入点前不动），是错的。
    check("A1 不动、A2 跟随数据变成 A3", s3.formula(1, 0) == "A1+A3", s3.formula(1, 0))
    check("插入点后的引用后移", s3.formula(1, 2) == "A3+A4", s3.formula(1, 2))

    # 绝对引用不该动
    s3.set_formula(2, 0, "=$A$1*2")
    s3.insert_rows(0, 1)
    wb3.recalc()
    check("绝对引用不随插入移动", s3.formula(2, 1) == "$A$1*2", s3.formula(2, 1))

    # 列方向
    wb4 = engine.Workbook()
    s4 = wb4.sheet(0)
    s4.set_num(0, 0, 10)
    s4.set_num(1, 0, 20)
    s4.insert_cols(1, 1)
    wb4.recalc()
    check("插入列后原B1到C1", s4.display(2, 0) == "20", s4.display(2, 0))

    # 删除行
    wb5 = engine.Workbook()
    s5 = wb5.sheet(0)
    for i in range(4):
        s5.set_num(0, i, i + 1)
    s5.delete_rows(1, 1)
    wb5.recalc()
    check("删除第2行后原第3行上移", s5.display(0, 1) == "3", s5.display(0, 1))

    # 取消合并
    wb6 = engine.Workbook()
    s6 = wb6.sheet(0)
    s6.merge(0, 0, 1, 1)
    check("合并后是锚点", s6.merge_info(0, 0)[0] == 2)
    check("取消合并成功", s6.unmerge(0, 0, 1, 1))
    check("取消后不再是锚点", s6.merge_info(0, 0)[0] == 0)
    check("再取消返回 False", s6.unmerge(0, 0, 1, 1) is False)

    # 存盘再读回
    path = "/tmp/gui_selftest.xlsx"
    if os.path.exists(path):
        os.remove(path)
    wb.recalc()
    check("保存", wb.save(path), wb.last_error)
    wb2 = engine.Workbook()
    check("打开", wb2.load(path), wb2.last_error)
    s2 = wb2.sheet(0)
    check("读回公式", s2.formula(1, 0) == "A1+A2", "得到 %r" % s2.formula(1, 0))
    check("读回值", s2.display(1, 0) == "30", "得到 %r" % s2.display(1, 0))

    print("  库路径: %s" % engine._LIB_PATH)


def test_qt():
    print("== Qt 控件 ==")
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    try:
        from PySide6.QtWidgets import QApplication
        from gui.grid import GridView, col_name, addr, parse_addr
    except ImportError as e:
        print("  跳过（未安装 PySide6: %s）" % e)
        return

    check("列名 0->A", col_name(0) == "A")
    check("列名 25->Z", col_name(25) == "Z")
    check("列名 26->AA", col_name(26) == "AA")
    check("列名 701->ZZ", col_name(701) == "ZZ")
    check("列名 702->AAA", col_name(702) == "AAA")
    check("地址 (0,0)->A1", addr(0, 0) == "A1")
    check("地址 (1,2)->B3", addr(1, 2) == "B3")
    check("解析 B3", parse_addr("B3") == (1, 2))
    check("解析 aa100", parse_addr("aa100") == (26, 99))
    check("解析非法", parse_addr("9A") is None)

    app = QApplication.instance() or QApplication(sys.argv)
    wb = engine.Workbook()
    sh = wb.sheet(0)
    for i in range(5):
        sh.set_num(0, i, i + 1)
    g = GridView(wb)
    g.resize(600, 400)

    g.set_cursor(2, 3)
    check("光标移动", (g.cursor_col, g.cursor_row) == (2, 3))
    g.set_cursor(4, 5, extend=True)
    check("扩展选区", g.sel_rect() == (2, 3, 4, 5), "得到 %r" % (g.sel_rect(),))

    vr = g.visible_range()
    check("可见范围合理", vr[2] >= vr[0] and vr[3] >= vr[1], "得到 %r" % (vr,))

    # 填充：公式的相对引用要跟着走
    sh.set_formula(1, 0, "=A1*2")
    wb.recalc()
    g.set_cursor(1, 0)
    g._do_fill(1, 0, 1, 3)
    wb.recalc()
    check("B1 = A1*2 = 2", sh.display(1, 0) == "2", "得到 %r" % sh.display(1, 0))
    check("填充后 B4 = A4*2 = 8", sh.display(1, 3) == "8", "得到 %r" % sh.display(1, 3))

    # 渲染不崩
    try:
        g.grab()
        check("网格可渲染", True)
    except Exception as e:
        check("网格可渲染", False, str(e))


def main():
    test_bridge()
    try:
        test_qt()
    except Exception as e:
        print("  Qt 测试异常: %s" % e)
    print()
    print("通过 %d 项，失败 %d 项" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
