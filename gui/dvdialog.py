"""
数据验证与批注的管理器。

之前这两项只能在终端里设，界面上看不到已有哪些规则、也没法删 ——
设错了只能重开文件。条件格式已经先做了管理器，这里补齐另外两个。

与条件格式管理器的一个共同点：引擎只提供"整体清除"，没有"删单条"。
所以删单条都走"清掉再重建其余"的路径。批注例外 —— 它按格存储，
可以直接按坐标删。
"""
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox,
                               QFormLayout, QGroupBox, QHBoxLayout, QLabel,
                               QLineEdit, QListWidget, QMessageBox, QPushButton,
                               QTabWidget, QTextEdit, QVBoxLayout)

from .grid import addr


class DvNoteDialog(QDialog):
    def __init__(self, sheet, sel, parent=None):
        super().__init__(parent)
        self.sheet = sheet
        self.sel = sel
        self.setWindowTitle("数据验证与批注")
        self.resize(640, 520)

        root = QVBoxLayout(self)
        tabs = QTabWidget()
        root.addWidget(tabs, 1)

        tabs.addTab(self._dv_tab(), "数据验证")
        tabs.addTab(self._note_tab(), "批注")

        bb = QDialogButtonBox(QDialogButtonBox.Close)
        bb.rejected.connect(self.reject)
        root.addWidget(bb)

    # ------------------------------------------------------------------
    # 数据验证
    # ------------------------------------------------------------------
    def _dv_tab(self):
        from PySide6.QtWidgets import QWidget
        w = QWidget()
        v = QVBoxLayout(w)

        v.addWidget(QLabel("已有规则："))
        self.dv_list = QListWidget()
        v.addWidget(self.dv_list, 1)

        row = QHBoxLayout()
        b1 = QPushButton("删除选中")
        b1.clicked.connect(self._dv_del)
        b2 = QPushButton("全部清除")
        b2.clicked.connect(self._dv_clear)
        row.addWidget(b1)
        row.addWidget(b2)
        row.addStretch(1)
        v.addLayout(row)

        gb = QGroupBox("新建验证规则")
        f = QFormLayout(gb)
        c0, r0, c1, r1 = self.sel
        f.addRow("应用区域：", QLabel("%s:%s" % (addr(c0, r0), addr(c1, r1))))

        self.cb_type = QComboBox()
        self.cb_type.addItems(["整数", "小数", "序列(下拉)", "日期",
                               "时间", "文本长度", "自定义公式"])
        self.cb_type.currentIndexChanged.connect(self._dv_sync)
        f.addRow("允许：", self.cb_type)

        self.cb_op = QComboBox()
        self.cb_op.addItems(["介于", "不介于", "等于", "不等于",
                             "大于", "小于", "大于等于", "小于等于"])
        f.addRow("条件：", self.cb_op)

        self.ed_f1 = QLineEdit()
        self.ed_f1.setPlaceholderText("最小值 / 序列内容（逗号分隔）/ 公式")
        f.addRow("值1：", self.ed_f1)

        self.ed_f2 = QLineEdit()
        self.ed_f2.setPlaceholderText("最大值（介于/不介于 时用）")
        f.addRow("值2：", self.ed_f2)

        self.cb_err = QComboBox()
        self.cb_err.addItems(["拒绝输入", "警告（可继续）", "仅提示"])
        f.addRow("非法时：", self.cb_err)

        self.ed_errmsg = QLineEdit()
        self.ed_errmsg.setPlaceholderText("出错提示文字")
        f.addRow("提示语：", self.ed_errmsg)

        self.chk_blank = QCheckBox("允许空值")
        self.chk_blank.setChecked(True)
        f.addRow("", self.chk_blank)

        self.dv_hint = QLabel("")
        self.dv_hint.setWordWrap(True)
        self.dv_hint.setStyleSheet("color:#666")
        f.addRow("", self.dv_hint)

        ab = QHBoxLayout()
        ba = QPushButton("添加规则")
        ba.clicked.connect(self._dv_add)
        ab.addStretch(1)
        ab.addWidget(ba)
        f.addRow("", ab)
        v.addWidget(gb)

        self._dv_sync(0)
        self._dv_refresh()
        return w

    def _dv_sync(self, i):
        # 序列（下拉）不需要 op 和第二阈值；自定义公式也只有一个公式
        is_list = (i == 2)
        is_custom = (i == 6)
        self.cb_op.setVisible(not (is_list or is_custom))
        self.ed_f2.setVisible(not (is_list or is_custom))
        self.ed_f1.setPlaceholderText(
            "候选项，逗号分隔，如 是,否,也许" if is_list else
            "公式，如 A1>0" if is_custom else "最小值")
        hints = {
            0: "必须是整数。", 1: "可以是小数。",
            2: "下拉选择。候选项用英文逗号分隔。",
            3: "日期。注意：界面不解析日期输入，实际按序列号比较。",
            4: "时间。同样按数值比较。",
            5: "按码点数计算长度（中文算 1，不是 3 字节）。",
            6: "公式按区域左上角书写，判定第 k 格时相对引用会平移。",
        }
        self.dv_hint.setText(hints.get(i, ""))

    def _dv_refresh(self):
        self.dv_list.clear()
        for i, d in enumerate(self.sheet.dv_list()):
            r = d["rect"]
            where = "%s:%s" % (addr(r[0], r[1]), addr(r[2], r[3]))
            t = self.sheet.DV_TYPES[d["type"]] if d["type"] < len(self.sheet.DV_TYPES) else "?"
            op = self.sheet.DV_OPS[d["op"]] if d["op"] < len(self.sheet.DV_OPS) else ""
            cond = ("%s %s" % (op, d["f1"])).strip()
            if d["f2"]:
                cond += " 且 %s" % d["f2"]
            self.dv_list.addItem("%d. [%s] %s → %s" % (i + 1, where, t, cond))

    def _dv_add(self):
        # 界面顺序 -> 引擎枚举值（1=整数 … 7=自定义）
        t = self.cb_type.currentIndex() + 1
        op_map = {0: 1, 1: 2, 2: 3, 3: 4, 4: 5, 5: 6, 6: 7, 7: 8}
        op = op_map.get(self.cb_op.currentIndex(), 1)
        f1 = self.ed_f1.text().strip()
        if not f1:
            QMessageBox.warning(self, "缺少值", "请填写验证条件。")
            return
        c0, r0, c1, r1 = self.sel
        ok = self.sheet.add_dv(c0, r0, c1, r1, t, op, f1,
                               self.ed_f2.text().strip(),
                               self.cb_err.currentIndex(),
                               "输入无效", self.ed_errmsg.text().strip(),
                               self.chk_blank.isChecked(), True)
        if not ok:
            QMessageBox.critical(self, "添加失败", self.sheet._wb.last_error)
            return
        self._dv_refresh()

    def _dv_del(self):
        row = self.dv_list.currentRow()
        if row < 0:
            return
        dvs = self.sheet.dv_list()
        if row >= len(dvs):
            return
        keep = [d for i, d in enumerate(dvs) if i != row]
        self.sheet.clear_dv()
        for d in keep:
            r = d["rect"]
            self.sheet.add_dv(r[0], r[1], r[2], r[3], d["type"], d["op"],
                              d["f1"], d["f2"], d["err_style"],
                              d["err_title"], d["err"],
                              True, True)
        self._dv_refresh()

    def _dv_clear(self):
        if QMessageBox.question(self, "确认", "清除本表全部数据验证规则？") != QMessageBox.Yes:
            return
        self.sheet.clear_dv()
        self._dv_refresh()

    # ------------------------------------------------------------------
    # 批注
    # ------------------------------------------------------------------
    def _note_tab(self):
        from PySide6.QtWidgets import QWidget
        w = QWidget()
        v = QVBoxLayout(w)

        v.addWidget(QLabel("已有批注："))
        self.note_list = QListWidget()
        self.note_list.currentRowChanged.connect(self._note_pick)
        v.addWidget(self.note_list, 1)

        row = QHBoxLayout()
        b1 = QPushButton("删除选中")
        b1.clicked.connect(self._note_del)
        b2 = QPushButton("全部清除")
        b2.clicked.connect(self._note_clear)
        row.addWidget(b1)
        row.addWidget(b2)
        row.addStretch(1)
        v.addLayout(row)

        gb = QGroupBox("添加批注")
        f = QFormLayout(gb)
        c0, r0, _, _ = self.sel
        self.ed_note_cell = QLineEdit(addr(c0, r0))
        self.ed_note_cell.setPlaceholderText("如 B3")
        f.addRow("单元格：", self.ed_note_cell)

        self.ed_author = QLineEdit("xlengine")
        f.addRow("作者：", self.ed_author)

        self.ed_text = QTextEdit()
        self.ed_text.setPlaceholderText("批注内容，支持多行")
        self.ed_text.setFixedHeight(80)
        f.addRow("内容：", self.ed_text)

        ab = QHBoxLayout()
        ba = QPushButton("添加批注")
        ba.clicked.connect(self._note_add)
        ab.addStretch(1)
        ab.addWidget(ba)
        f.addRow("", ab)
        v.addWidget(gb)

        self.lbl_note_hint = QLabel(
            "注意：Excel 里批注的红色三角靠 VML 绘制，本引擎不生成 VML，"
            "所以数据在（openpyxl 可读），但 Excel 里要手动「显示批注」才看得到。")
        self.lbl_note_hint.setWordWrap(True)
        self.lbl_note_hint.setStyleSheet("color:#666")
        v.addWidget(self.lbl_note_hint)

        self._note_refresh()
        return w

    def _note_refresh(self):
        self.note_list.clear()
        for n in self.sheet.note_list():
            t = n["text"].replace("\n", " ⏎ ")
            if len(t) > 40:
                t = t[:40] + "…"
            self.note_list.addItem("%s  [%s] %s" % (addr(n["col"], n["row"]),
                                                    n["author"], t))

    def _note_pick(self, row):
        notes = self.sheet.note_list()
        if 0 <= row < len(notes):
            n = notes[row]
            self.ed_note_cell.setText(addr(n["col"], n["row"]))
            self.ed_author.setText(n["author"])
            self.ed_text.setPlainText(n["text"])

    def _note_add(self):
        from .grid import parse_addr
        a = parse_addr(self.ed_note_cell.text().strip())
        if not a:
            QMessageBox.warning(self, "地址无效",
                                "请填写合法单元格地址，如 B3。")
            return
        text = self.ed_text.toPlainText().strip()
        if not text:
            QMessageBox.warning(self, "缺少内容", "请填写批注内容。")
            return
        # 同格重复添加会叠加两条，先删旧的
        self.sheet.remove_note(a[0], a[1])
        if not self.sheet.add_note(a[0], a[1], text,
                                   self.ed_author.text().strip() or "xlengine"):
            QMessageBox.critical(self, "添加失败", self.sheet._wb.last_error)
            return
        self._note_refresh()

    def _note_del(self):
        row = self.note_list.currentRow()
        notes = self.sheet.note_list()
        if 0 <= row < len(notes):
            n = notes[row]
            self.sheet.remove_note(n["col"], n["row"])
            self._note_refresh()

    def _note_clear(self):
        if QMessageBox.question(self, "确认", "清除本表全部批注？") != QMessageBox.Yes:
            return
        self.sheet.clear_notes()
        self._note_refresh()
