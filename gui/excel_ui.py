"""
Excel / WPS 风格的交互增强。

单独成文件是为了不动 grid.py 那套已验证过的绘制逻辑 ——
这里只往 MainWindow 上"挂"功能：右键菜单、状态栏统计、字号、
查找替换、排序、合并与插入删除。

为什么需要这些：光有网格和工具栏，用起来还是"能编辑的表格"，
离"Excel 那样"差的是这些高频交互 —— 右键出菜单、选中就能看见求和、
Ctrl+F 能查找。它们不涉及引擎改动，纯界面层。
"""

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction, QKeySequence
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QInputDialog,
    QLabel,
    QLineEdit,
    QMenu,
    QMessageBox,
    QPushButton,
    QVBoxLayout,
    QCheckBox,
    QHBoxLayout,
)

# Excel 的下拉字号序列
FONT_SIZES = [8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72]

# 常用中文字体（Windows / macOS / Linux 都尽量给一份能用的）
FONT_FAMILIES = [
    "默认",
    "微软雅黑",
    "宋体",
    "黑体",
    "楷体",
    "SimSun",
    "Microsoft YaHei",
    "Arial",
    "Calibri",
    "Times New Roman",
    "Consolas",
]


def _to_num(s):
    """把显示文本尽量转成数字；失败返回 None。"""
    if s is None:
        return None
    s = str(s).strip().replace(",", "")
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None


