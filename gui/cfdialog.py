"""
条件格式规则管理器。

之前只能在终端里用 `:cf >3 fill=FF0000` 设，界面上看不到已有哪些规则、
也没法删 —— 规则一旦设错就只能重开文件。

设计要点：
  - 规则**优先级**是 Excel 语义：数字越小越优先，且命中 stopIfTrue 后
    不再看后续规则。界面按列表顺序展示即优先级顺序。
  - 判定公式按"区域左上角"书写（与引擎的 cf 求值一致）。
    界面必须提示这一点，否则用户写 A2>25 会得到错位的结果。
"""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QColorDialog, QComboBox, QDialog,
                               QDialogButtonBox, QFormLayout, QGroupBox,
                               QHBoxLayout, QLabel, QLineEdit, QListWidget,
                               QMessageBox, QPushButton, QSpinBox, QVBoxLayout)

from .grid import addr


class CfDialog(QDialog):
    def __init__(self, sheet, sel, parent=None):
        super().__init__(parent)
        self.sheet = sheet
        self.sel = sel          # (c0, r0, c1, r1)
        self.setWindowTitle("条件格式规则")
        self.resize(620, 480)

        root = QVBoxLayout(self)

        # ---- 已有规则 ----
        root.addWidget(QLabel("已有规则（自上而下 = 优先级由高到低）："))
        self.list = QListWidget()
        self.list.currentRowChanged.connect(self._on_pick)
        root.addWidget(self.list, 1)

        row = QHBoxLayout()
        self.btn_del = QPushButton("删除选中")
        self.btn_del.clicked.connect(self._del)
        self.btn_clear = QPushButton("全部清除")
        self.btn_clear.clicked.connect(self._clear)
        row.addWidget(self.btn_del)
        row.addWidget(self.btn_clear)
        row.addStretch(1)
        root.addLayout(row)

        # ---- 新建规则 ----
        gb = QGroupBox("新建规则")
        f = QFormLayout(gb)

        c0, r0, c1, r1 = sel
        self.lbl_range = QLabel("%s:%s" % (addr(c0, r0), addr(c1, r1)))
        f.addRow("应用区域：", self.lbl_range)

        self.cb_type = QComboBox()
        self.cb_type.addItems(["单元格值", "自定义公式", "前/后 N 项",
                               "高于/低于均值", "重复值", "唯一值"])
        self.cb_type.currentIndexChanged.connect(self._sync_type)
        f.addRow("规则类型：", self.cb_type)

        self.cb_op = QComboBox()
        self.cb_op.addItems(["大于", "小于", "大于等于", "小于等于",
                             "等于", "不等于", "介于", "不介于"])
        self.cb_op.setToolTip("与阈值比较的方式")
        f.addRow("条件：", self.cb_op)

        self.ed_f1 = QLineEdit()
        self.ed_f1.setPlaceholderText("如 25，或 0.6")
        f.addRow("阈值：", self.ed_f1)

        self.ed_f2 = QLineEdit()
        self.ed_f2.setPlaceholderText("介于/不介于 时的第二个阈值")
        f.addRow("阈值2：", self.ed_f2)

        self.ed_formula = QLineEdit()
        self.ed_formula.setPlaceholderText("如 A1>25（按区域左上角书写）")
        f.addRow("公式：", self.ed_formula)

        self.sp_rank = QSpinBox()
        self.sp_rank.setRange(1, 1000)
        self.sp_rank.setValue(10)
        f.addRow("N：", self.sp_rank)

        self.chk_bottom = QCheckBox("取最小的 N 个（否则取最大）")
        self.chk_above = QCheckBox("高于均值（否则低于）")
        self.chk_above.setChecked(True)
        f.addRow("", self.chk_bottom)
        f.addRow("", self.chk_above)

        # 样式
        sc = QHBoxLayout()
        self.ed_fill = QLineEdit()
        self.ed_fill.setPlaceholderText("填充色 RRGGBB，如 FFFF00")
        btn_fill = QPushButton("选色")
        btn_fill.clicked.connect(self._pick_fill)
        sc.addWidget(self.ed_fill)
        sc.addWidget(btn_fill)
        f.addRow("命中样式：", sc)

        self.chk_bold = QCheckBox("加粗")
        self.chk_italic = QCheckBox("斜体")
        hb = QHBoxLayout()
        hb.addWidget(self.chk_bold)
        hb.addWidget(self.chk_italic)
        hb.addStretch(1)
        f.addRow("", hb)

        self.lbl_hint = QLabel("")
        self.lbl_hint.setWordWrap(True)
        self.lbl_hint.setStyleSheet("color:#666")
        f.addRow("", self.lbl_hint)

        root.addWidget(gb)

        bb = QDialogButtonBox(QDialogButtonBox.Close)
        btn_add = bb.addButton("添加规则", QDialogButtonBox.ApplyRole)
        btn_add.clicked.connect(self._add)
        bb.rejected.connect(self.reject)
        root.addWidget(bb)

        self._sync_type(0)
        self._refresh()

    # ------------------------------------------------------------------
    def _sync_type(self, i):
        """按类型显示/隐藏相关控件，避免用户填了用不上的字段。"""
        is_cellis = (i == 0)
        is_expr = (i == 1)
        is_top = (i == 2)
        is_avg = (i == 3)
        for w, show in ((self.ed_f1, is_cellis), (self.ed_f2, is_cellis),
                        (self.ed_formula, is_expr), (self.sp_rank, is_top),
                        (self.chk_bottom, is_top), (self.chk_above, is_avg),
                        (self.cb_op, is_cellis)):
            w.setVisible(show)
        hints = {
            0: "阈值与单元格的值比较。",
            1: "公式按**区域左上角**书写：区域为 B1:B5 时写 A1>25，"
               "对 B4 求值的语义等价于 A4>25。",
            2: "N 表示取前 N 项（或前 N%）。",
            3: "高于/低于该区域的平均值。",
            4: "区域内出现次数 >1 的单元格。",
            5: "区域内只出现一次的单元格。",
        }
        self.lbl_hint.setText(hints.get(i, ""))

    def _pick_fill(self):
        col = QColorDialog.getColor(QColor("#ffff00"), self, "命中时的填充色")
        if col.isValid():
            self.ed_fill.setText("%02X%02X%02X" % (col.red(), col.green(), col.blue()))

    # ------------------------------------------------------------------
    def _refresh(self):
        self.list.clear()
        for i, cf in enumerate(self.sheet.cf_list()):
            r = cf["rect"]
            where = "%s:%s" % (addr(r[0], r[1]), addr(r[2], r[3]))
            tname = ["单元格值", "公式", "前/后N", "均值", "重复", "唯一"][cf["type"]]
            ops = ["", "介于", "不介于", "等于", "不等于",
                   "大于", "小于", "≥", "≤"]
            if cf["type"] == 0:
                cond = "%s %s" % (ops[cf["op"]] if cf["op"] < len(ops) else "",
                                  cf["f1"] + (" 且 " + cf["f2"] if cf["f2"] else ""))
            elif cf["type"] == 1:
                cond = cf["f1"]
            elif cf["type"] == 2:
                cond = "%s%d 项" % ("后" if cf["flags"][:1] == "1" else "前",
                                    int(cf["f1"]) if cf["f1"].isdigit() else 10)
            else:
                cond = ""
            style = []
            if cf["fill"]:
                style.append("填充#" + cf["fill"])
            if cf["flags"] and len(cf["flags"]) >= 2:
                if cf["flags"][0] == "1":
                    style.append("粗")
                if cf["flags"][1] == "1":
                    style.append("斜")
            self.list.addItem("%d. [%s] %s → %s  %s"
                              % (i + 1, where, tname, cond, " ".join(style)))

    def _on_pick(self, row):
        self.btn_del.setEnabled(row >= 0)

    # ------------------------------------------------------------------
    def _add(self):
        t = self.cb_type.currentIndex()
        c0, r0, c1, r1 = self.sel
        op_map = {0: 5, 1: 6, 2: 7, 3: 8, 4: 3, 5: 4, 6: 1, 7: 2}
        op = op_map.get(self.cb_op.currentIndex(), 5)
        f1 = self.ed_f1.text().strip() if t == 0 else self.ed_formula.text().strip()
        f2 = self.ed_f2.text().strip() if t == 0 else ""
        fill = self.ed_fill.text().strip()

        # 不给样式的话规则"生效了也看不出来"，给个默认红色填充
        if not fill and not self.chk_bold.isChecked():
            fill = "FFC7CE"

        if t == 1 and not f1:
            QMessageBox.warning(self, "缺少公式", "自定义公式规则必须填公式。")
            return
        if t == 0 and not f1:
            QMessageBox.warning(self, "缺少阈值", "请填写比较的阈值。")
            return

        ok = self.sheet.add_cf(c0, r0, c1, r1, t, op, f1, f2, fill, "",
                               self.chk_bold.isChecked(), self.chk_italic.isChecked(),
                               self.chk_bottom.isChecked(), False,
                               self.sp_rank.value(), self.chk_above.isChecked())
        if not ok:
            QMessageBox.critical(self, "添加失败", self.sheet._wb.last_error)
            return
        self._refresh()

    def _del(self):
        row = self.list.currentRow()
        if row < 0:
            return
        # 引擎只提供整体清除，删单条靠"清掉再重建其余"。
        # 这是最小改动路径：重建时按原参数重新 add。
        cfs = self.sheet.cf_list()
        if row >= len(cfs):
            return
        keep = [c for i, c in enumerate(cfs) if i != row]
        self.sheet.clear_cf()
        for c in keep:
            r = c["rect"]
            flags = c["flags"]
            self.sheet.add_cf(r[0], r[1], r[2], r[3], c["type"], c["op"],
                              c["f1"], c["f2"], c["fill"], c["font"],
                              flags[:1] == "1", flags[1:2] == "1",
                              c["type"] == 2 and c.get("f1", "").isdigit(),
                              False, 10, True)
        self._refresh()

    def _clear(self):
        if QMessageBox.question(self, "确认", "清除本表全部条件格式规则？") != QMessageBox.Yes:
            return
        self.sheet.clear_cf()
        self._refresh()
