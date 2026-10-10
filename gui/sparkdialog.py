"""迷你图对话框 —— 选数据区域、类型、目标格。"""

from PySide6.QtWidgets import (
    QComboBox, QDialog, QDialogButtonBox, QFormLayout,
    QLabel, QLineEdit, QMessageBox, QVBoxLayout,
)

from gui.sparkline import COLUMN, LINE, TYPES, WINLOSS, collect_values


def _col_name(c):
    s = ""
    c += 1
    while c > 0:
        c, m = divmod(c - 1, 26)
        s = chr(65 + m) + s
    return s


def _addr(c, r):
    return "%s%d" % (_col_name(c), r + 1)


def _a1(c0, r0, c1, r1):
    return "%s:%s" % (_addr(c0, r0), _addr(c1, r1))


class SparkDialog(QDialog):
    def __init__(self, win, parent=None):
        super().__init__(parent or win)
        self.win = win
        self.setWindowTitle("迷你图")
        self.resize(430, 250)
        self._build()

    def _build(self):
        lay = QVBoxLayout(self)
        form = QFormLayout()

        c0, r0, c1, r1 = self.win.grid.sel_rect()
        self.e_range = QLineEdit(_a1(c0, r0, c1, r1))
        self.e_target = QLineEdit(_addr(c1 + 1, r0))

        self.cb_type = QComboBox()
        for k in (LINE, COLUMN, WINLOSS):
            self.cb_type.addItem(TYPES[k], k)

        form.addRow("数据区域:", self.e_range)
        form.addRow("放置到:", self.e_target)
        form.addRow("类型:", self.cb_type)
        lay.addLayout(form)

        self.tip = QLabel("")
        self.tip.setWordWrap(True)
        lay.addWidget(self.tip)

        lay.addWidget(QLabel(
            "说明：迷你图只存在于当前会话，存盘后不保留。"))

        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.button(QDialogButtonBox.Ok).setText("插入")
        bb.accepted.connect(self._ok)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)

    def _parse_range(self, text):
        """解析 A1:B5 形式。返回 (c0,r0,c1,r1) 或 None。"""
        import re
        s = str(text).strip().upper().replace("$", "")
        if not s:
            return None
        parts = s.split(":")
        try:
            if len(parts) == 1:
                c0, r0 = _parse_addr(parts[0])
                return (c0, r0, c0, r0)
            a, b = parts[0], parts[1]
            c0, r0 = _parse_addr(a)
            c1, r1 = _parse_addr(b)
            return (min(c0, c1), min(r0, r1), max(c0, c1), max(r0, r1))
        except Exception:
            return None

    def _ok(self):
        rng = self._parse_range(self.e_range.text())
        tgt = self._parse_range(self.e_target.text())
        if not rng:
            QMessageBox.warning(self, "迷你图", "数据区域格式不对，例如 A1:E1")
            return
        if not tgt:
            QMessageBox.warning(self, "迷你图", "目标格格式不对，例如 F1")
            return

        sh = self.win.grid.sheet
        spec = {"type": self.cb_type.currentData(),
                "c0": rng[0], "r0": rng[1], "c1": rng[2], "r1": rng[3],
                "color": "#4472c4"}
        vals = collect_values(sh, spec)
        if not vals:
            QMessageBox.warning(
                self, "迷你图",
                "数据区域里没有可用数值。\n"
                "迷你图只认数字，文本与空单元格会被跳过。")
            return

        # 目标格用左上角
        self.win.grid.sparklines.set(tgt[0], tgt[1], spec["type"],
                                     spec["c0"], spec["r0"],
                                     spec["c1"], spec["r1"], spec["color"])
        self.win.grid.viewport().update()
        self.win.status_label.setText(
            "已插入迷你图 %s（%d 个数据点）" % (_addr(tgt[0], tgt[1]), len(vals)))
        self.accept()


def _parse_addr(a):
    s = str(a).strip().upper()
    col = 0
    i = 0
    while i < len(s) and s[i].isalpha():
        col = col * 26 + (ord(s[i]) - 64)
        i += 1
    row = int(s[i:]) - 1 if i < len(s) else 0
    return (col - 1, row)


def open_spark_dialog(win):
    dlg = SparkDialog(win)
    dlg.exec()
    return dlg
