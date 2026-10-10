"""
xlengine 表格 —— 主窗口。

结构（从上到下，与 Excel/WPS 一致）：
    菜单栏
    工具栏（格式按钮）
    公式栏（地址框 + 内容编辑框）
    网格
    表签栏
    状态栏
"""
import os
import sys

from PySide6.QtCore import Qt, Signal, QSize
from PySide6.QtGui import (QAction, QColor, QFont, QIcon, QKeySequence,
                           QPixmap, QPainter)
from PySide6.QtWidgets import (QApplication, QColorDialog, QComboBox,
                               QDialog, QDialogButtonBox, QFileDialog,
                               QFormLayout, QHBoxLayout, QInputDialog,
                               QLabel, QLineEdit, QMainWindow, QMessageBox,
                               QPushButton, QSplitter, QStatusBar, QTabBar,
                               QTextEdit, QToolBar, QVBoxLayout, QWidget,
                               QSpinBox, QCheckBox)

from . import engine
from .grid import GridView, addr, col_name
from .undo import UndoStack

APP_NAME = "xlengine 表格"

# 数字格式预设（引擎支持的格式码）
NUM_FORMATS = [
    ("常规", ""),
    ("数值 1,234.57", "#,##0.00"),
    ("整数 1,235", "#,##0"),
    ("货币 ¥1,234.57", "¥#,##0.00"),
    ("百分比 12.34%", "0.00%"),
    ("日期 2026-09-27", "yyyy-mm-dd"),
    ("日期 2026年9月27日", "yyyy\"年\"m\"月\"d\"日\""),
    ("时间 13:45:30", "h:mm:ss"),
    ("科学计数", "0.00E+00"),
]

CHART_TYPES = [
    ("柱状图", 0), ("条形图", 1), ("折线图", 2), ("面积图", 3),
    ("饼图", 4), ("散点图", 5), ("堆积柱状图", 6), ("堆积面积图", 7),
    ("直方图", 8),
]