class ExcelUI:
    """挂在主窗口上的一组 Excel 风格交互。"""

    def __init__(self, win):
        self.win = win
        self.clip = None          # 复制的二维数据
        self.clip_is_cut = False
        self.clip_from = None
        self._install_context_menu()
        self._install_font_controls()
        self._install_extra_actions()

    # ------------------------------------------------------------------
    # 右键菜单
    # ------------------------------------------------------------------
    def _install_context_menu(self):
        # 网格默认没有右键菜单，Qt 需要显式打开这个策略才会发 customContextMenuRequested
        self.win.grid.setContextMenuPolicy(Qt.CustomContextMenu)
        self.win.grid.customContextMenuRequested.connect(self._show_menu)

    def _show_menu(self, pos):
        win = self.win
        m = QMenu(win)

        a_copy = m.addAction("复制\tCtrl+C")
        a_cut = m.addAction("剪切\tCtrl+X")
        a_paste = m.addAction("粘贴\tCtrl+V")
        m.addSeparator()
        a_clear = m.addAction("清除内容\tDelete")
        m.addSeparator()
        a_ins_row = m.addAction("插入行")
        a_del_row = m.addAction("删除行")
        a_ins_col = m.addAction("插入列")
        a_del_col = m.addAction("删除列")
        m.addSeparator()
        a_merge = m.addAction("合并单元格")
        a_unmerge = m.addAction("取消合并")
        m.addSeparator()
        a_find = m.addAction("查找替换…\tCtrl+F")

        act = m.exec(self.win.grid.viewport().mapToGlobal(pos))
        if act is None:
            return
        if act == a_copy:
            self.copy()
        elif act == a_cut:
            self.cut()
        elif act == a_paste:
            self.paste()
        elif act == a_clear:
            self.clear_contents()
        elif act == a_ins_row:
            self.insert_rows()
        elif act == a_del_row:
            self.delete_rows()
        elif act == a_ins_col:
            self.insert_cols()
        elif act == a_del_col:
            self.delete_cols()
        elif act == a_merge:
            self.merge()
        elif act == a_unmerge:
            self.unmerge()
        elif act == a_find:
            self.find_replace()

    # ------------------------------------------------------------------
    # 剪贴板
    # ------------------------------------------------------------------
    def _rect(self):
        return self.win.grid.sel_rect()

    def copy(self):
        c0, r0, c1, r1 = self._rect()
        sh = self.win.grid.sheet
        data = []
        for r in range(r0, r1 + 1):
            data.append([sh.raw(c, r) for c in range(c0, c1 + 1)])
        self.clip = data
        self.clip_is_cut = False
        self.clip_from = None
        self.win.status_label.setText(
            "已复制 %d 格" % ((c1 - c0 + 1) * (r1 - r0 + 1)))

    def cut(self):
        self.copy()
        self.clip_is_cut = True
        self.clip_from = self._rect()

    def paste(self):
        if not self.clip:
            return
        win = self.win
        c0, r0, c1, r1 = self._rect()
        # 粘贴目标以当前格为左上角，尺寸跟剪贴板一致
        sh = win.grid.sheet
        cells = []
        for dr, row in enumerate(self.clip):
            for dc, v in enumerate(row):
                cells.append((c0 + dc, r0 + dr, v))
        win._on_cells_will_change(
            [(c, r) for c, r, _ in cells], "粘贴")
        for c, r, v in cells:
            _set_cell(sh, c, r, v)
        if self.clip_is_cut and self.clip_from:
            fc0, fr0, fc1, fr1 = self.clip_from
            for r in range(fr0, fr1 + 1):
                for c in range(fc0, fc1 + 1):
                    sh.erase(c, r)
            self.clip = None
            self.clip_is_cut = False
            self.clip_from = None
        win.wb.recalc()
        win.grid.viewport().update()
        win.dirty = True
        win._update_title()
        win.status_label.setText("已粘贴 %d 格" % len(cells))

    def clear_contents(self):
        win = self.win
        c0, r0, c1, r1 = self._rect()
        sh = win.grid.sheet
        cells = [(c, r) for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)]
        win._on_cells_will_change(cells, "清除内容")
        for c, r in cells:
            sh.erase(c, r)
        win.wb.recalc()
        win.grid.viewport().update()
        win.dirty = True
        win._update_title()
        win.status_label.setText("已清除 %d 格" % len(cells))

    # ------------------------------------------------------------------
    # 结构性编辑
    # ------------------------------------------------------------------
    def insert_rows(self):
        c0, r0, c1, r1 = self._rect()
        self._struct("insert_rows", r0, r1 - r0 + 1, "插入 %d 行")

    def delete_rows(self):
        c0, r0, c1, r1 = self._rect()
        self._struct("delete_rows", r0, r1 - r0 + 1, "删除 %d 行")

    def insert_cols(self):
        c0, r0, c1, r1 = self._rect()
        self._struct("insert_cols", c0, c1 - c0 + 1, "插入 %d 列")

    def delete_cols(self):
        c0, r0, c1, r1 = self._rect()
        self._struct("delete_cols", c0, c1 - c0 + 1, "删除 %d 列")

    def _struct(self, fn, at, n, label):
        win = self.win
        sh = win.grid.sheet
        getattr(sh, fn)(at, n)
        win.wb.recalc()
        win.grid.viewport().update()
        win.dirty = True
        win._update_title()
        win.status_label.setText(label % n)

    def merge(self):
        c0, r0, c1, r1 = self._rect()
        self.win.grid.sheet.merge(c0, r0, c1, r1)
        self.win.grid.viewport().update()
        self.win.dirty = True
        self.win.status_label.setText("已合并")

    def unmerge(self):
        c0, r0, c1, r1 = self._rect()
        try:
            self.win.grid.sheet.unmerge(c0, r0, c1, r1)
        except TypeError:
            self.win.grid.sheet.unmerge(c0, r0)
        self.win.grid.viewport().update()
        self.win.dirty = True
        self.win.status_label.setText("已取消合并")

    # ------------------------------------------------------------------
    # 查找替换
    # ------------------------------------------------------------------
    def find_replace(self):
        win = self.win
        dlg = QDialog(win)
        dlg.setWindowTitle("查找替换")
        lay = QVBoxLayout(dlg)
        form = QFormLayout()
        e_find = QLineEdit()
        e_rep = QLineEdit()
        form.addRow("查找:", e_find)
        form.addRow("替换为:", e_rep)
        lay.addLayout(form)

        row = QHBoxLayout()
        chk_case = QCheckBox("区分大小写")
        chk_formula = QCheckBox("含公式")
        row.addWidget(chk_case)
        row.addWidget(chk_formula)
        lay.addLayout(row)

        bb = QDialogButtonBox(
            QDialogButtonBox.Find | QDialogButtonBox.Replace |
            QDialogButtonBox.ReplaceAll | QDialogButtonBox.Close)
        lay.addWidget(bb)

        state = {"idx": 0, "hits": []}
        sh = win.grid.sheet
        ur = sh.used_range()
        target = e_find.text()

        def collect():
            t = e_find.text()
            if not t:
                return []
            out = []
            for r in range(ur[1], ur[3] + 1):
                for c in range(ur[0], ur[2] + 1):
                    v = sh.raw(c, r) if chk_formula.isChecked() else sh.display(c, r)
                    if not v:
                        continue
                    if chk_case.isChecked():
                        ok = t in v
                    else:
                        ok = t.lower() in v.lower()
                    if ok:
                        out.append((c, r))
            return out

        def do_find():
            state["hits"] = collect()
            if not state["hits"]:
                win.status_label.setText("未找到")
                return
            state["idx"] = (state["idx"] + 1) % len(state["hits"])
            c, r = state["hits"][state["idx"]]
            win.grid.set_cursor(c, r)
            win.status_label.setText(
                "第 %d/%d 个：%s" % (state["idx"] + 1, len(state["hits"]), addr_of(c, r)))

        def do_replace():
            if not state["hits"]:
                do_find()
                return
            c, r = state["hits"][state["idx"]]
            old = sh.raw(c, r)
            new = old.replace(e_find.text(), e_rep.text())
            _set_cell(sh, c, r, new)
            win.wb.recalc()
            win.grid.viewport().update()
            win.dirty = True
            win.status_label.setText("已替换 %s" % addr_of(c, r))

        def do_replace_all():
            hits = collect()
            if not hits:
                win.status_label.setText("未找到")
                return
            win._on_cells_will_change(hits, "全部替换")
            n = 0
            for c, r in hits:
                old = sh.raw(c, r)
                if e_find.text() in old:
                    _set_cell(sh, c, r, old.replace(e_find.text(), e_rep.text()))
                    n += 1
            win.wb.recalc()
            win.grid.viewport().update()
            win.dirty = True
            win.status_label.setText("已替换 %d 处" % n)

        btn_find = bb.button(QDialogButtonBox.Find)
        btn_rep = bb.button(QDialogButtonBox.Replace)
        btn_all = bb.button(QDialogButtonBox.ReplaceAll)
        btn_find.clicked.connect(do_find)
        btn_rep.clicked.connect(do_replace)
        btn_all.clicked.connect(do_replace_all)
        bb.rejected.connect(dlg.close)
        dlg.exec()

    # ------------------------------------------------------------------
    # 排序
    # ------------------------------------------------------------------
    def sort_dialog(self):
        win = self.win
        c0, r0, c1, r1 = self._rect()
        col, ok = QInputDialog.getInt(
            win, "排序", "按第几列排序（%s = %d）："
            % (col_name(c0), c0 + 1), c0 + 1, 1, 200)
        if not ok:
            return
        col -= 1
        desc, ok2 = QInputDialog.getItem(
            win, "排序", "顺序:", ["升序", "降序"], 0, False)
        if not ok2:
            return
        sh = win.grid.sheet
        rows = []
        for r in range(r0, r1 + 1):
            rows.append([sh.raw(c, r) for c in range(c0, c1 + 1)])
        key_i = col - c0

        def key(row):
            v = row[key_i] if 0 <= key_i < len(row) else ""
            n = _to_num(v)
            # 数字与文本分开排：纯文本比较会把 "10" 排到 "9" 前面
            return (0, n, "") if n is not None else (1, 0.0, str(v))

        rows.sort(key=key, reverse=(desc == "降序"))
        win._on_cells_will_change(
            [(c, r) for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)], "排序")
        for dr, row in enumerate(rows):
            for dc, v in enumerate(row):
                _set_cell(sh, c0 + dc, r0 + dr, v)
        win.wb.recalc()
        win.grid.viewport().update()
        win.dirty = True
        win.status_label.setText("已排序 %d 行" % len(rows))

    # ------------------------------------------------------------------
    # 工具栏：字号 / 字体
    # ------------------------------------------------------------------
    def _install_font_controls(self):
        win = self.win
        from PySide6.QtWidgets import QToolBar
        bars = win.findChildren(QToolBar)
        if not bars:
            return
        tb = bars[0]      # 主窗口上第一个工具栏就是格式栏

        self.size_box = QComboBox()
        self.size_box.setEditable(True)
        for s in FONT_SIZES:
            self.size_box.addItem(str(s), s)
        self.size_box.setFixedWidth(64)
        self.size_box.setToolTip("字号")
        self.size_box.currentIndexChanged.connect(self._apply_size)
        tb.addWidget(QLabel(" 字号 "))
        tb.addWidget(self.size_box)

        self.fam_box = QComboBox()
        for f in FONT_FAMILIES:
            self.fam_box.addItem(f)
        self.fam_box.setFixedWidth(110)
        self.fam_box.setToolTip(
            "字体（引擎目前只存字号；字体名留待后续版本写入 xlsx）")
        self.fam_box.setEnabled(False)
        tb.addWidget(QLabel(" 字体 "))
        tb.addWidget(self.fam_box)

    def _apply_size(self, idx):
        if idx < 0:
            return
        win = self.win
        size = self.size_box.itemData(idx)
        if not size:
            return
        c0, r0, c1, r1 = self._rect()
        win._apply_to_selection(
            lambda c, r: win.grid.sheet.style(c, r, size=int(size)),
            "设置字号 %d" % size)

    # ------------------------------------------------------------------
    # 快捷键动作
    # ------------------------------------------------------------------
    def _install_extra_actions(self):
        win = self.win
        self.act_copy = QAction("复制", win)
        self.act_copy.setShortcut(QKeySequence.Copy)
        self.act_copy.triggered.connect(self.copy)
        self.act_cut = QAction("剪切", win)
        self.act_cut.setShortcut(QKeySequence.Cut)
        self.act_cut.triggered.connect(self.cut)
        self.act_paste = QAction("粘贴", win)
        self.act_paste.setShortcut(QKeySequence.Paste)
        self.act_paste.triggered.connect(self.paste)
        self.act_find = QAction("查找替换…", win)
        self.act_find.setShortcut(QKeySequence.Find)
        self.act_find.triggered.connect(self.find_replace)
        self.act_sort = QAction("排序…", win)
        self.act_sort.triggered.connect(self.sort_dialog)
        self.act_merge = QAction("合并单元格", win)
        self.act_merge.triggered.connect(self.merge)
        self.act_unmerge = QAction("取消合并", win)
        self.act_unmerge.triggered.connect(self.unmerge)

        for a in (self.act_copy, self.act_cut, self.act_paste,
                  self.act_find, self.act_sort, self.act_merge, self.act_unmerge):
            win.addAction(a)

    # ------------------------------------------------------------------
    # 状态栏统计（Excel 右下角那一行）
    # ------------------------------------------------------------------
    def update_stats(self):
        win = self.win
        c0, r0, c1, r1 = self._rect()
        sh = win.grid.sheet
        nums = []
        n = 0
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                v = sh.display(c, r)
                if v:
                    n += 1
                x = _to_num(v)
                if x is not None:
                    nums.append(x)
        parts = []
        if n > 1:
            parts.append("选区 %d 格" % ((c1 - c0 + 1) * (r1 - r0 + 1)))
        if nums:
            s = sum(nums)
            parts.append("求和 %.10g" % s)
            parts.append("平均 %.10g" % (s / len(nums)))
            parts.append("最大 %.10g" % max(nums))
            parts.append("最小 %.10g" % min(nums))
        parts.append("计数 %d" % n)
        win.sel_label.setText("   ".join(parts))


# ---------------------------------------------------------------------------
def _set_cell(sh, c, r, v):
    """按 Excel 的方式写入：= 开头当公式，看着像数字当数字，其余当文本。"""
    v = "" if v is None else str(v)
    if v.startswith("="):
        sh.set_formula(c, r, v[1:])
        return
    if v == "":
        sh.erase(c, r)
        return
    n = _to_num(v)
    if n is not None and v.strip().lstrip("+-").replace(".", "", 1).isdigit():
        sh.set_num(c, r, n)
        return
    sh.set_str(c, r, v)


def col_name(c):
    s = ""
    c += 1
    while c > 0:
        c, m = divmod(c - 1, 26)
        s = chr(65 + m) + s
    return s


def addr_of(c, r):
    return "%s%d" % (col_name(c), r + 1)


def install(win):
    """挂到主窗口上，返回 ExcelUI 实例。"""
    ui = ExcelUI(win)
    # 接管状态栏右侧的统计显示
    orig = win._on_selection

    def patched():
        orig()
        ui.update_stats()

    win._on_selection = patched
    return ui
