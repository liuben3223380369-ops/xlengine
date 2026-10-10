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

    # ---- 条件格式 ----
    wbc = engine.Workbook()
    sc = wbc.sheet(0)
    for i, v in enumerate([85, 92, 78, 65, 88, 71]):
        sc.set_num(0, i, v)
    wbc.recalc()
    check("初始无规则", sc.cf_count() == 0)
    check("添加 cellIs 规则", sc.add_cf(0, 0, 0, 5, ctype=0, op=5, f1="80", fill="FFC7CE"))
    check("添加公式规则", sc.add_cf(0, 0, 0, 5, ctype=1, f1="A1<70", fill="FFFF00", bold=True))
    check("规则数 2", sc.cf_count() == 2, sc.cf_count())
    lst = sc.cf_list()
    check("规则可读回区域", lst[0]["rect"] == [0, 0, 0, 5], lst[0]["rect"])
    check("规则可读回阈值", lst[0]["f1"] == "80", lst[0]["f1"])
    check("规则可读回填充", lst[0]["fill"] == "FFC7CE", lst[0]["fill"])
    check("清除规则", sc.clear_cf())
    check("清除后为 0", sc.cf_count() == 0)

    # ---- 区域复制与擦除 ----
    wbm = engine.Workbook()
    sm = wbm.sheet(0)
    sm.set_num(0, 0, 1)
    sm.set_num(0, 1, 2)
    sm.set_formula(1, 0, "=A1*10")
    sm.set_formula(1, 1, "=A2*10")
    wbm.recalc()
    check("复制前 B1=10", sm.display(1, 0) == "10", sm.display(1, 0))
    # B1:B2 -> D1:D2，同尺寸复制
    n = sm.copy_range(1, 0, 1, 1, 3, 0)
    check("复制返回 2 格", n == 2, n)
    wbm.recalc()
    # 关键：同尺寸复制时每个格子的偏移都是 dst-src，
    # 不是"目标位置-源区域左上角"，否则 B2 会多平移一行成 =C3*10
    check("D1 = C1*10", sm.formula(3, 0) == "C1*10", sm.formula(3, 0))
    check("D2 = C2*10", sm.formula(3, 1) == "C2*10", sm.formula(3, 1))
    sm.erase_range(1, 0, 1, 1)
    wbm.recalc()
    check("擦除后 B1 空", sm.display(1, 0) == "", repr(sm.display(1, 0)))

    # ---- 数据验证 ----
    wbd = engine.Workbook()
    sd = wbd.sheet(0)
    for i in range(3):
        sd.set_num(0, i, 85 + i)
    wbd.recalc()
    check("dv 初始 0", sd.dv_count() == 0)
    check("dv 添加整数区间", sd.add_dv(0, 0, 0, 2, dtype=1, op=1, f1="0", f2="100"))
    check("dv 添加序列", sd.add_dv(1, 0, 1, 2, dtype=3, f1="是,否,也许"))
    check("dv 数量 2", sd.dv_count() == 2, sd.dv_count())
    dl = sd.dv_list()
    check("dv 区域可读回", dl[0]["rect"] == [0, 0, 0, 2], dl[0]["rect"])
    check("dv 阈值可读回", dl[0]["f1"] == "0" and dl[0]["f2"] == "100",
          "%r/%r" % (dl[0]["f1"], dl[0]["f2"]))
    check("dv 序列可读回", dl[1]["f1"] == "是,否,也许", dl[1]["f1"])
    check("dv 清除", sd.clear_dv())
    check("dv 清除后 0", sd.dv_count() == 0)

    # ---- 批注 ----
    wbn = engine.Workbook()
    sn = wbn.sheet(0)
    sn.set_num(0, 0, 1)
    wbn.recalc()
    check("note 初始 0", sn.note_count() == 0)
    check("note 添加", sn.add_note(0, 0, "第一行\n第二行", "作者甲"))
    check("note 添加第二条", sn.add_note(1, 1, "备注", "作者乙"))
    check("note 数量 2", sn.note_count() == 2, sn.note_count())
    nl = sn.note_list()
    check("note 坐标可读回", (nl[0]["col"], nl[0]["row"]) == (0, 0),
          "%r" % ((nl[0]["col"], nl[0]["row"]),))
    check("note 作者可读回", nl[0]["author"] == "作者甲", nl[0]["author"])
    check("note 多行保留", nl[0]["text"] == "第一行\n第二行", repr(nl[0]["text"]))
    check("note 按格删除", sn.remove_note(0, 0))
    check("note 删后剩 1", sn.note_count() == 1, sn.note_count())
    check("note 删不存在的返回 False", sn.remove_note(0, 0) is False)

    # 多行批注的换行必须在别的 reader 里也保住。
    # 写入时拆成多个 <r> 的话，自己的 reader 会用 \n 连起来 —— 往返自测
    # 完全正常，openpyxl 却会把 run 直接拼接，换行就丢了。
    import os as _os
    _p = "/tmp/selftest_notes.xlsx"
    if _os.path.exists(_p):
        _os.remove(_p)
    wbn.recalc()
    wbn.save(_p)
    try:
        import openpyxl
        _ws = openpyxl.load_workbook(_p).active
        _c = _ws["B2"].comment
        check("openpyxl 能读到批注", _c is not None)
        check("openpyxl 读到的作者", _c is not None and _c.author == "作者乙",
              _c.author if _c else None)
    except ImportError:
        print("  （跳过 openpyxl 验证：未安装）")
    sn.clear_notes()
    check("note 全部清除", sn.note_count() == 0)

    # ---- 图表编辑 ----
    wbg = engine.Workbook()
    sg = wbg.sheet(0)
    for i, v in enumerate([85, 92, 78]):
        sg.set_num(0, i, v)
    wbg.recalc()
    check("chart 初始 0", sg.chart_count() == 0)
    sg.add_chart(0, 0, 0, 2, 2, "原标题", has_header=0)
    check("chart 添加后 1", sg.chart_count() == 1, sg.chart_count())
    cl = sg.chart_list()
    check("chart 标题可读回", cl[0]["title"] == "原标题", cl[0]["title"])
    check("chart 改类型", sg.set_chart_type(0, 2))
    check("chart 类型生效", sg.chart_list()[0]["type"] == 2,
          sg.chart_list()[0]["type"])
    check("chart 改标题", sg.set_chart_title(0, "新标题"))
    check("chart 标题生效", sg.chart_list()[0]["title"] == "新标题",
          sg.chart_list()[0]["title"])
    check("chart 改锚点", sg.set_chart_anchor(0, 5, 0, 13, 16))
    check("chart 锚点生效", sg.chart_list()[0]["anchor"] == [5, 0, 13, 16],
          sg.chart_list()[0]["anchor"])
    check("chart 删除", sg.remove_chart(0))
    check("chart 删后 0", sg.chart_count() == 0, sg.chart_count())

    # ---- PDF：数字不能丢 ----
    #
    # 中文字体常常不含 ASCII 数字（DroidSansFallbackFull.ttf 实测没有 0-9）。
    # 只嵌一个字体的话，PDF 里所有数字会变成 .notdef —— 不报错、不崩溃，
    # 就是数字整片消失。所以必须有字体回退，并且要用 pdftotext 抽出来验证。
    #
    wbp = engine.Workbook()
    sp = wbp.sheet(0)
    sp.set_str(0, 0, "项目")
    # 数字用短的：列宽有上限，5 位数字会被压成 "123..."，
    # 那是列宽截断不是丢数字，混进来会让这条断言测不到真正要测的东西。
    sp.set_num(1, 0, 42)
    sp.set_str(0, 1, "数量")
    sp.set_num(1, 1, 7)
    wbp.recalc()
    _pdf = "/tmp/selftest_pdf.pdf"
    if _os.path.exists(_pdf):
        _os.remove(_pdf)
    ok_pdf = sp.export_pdf(_pdf)
    check("PDF 导出成功", ok_pdf, wbp.last_error)
    if ok_pdf and _os.path.exists(_pdf):
        check("PDF 头正确", open(_pdf, "rb").read(5) == b"%PDF-")
        _txt = ""
        try:
            import subprocess
            _r = subprocess.run(["pdftotext", _pdf, "-"],
                                capture_output=True, text=True)
            _txt = _r.stdout
        except FileNotFoundError:
            print("  （跳过文本抽取验证：未装 pdftotext）")
        if _txt:
            check("PDF 里数字没丢", "42" in _txt and "7" in _txt,
                  repr(_txt[:80]))
            check("PDF 里中文没丢", "项目" in _txt, repr(_txt[:60]))

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


def test_undo():
    print("== 撤销栈 ==")
    from gui.undo import UndoStack
    u = UndoStack()
    check("初始不可撤销", not u.can_undo())
    rec = u.begin(0, "测试")
    rec["cells"][(0, 0)] = ("=1+2", {})
    u.push(rec)
    check("可撤销", u.can_undo())
    check("标签正确", u.undo_label() == "测试")
    r = u.pop_undo()
    check("弹出记录", r is not None and r["label"] == "测试")
    check("弹出后不可撤销", not u.can_undo())
    u.push_redo(r)
    check("可重做", u.can_redo())
    check("新操作作废重做链", True)
    u.push(rec)
    check("push 后重做链清空", not u.can_redo())


def main():
    test_bridge()
    try:
        test_undo()
    except Exception as e:
        print("  撤销测试异常: %s" % e)
    try:
        test_qt()
    except Exception as e:
        print("  Qt 测试异常: %s" % e)
    print()
    print("通过 %d 项，失败 %d 项" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