def _icon_swatch(color, size=16):
    """用纯色小方块当图标 —— 避免依赖图标资源文件。"""
    pm = QPixmap(size, size)
    pm.fill(QColor(color or "#ffffff"))
    return QIcon(pm)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.wb = engine.Workbook()
        self.undo = UndoStack()
        self.file_path = None
        self.dirty = False

        self.grid = GridView(self.wb)
        self.setCentralWidget(self.grid)

        self._make_actions()
        self._make_undo_actions()
        self._make_menu()
        self._make_toolbar()
        self._make_formula_bar()
        self._make_tabs()
        self._make_status()

        self.grid.selectionChanged.connect(self._on_selection)
        self.grid.cellsWillChange.connect(self._on_cells_will_change)
        self.grid.statusMessage.connect(self._on_status)
        self.grid.modified.connect(self._on_modified)

        # Excel / WPS 风格的交互增强：右键菜单、状态栏统计、字号、
        # 查找替换、排序、合并与插入删除。
        # 挂在这一步（信号连好之后），因为它会包装 _on_selection 补统计。
        try:
            from gui.excel_ui import install as _install_excel_ui
            self.excel_ui = _install_excel_ui(self)
        except Exception as e:
            # 界面增强挂不上不该让整个程序起不来
            self.excel_ui = None
            print("Excel 风格增强未启用: %s" % e)

        self.setWindowTitle(APP_NAME)
        self.resize(1100, 700)
        self._new_workbook()

    # ------------------------------------------------------------------
    # 构建界面
    # ------------------------------------------------------------------
    def _make_actions(self):
        self.act_new = QAction("新建", self)
        self.act_new.setShortcut(QKeySequence.New)
        self.act_new.triggered.connect(self._new_workbook)
        self.act_open = QAction("打开…", self)
        self.act_open.setShortcut(QKeySequence.Open)
        self.act_open.triggered.connect(self._open)
        self.act_save = QAction("保存", self)
        self.act_save.setShortcut(QKeySequence.Save)
        self.act_save.triggered.connect(self._save)
        self.act_save_as = QAction("另存为…", self)
        self.act_save_as.setShortcut(QKeySequence.SaveAs)
        self.act_save_as.triggered.connect(self._save_as)
        self.act_export_pdf = QAction("导出 PDF…", self)
        self.act_export_pdf.triggered.connect(self._export_pdf)
        self.act_quit = QAction("退出", self)
        self.act_quit.setShortcut(QKeySequence.Quit)
        self.act_quit.triggered.connect(self.close)

        self.act_bold = QAction("粗体", self)
        self.act_bold.setShortcut(QKeySequence.Bold)
        self.act_bold.setCheckable(True)
        self.act_bold.triggered.connect(lambda: self._toggle_font(bold=True))
        self.act_italic = QAction("斜体", self)
        self.act_italic.setShortcut(QKeySequence.Italic)
        self.act_italic.setCheckable(True)
        self.act_italic.triggered.connect(lambda: self._toggle_font(italic=True))
        self.act_underline = QAction("下划线", self)
        self.act_underline.setShortcut(QKeySequence.Underline)
        self.act_underline.setCheckable(True)
        self.act_underline.triggered.connect(lambda: self._toggle_font(underline=True))

        self.act_chart = QAction("插入图表…", self)
        self.act_chart.triggered.connect(self._insert_chart)
        self.act_freeze = QAction("冻结窗格…", self)
        self.act_freeze.triggered.connect(self._freeze)
        self.act_filter = QAction("自动筛选", self)
        self.act_filter.triggered.connect(self._autofilter)

        self.act_funcs = QAction("函数列表…", self)
        self.act_funcs.triggered.connect(self._show_funcs)
        self.act_about = QAction("关于", self)
        self.act_about.triggered.connect(self._about)
        self.act_demo = QAction("填入示例数据", self)
        self.act_demo.triggered.connect(self._fill_demo)

    def _make_undo_actions(self):
        from PySide6.QtGui import QKeySequence
        self.act_undo = QAction("撤销", self)
        self.act_undo.setShortcut(QKeySequence.Undo)
        self.act_undo.triggered.connect(self._do_undo)
        self.act_redo = QAction("重做", self)
        self.act_redo.setShortcut(QKeySequence.Redo)
        self.act_redo.triggered.connect(self._do_redo)
        self.act_undo.setEnabled(False)
        self.act_redo.setEnabled(False)

    def _make_menu(self):
        m = self.menuBar()
        f = m.addMenu("文件(&F)")
        for a in (self.act_new, self.act_open, self.act_save, self.act_save_as):
            f.addAction(a)
        f.addSeparator()
        f.addAction(self.act_export_pdf)
        f.addSeparator()
        f.addAction(self.act_quit)

        e = m.addMenu("编辑(&E)")
        e.addAction(self.act_undo)
        e.addAction(self.act_redo)
        e.addSeparator()
        e.addAction("清除内容", self.grid._clear_selection).setShortcut(QKeySequence.Delete)

        v = m.addMenu("视图(&V)")
        v.addAction(self.act_freeze)
        v.addAction(self.act_filter)

        i = m.addMenu("插入(&I)")
        i.addAction(self.act_chart)
        i.addAction("管理已有图表…", self._manage_chart)

        d = m.addMenu("数据(&D)")
        d.addAction("数据验证与批注…", self._manage_dv_note)

        fm = m.addMenu("格式(&O)")
        fm.addAction("条件格式规则…", self._manage_cf)
        fm.addAction(self.act_bold)
        fm.addAction(self.act_italic)
        fm.addAction(self.act_underline)
        fm.addSeparator()
        fm.addAction("列宽…", self._set_col_width)
        fm.addAction("行高…", self._set_row_height)

        h = m.addMenu("帮助(&H)")
        h.addAction(self.act_demo)
        h.addAction(self.act_funcs)
        h.addSeparator()
        h.addAction(self.act_about)

    def _make_toolbar(self):
        tb = QToolBar("格式")
        tb.setIconSize(QSize(16, 16))
        self.addToolBar(tb)

        for a in (self.act_bold, self.act_italic, self.act_underline):
            tb.addAction(a)
        tb.addSeparator()

        self.btn_fill = QPushButton()
        self.btn_fill.setIcon(_icon_swatch("#ffff00"))
        self.btn_fill.setToolTip("填充颜色")
        self.btn_fill.setFixedSize(28, 26)
        self.btn_fill.clicked.connect(self._pick_fill)
        tb.addWidget(self.btn_fill)

        self.btn_font_color = QPushButton()
        self.btn_font_color.setIcon(_icon_swatch("#ff0000"))
        self.btn_font_color.setToolTip("字体颜色")
        self.btn_font_color.setFixedSize(28, 26)
        self.btn_font_color.clicked.connect(self._pick_font_color)
        tb.addWidget(self.btn_font_color)
        tb.addSeparator()

        # 对齐
        self.align_box = QComboBox()
        self.align_box.addItems(["常规", "左对齐", "居中", "右对齐"])
        self.align_box.setFixedWidth(80)
        self.align_box.currentIndexChanged.connect(self._apply_align)
        tb.addWidget(QLabel(" 对齐 "))
        tb.addWidget(self.align_box)
        tb.addSeparator()

        self.border_box = QComboBox()
        # 必须与引擎的 BorderStyle 枚举严格对应：
        #   None=0 Thin=1 Medium=2 Thick=3 Double=4
        # 多给一项就会静默错位（比如"虚线"映射到一个不存在的样式）。
        self.border_box.addItems(["无边框", "细线", "中等", "粗线", "双线"])
        self.border_box.setFixedWidth(80)
        self.border_box.currentIndexChanged.connect(self._apply_border)
        tb.addWidget(QLabel(" 边框 "))
        tb.addWidget(self.border_box)
        tb.addSeparator()

        self.fmt_box = QComboBox()
        for name, code in NUM_FORMATS:
            self.fmt_box.addItem(name, code)
        self.fmt_box.setFixedWidth(140)
        self.fmt_box.currentIndexChanged.connect(self._apply_numfmt)
        tb.addWidget(QLabel(" 格式 "))
        tb.addWidget(self.fmt_box)

        tb.addAction(self.act_chart)

    def _make_formula_bar(self):
        w = QWidget()
        lay = QHBoxLayout(w)
        lay.setContentsMargins(4, 2, 4, 2)

        self.addr_box = QLineEdit()
        self.addr_box.setFixedWidth(80)
        self.addr_box.setToolTip("输入地址后回车跳转，如 B12")
        self.addr_box.returnPressed.connect(self._goto_addr)

        self.fx_icon = QLabel("fx")
        self.fx_icon.setFixedWidth(20)

        self.formula_edit = QLineEdit()
        self.formula_edit.returnPressed.connect(self._commit_formula)
        self.formula_edit.setToolTip("编辑当前格内容；以 = 开头即为公式")

        lay.addWidget(self.addr_box)
        lay.addWidget(self.fx_icon)
        lay.addWidget(self.formula_edit)

        bar = QToolBar("公式栏")
        bar.setMovable(False)
        bar.setFloatable(False)
        bar.addWidget(w)
        self.addToolBar(bar)
        self.formula_bar = bar

    def _make_tabs(self):
        self.tabs = QTabBar()
        self.tabs.setTabsClosable(False)
        self.tabs.setMovable(False)
        self.tabs.currentChanged.connect(self.grid.set_sheet)
        tb = QToolBar("工作表")
        tb.setMovable(False)
        tb.setFloatable(False)
        tb.addWidget(self.tabs)

        add_btn = QPushButton("+")
        add_btn.setFixedSize(26, 24)
        add_btn.setToolTip("新建工作表")
        add_btn.clicked.connect(self._add_sheet)
        tb.addWidget(add_btn)
        self.addToolBar(Qt.BottomToolBarArea, tb)
        self.tab_bar = tb

    def _make_status(self):
        sb = QStatusBar()
        self.setStatusBar(sb)
        self.status_label = QLabel("就绪")
        sb.addWidget(self.status_label)
        self.sel_label = QLabel("")
        sb.addPermanentWidget(self.sel_label)
        self.status_bar = sb

    # ------------------------------------------------------------------
    # 信号
    # ------------------------------------------------------------------
    def _on_selection(self):
        c, r = self.grid.cursor_col, self.grid.cursor_row
        self.addr_box.setText(addr(c, r))
        self.formula_edit.setText(self.grid.sheet.raw(c, r))
        c0, r0, c1, r1 = self.grid.sel_rect()
        n = (c1 - c0 + 1) * (r1 - r0 + 1)
        if n > 1:
            self.sel_label.setText("选区 %d 格" % n)
        else:
            self.sel_label.setText("")
        # 同步工具栏按钮状态
        sty = self.grid.sheet.get_style(c, r)
        self.act_bold.setChecked(sty.get("bold", False))
        self.act_italic.setChecked(sty.get("italic", False))
        h = sty.get("halign", 0)
        self.align_box.blockSignals(True)
        self.align_box.setCurrentIndex(h if 0 <= h <= 3 else 0)
        self.align_box.blockSignals(False)
        b = sty.get("border", 0)
        self.border_box.blockSignals(True)
        self.border_box.setCurrentIndex(b if 0 <= b <= 4 else 0)
        self.border_box.blockSignals(False)

    def _on_status(self, msg):
        self.status_label.setText(msg)

    def _update_title(self):
        name = os.path.basename(self.file_path) if self.file_path else "未命名"
        star = " *" if self.dirty else ""
        self.setWindowTitle("%s - %s%s" % (name, APP_NAME, star))

    # ------------------------------------------------------------------
    # 文件
    # ------------------------------------------------------------------
    def _new_workbook(self):
        if not self._maybe_save():
            return
        self.wb = engine.Workbook()
        self.grid.wb = self.wb
        self.grid.set_sheet(0)
        self.grid._load_layout()
        self.file_path = None
        self.dirty = False
        self._refresh_tabs()
        self._update_title()
        self.status_label.setText("已新建")

    def _open(self):
        if not self._maybe_save():
            return
        path, _ = QFileDialog.getOpenFileName(
            self, "打开电子表格", "", "Excel 工作簿 (*.xlsx);;所有文件 (*)")
        if not path:
            return
        wb = engine.Workbook()
        if not wb.load(path):
            QMessageBox.critical(self, "打开失败",
                                 "无法打开该文件：\n%s\n\n%s" % (path, wb.last_error))
            return
        # 先用新对象读，成功后再替换 —— 避免读到一半失败把手头的内容弄丢
        self.wb = wb
        self.grid.wb = wb
        self.grid.sheet_index = 0
        self.grid.anchor_col = self.grid.anchor_row = 0
        self.grid.cursor_col = self.grid.cursor_row = 0
        self.grid._load_layout()
        self.file_path = path
        self.dirty = False
        self._refresh_tabs()
        self._update_title()
        self.grid.viewport().update()
        self.status_label.setText("已打开 %s" % os.path.basename(path))

    def _save(self):
        if not self.file_path:
            return self._save_as()
        return self._do_save(self.file_path)

    def _save_as(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "保存电子表格", self.file_path or "工作簿1.xlsx",
            "Excel 工作簿 (*.xlsx);;所有文件 (*)")
        if not path:
            return False
        if not path.lower().endswith(".xlsx"):
            path += ".xlsx"
        return self._do_save(path)

    def _do_save(self, path):
        self.wb.recalc()
        if not self.wb.save(path):
            QMessageBox.critical(self, "保存失败",
                                 "无法保存到：\n%s\n\n%s" % (path, self.wb.last_error))
            return False
        self.file_path = path
        self.dirty = False
        self._update_title()
        self.status_label.setText("已保存 %s" % os.path.basename(path))
        return True

    def _maybe_save(self):
        if not self.dirty:
            return True
        r = QMessageBox.question(
            self, "确认", "当前工作簿有未保存的修改，要保存吗？",
            QMessageBox.Save | QMessageBox.Discard | QMessageBox.Cancel)
        if r == QMessageBox.Save:
            return self._save()
        if r == QMessageBox.Cancel:
            return False
        return True

    def _export_pdf(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "导出 PDF", "表格.pdf", "PDF 文件 (*.pdf);;所有文件 (*)")
        if not path:
            return
        if not path.lower().endswith(".pdf"):
            path += ".pdf"
        dlg = QDialog(self)
        dlg.setWindowTitle("导出 PDF")
        lay = QFormLayout(dlg)
        chk_land = QCheckBox("横向")
        lay.addRow(chk_land)
        chk_all = QCheckBox("导出所有工作表到一个 PDF")
        chk_all.setChecked(True)
        lay.addRow(chk_all)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(dlg.accept())
        bb.rejected.connect(dlg.reject())
        lay.addRow(bb)
        if dlg.exec() != QDialog.Accepted:
            return
        if chk_all.isChecked():
            n = self.wb.export_pdf_all(path, "", chk_land.isChecked())
            if n < 0:
                QMessageBox.critical(self, "导出失败",
                                     "无法导出到：\n%s\n\n%s" % (path, self.wb.last_error))
                return
            self.status_label.setText("已导出 %d 张表到 %s" % (n, os.path.basename(path)))
        else:
            if not self.grid.sheet.export_pdf(path, "", chk_land.isChecked()):
                QMessageBox.critical(self, "导出失败",
                                     "无法导出到：\n%s\n\n%s" % (path, self.wb.last_error))
                return
            self.status_label.setText("已导出 %s" % os.path.basename(path))

    def closeEvent(self, ev):
        if self._maybe_save():
            ev.accept()
        else:
            ev.ignore()

    # ------------------------------------------------------------------
    # 表签
    # ------------------------------------------------------------------
    def _refresh_tabs(self):
        self.tabs.blockSignals(True)
        while self.tabs.count():
            self.tabs.removeTab(0)
        for n in self.wb.sheet_names():
            self.tabs.addTab(n)
        self.tabs.setCurrentIndex(self.grid.sheet_index)
        self.tabs.blockSignals(False)

    def _add_sheet(self):
        name, ok = QInputDialog.getText(self, "新建工作表", "名称：",
                                        text="Sheet%d" % (self.wb.sheet_count + 1))
        if not ok:
            return
        idx = self.wb.add_sheet(name)
        self._refresh_tabs()
        self.tabs.setCurrentIndex(idx)
        self.dirty = True
        self._update_title()

    # ------------------------------------------------------------------
    # 格式
    # ------------------------------------------------------------------
    def _sel(self):
        return self.grid.sel_rect()

    def _apply_to_selection(self, fn, label="修改"):
        """对选区逐格执行 fn，并把改动记入撤销栈。

        快照必须在**改之前**取 —— 改完再取到的就是新值，撤销等于没做。
        """
        c0, r0, c1, r1 = self._sel()
        sh = self.grid.sheet
        rec = self.undo.begin(self.grid.sheet_index, label)
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                rec["cells"][(c, r)] = sh.snapshot(c, r)
        for (c, r) in list(rec["cells"]):
            fn(sh, c, r)
        self.undo.push(rec)
        self.wb.recalc()
        self.grid.modified.emit()
        self.grid.viewport().update()
        self._update_undo_actions()

    def _toggle_font(self, bold=False, italic=False, underline=False):
        c, r = self.grid.cursor_col, self.grid.cursor_row
        want_bold = self.act_bold.isChecked() if bold else None
        want_italic = self.act_italic.isChecked() if italic else None
        self._apply_to_selection(
            lambda sh, c, r: sh.style(c, r, bold=want_bold, italic=want_italic), "字体")

    def _pick_fill(self):
        col = QColorDialog.getColor(QColor("#ffff00"), self, "填充颜色")
        if not col.isValid():
            return
        rgb = "%02X%02X%02X" % (col.red(), col.green(), col.blue())
        self.btn_fill.setIcon(_icon_swatch("#" + rgb))
        self._apply_to_selection(lambda sh, c, r: sh.style(c, r, fill=rgb), "填充色")

    def _pick_font_color(self):
        col = QColorDialog.getColor(QColor("#ff0000"), self, "字体颜色")
        if not col.isValid():
            return
        rgb = "%02X%02X%02X" % (col.red(), col.green(), col.blue())
        self.btn_font_color.setIcon(_icon_swatch("#" + rgb))
        self._apply_to_selection(lambda sh, c, r: sh.style(c, r, font_color=rgb), "字体颜色")

    def _apply_align(self, idx):
        self._apply_to_selection(lambda sh, c, r: sh.style(c, r, halign=idx), "对齐")

    def _apply_border(self, idx):
        self._apply_to_selection(lambda sh, c, r: sh.style(c, r, border=idx), "边框")

    def _apply_numfmt(self, idx):
        code = self.fmt_box.itemData(idx) or ""
        self._apply_to_selection(lambda sh, c, r: sh.set_numfmt(c, r, code), "数字格式")

    def _on_cells_will_change(self, cells, label):
        """改动前取快照。由 GridView 在真正写入之前发出。"""
        if not cells:
            return
        sh = self.grid.sheet
        rec = self.undo.begin(self.grid.sheet_index, label)
        for (c, r) in cells:
            rec["cells"][(c, r)] = sh.snapshot(c, r)
        self._pending_rec = rec      # 改动完成后由 _on_modified 提交

    def _on_modified(self):
        self.dirty = True
        self._update_title()
        # 提交刚才记的快照。放在这里而不是改前，
        # 是因为改前还不知道这次改动会不会真的成功。
        if getattr(self, "_pending_rec", None):
            self.undo.push(self._pending_rec)
            self._pending_rec = None
        self._update_undo_actions()

    def _update_undo_actions(self):
        if hasattr(self, "act_undo"):
            self.act_undo.setEnabled(self.undo.can_undo())
            self.act_redo.setEnabled(self.undo.can_redo())

    def _do_undo(self):
        rec = self.undo.pop_undo()
        if not rec:
            return
        if rec["struct"]:
            # 插入/删除行列无法用格子快照还原，明确告知而不是静默失败
            self.undo.push_undo(rec)
            self.status_label.setText("结构性编辑（插入/删除行列）暂不支持撤销")
            return
        # 先把当前值存进 redo，再把 before 写回
        sh = self.wb.sheet(rec["sheet"])
        back = self.undo.begin(rec["sheet"], rec["label"])
        for (c, r) in rec["cells"]:
            back["cells"][(c, r)] = sh.snapshot(c, r)
        for (c, r), before in rec["cells"].items():
            sh.restore(c, r, before)
        self.undo.push_redo(back)
        self.wb.recalc()
        self.grid.modified.emit()
        self.grid.viewport().update()
        self._update_undo_actions()
        self.status_label.setText("已撤销：%s" % rec["label"])

    def _do_redo(self):
        rec = self.undo.pop_redo()
        if not rec:
            return
        if rec["struct"]:
            self.undo.push_redo(rec)
            return
        sh = self.wb.sheet(rec["sheet"])
        back = self.undo.begin(rec["sheet"], rec["label"])
        for (c, r) in rec["cells"]:
            back["cells"][(c, r)] = sh.snapshot(c, r)
        for (c, r), after in rec["cells"].items():
            sh.restore(c, r, after)
        self.undo.push_undo(back)
        self.wb.recalc()
        self.grid.modified.emit()
        self.grid.viewport().update()
        self._update_undo_actions()
        self.status_label.setText("已重做：%s" % rec["label"])

    def _set_col_width(self):
        c = self.grid.cursor_col
        w, ok = QInputDialog.getDouble(self, "列宽", "宽度（像素）：",
                                       self.grid.col_w(c), 20, 1000, 0)
        if ok:
            self.grid.set_col_w(c, w)
            self.dirty = True
            self._update_title()

    def _set_row_height(self):
        r = self.grid.cursor_row
        h, ok = QInputDialog.getDouble(self, "行高", "高度（像素）：",
                                       self.grid.row_h(r), 12, 500, 0)
        if ok:
            self.grid.set_row_h(r, h)
            self.dirty = True
            self._update_title()

    # ------------------------------------------------------------------
    # 公式栏
    # ------------------------------------------------------------------
    def _goto_addr(self):
        p = self.grid
        t = self.addr_box.text()
        res = None
        try:
            from .grid import parse_addr
            res = parse_addr(t)
        except Exception:
            res = None
        if res is None:
            self.status_label.setText("无效地址: " + t)
            return
        p.set_cursor(res[0], res[1])

    def _commit_formula(self):
        text = self.formula_edit.text()
        c, r = self.grid.cursor_col, self.grid.cursor_row
        if self.grid.sheet.set(c, r, text):
            self.wb.recalc()
            self.grid.modified.emit()
            self.grid.viewport().update()
            self.status_label.setText("就绪")
        else:
            self.status_label.setText("公式错误: " + self.wb.last_error)

    # ------------------------------------------------------------------
    # 图表 / 视图
    # ------------------------------------------------------------------
    def _insert_chart(self):
        c0, r0, c1, r1 = self._sel()
        if c0 == c1 and r0 == r1:
            QMessageBox.information(self, "插入图表",
                                    "请先选中一块数据区域，再插入图表。")
            return
        dlg = QDialog(self)
        dlg.setWindowTitle("插入图表")
        lay = QFormLayout(dlg)
        cb = QComboBox()
        for n, t in CHART_TYPES:
            cb.addItem(n, t)
        lay.addRow("图表类型：", cb)
        title = QLineEdit("销售额")
        lay.addRow("标题：", title)
        chk_header = QCheckBox("首行/首列为标签")
        chk_header.setChecked(True)
        lay.addRow(chk_header)
        chk_rows = QCheckBox("系列来自行（不打勾则来自列）")
        lay.addRow(chk_rows)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(dlg.accept())
        bb.rejected.connect(dlg.reject())
        lay.addRow(bb)
        if dlg.exec() != QDialog.Accepted:
            return
        idx = self.grid.sheet.add_chart(
            cb.currentData(), c0, r0, c1, r1, title.text(),
            chk_header.isChecked(), chk_rows.isChecked())
        if idx < 0:
            QMessageBox.warning(self, "插入失败", self.wb.last_error or "无法创建图表")
            return
        self.dirty = True
        self._update_title()
        self.status_label.setText("已插入图表：%s（保存后在 Excel 中可见）" % title.text())

    def _freeze(self):
        dlg = QDialog(self)
        dlg.setWindowTitle("冻结窗格")
        lay = QFormLayout(dlg)
        cols = QSpinBox()
        cols.setRange(0, 50)
        rows = QSpinBox()
        rows.setRange(0, 50)
        lay.addRow("冻结左侧列数：", cols)
        lay.addRow("冻结顶部行数：", rows)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(dlg.accept())
        bb.rejected.connect(dlg.reject())
        lay.addRow(bb)
        if dlg.exec() != QDialog.Accepted:
            return
        self.grid.set_freeze(cols.value(), rows.value())
        self.dirty = True
        self._update_title()
        self.status_label.setText("已冻结 %d 列 %d 行（保存后生效）" %
                                  (cols.value(), rows.value()))

    def _autofilter(self):
        c0, r0, c1, r1 = self._sel()
        self.grid.set_filter(c0, r0, c1, r1)
        self.dirty = True
        self._update_title()
        self.status_label.setText("已设置自动筛选 %s:%s" % (addr(c0, r0), addr(c1, r1)))

    # ------------------------------------------------------------------
    # 帮助
    # ------------------------------------------------------------------
    def _manage_cf(self):
        """条件格式规则管理器。"""
        from .cfdialog import CfDialog
        dlg = CfDialog(self.grid.sheet, self._sel(), self)
        if dlg.exec() == QDialog.Accepted or True:
            self.wb.recalc()
            self.grid.modified.emit()
            self.grid.viewport().update()
            self.status_label.setText("条件格式规则 %d 条" % self.grid.sheet.cf_count())

    def _manage_dv_note(self):
        """数据验证 + 批注 管理器。"""
        from .dvdialog import DvNoteDialog
        dlg = DvNoteDialog(self.grid.sheet, self._sel(), self)
        dlg.exec()
        self.wb.recalc()
        self.grid.modified.emit()
        self.grid.viewport().update()
        self.status_label.setText("数据验证 %d 条 / 批注 %d 条"
                                  % (self.grid.sheet.dv_count(),
                                     self.grid.sheet.note_count()))

    def _manage_chart(self):
        """图表管理器：改类型/标题/位置，或删除。"""
        from .chartdialog import ChartDialog
        dlg = ChartDialog(self.grid.sheet, self)
        dlg.exec()
        self.grid.modified.emit()
        self.grid.viewport().update()
        self.status_label.setText("图表 %d 个" % self.grid.sheet.chart_count())

    def _show_funcs(self):
        names = sorted(set(engine.func_names()))
        dlg = QDialog(self)
        dlg.setWindowTitle("函数列表（%d 个）" % len(names))
        dlg.resize(560, 480)
        lay = QVBoxLayout(dlg)
        edit = QLineEdit()
        edit.setPlaceholderText("输入关键字筛选…")
        box = QTextEdit()
        box.setReadOnly(True)
        box.setFont(QFont("Consolas" if sys.platform == "win32" else "monospace", 10))
        lay.addWidget(edit)
        lay.addWidget(box)

        def render(filt=""):
            f = filt.strip().upper()
            hit = [n for n in names if f in n] if f else names
            # 每行 4 个，方便扫读
            lines = []
            for i in range(0, len(hit), 4):
                lines.append("".join("%-18s" % n for n in hit[i:i + 4]))
            box.setPlainText("\n".join(lines) if lines else "（无匹配）")
            box.moveCursor(box.textCursor().Start)

        edit.textChanged.connect(render)
        render()
        bb = QDialogButtonBox(QDialogButtonBox.Close)
        bb.rejected.connect(dlg.reject)
        lay.addWidget(bb)
        dlg.exec()

    def _about(self):
        QMessageBox.about(
            self, "关于",
            "<b>%s</b><br><br>"
            "自研电子表格引擎的图形界面。<br>"
            "计算引擎为 C++ 实现，含 %d 个函数；界面用 Qt（PySide6）编写。<br><br>"
            "仅依赖 zlib，无第三方表格库。<br><br>"
            "已知边界见项目 README。" % (APP_NAME, engine.func_count()))

    # ------------------------------------------------------------------
    def _fill_demo(self):
        """填一份示例，方便立刻看到效果。"""
        sh = self.grid.sheet
        headers = ["地区", "一季度", "二季度", "三季度", "四季度", "合计", "占比"]
        for c, h in enumerate(headers):
            sh.style(c, 0, bold=True, fill="DDEBF7", halign=2, border=1,
                     border_color="9BB7D4")
            sh.set_str(c, 0, h)
        data = [("华北", 120, 165, 143, 178),
                ("华东", 98, 132, 121, 154),
                ("华南", 87, 110, 96, 132),
                ("西南", 64, 78, 85, 91),
                ("东北", 52, 61, 58, 70)]
        for i, (name, *vals) in enumerate(data, start=1):
            sh.set_str(0, i, name)
            for j, v in enumerate(vals):
                sh.set_num(1 + j, i, v)
            sh.set_formula(5, i, "=SUM(B%d:E%d)" % (i + 1, i + 1))
            sh.set_formula(6, i, "=F%d/SUM($F$2:$F$6)" % (i + 1))
            sh.set_numfmt(6, i, "0.0%")
        # 合计行
        r = len(data) + 1
        sh.set_str(0, r, "合计")
        for c in range(1, 6):
            sh.set_formula(c, r, "=SUM(%s2:%s%d)" % (col_name(c), col_name(c), len(data) + 1))
        for c in range(7):
            sh.style(c, r, bold=True, fill="F2F2F2", border=1, border_color="BFBFBF")
        for c in range(1, 6):
            for i in range(1, len(data) + 1):
                sh.set_numfmt(c, i, "#,##0")
        for c in range(7):
            self.grid.set_col_w(c, 90)
        self.grid.set_col_w(0, 100)
        self.wb.recalc()
        self.grid.viewport().update()
        self.dirty = True
        self._update_title()
        self.status_label.setText("已填入示例数据（含公式、百分比格式、合计行）")


def main():
    # --selftest: 不起窗口，跑完界面自检就退出。
    # CI 上的 Windows runner 没有显示器，用这个在离屏模式下验证
    # "DLL 找得到、引擎能加载、界面能构造" —— 打包后能不能跑，靠它兜底。
    if "--selftest" in sys.argv:
        import gui.selftest as _st

        app = QApplication(sys.argv)
        app.setApplicationName(APP_NAME)
        # main() 自己会打印"通过 N 项，失败 M 项"，返回 0/1 退出码
        return _st.main()

    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
