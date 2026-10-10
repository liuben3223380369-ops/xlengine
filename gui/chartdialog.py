"""
图表管理器：改已有图表的类型、标题、位置，或删除。

之前只能新建 —— 类型选错了只能删掉重加（而"删掉"当时也没有接口）。

三个必须知道的点：

1. **改标题要同步 chartTitles_。** 存盘用的是那份独立的标题表，
   只改 `Chart::title` 不生效，文件里还是旧标题。

2. **删图表要同步删标题。** 否则后面图表的标题会整体错位——
   删了第 1 张，第 2 张会套用第 1 张的标题。

3. **改数据区域没有直接接口。** 图表的系列是解析数据时按快照存进
   `DataSeries::values` 的，不是活引用，所以改区域只能"删掉重建"。
   openpyxl 也一样：它读回来的是缓存值。
"""
from PySide6.QtWidgets import (QComboBox, QDialog, QDialogButtonBox,
                               QFormLayout, QHBoxLayout, QLabel, QLineEdit,
                               QListWidget, QMessageBox, QPushButton,
                               QSpinBox, QVBoxLayout)

from .grid import addr, col_name


class ChartDialog(QDialog):
    def __init__(self, sheet, parent=None):
        super().__init__(parent)
        self.sheet = sheet
        self.setWindowTitle("图表")
        self.resize(560, 460)

        root = QVBoxLayout(self)
        root.addWidget(QLabel("本表已有图表："))
        self.list = QListWidget()
        self.list.currentRowChanged.connect(self._pick)
        root.addWidget(self.list, 1)

        # ---- 编辑区 ----
        f = QFormLayout()
        self.cb_type = QComboBox()
        self.cb_type.addItems(sheet.CHART_TYPES)
        f.addRow("类型：", self.cb_type)

        self.ed_title = QLineEdit()
        f.addRow("标题：", self.ed_title)

        pos = QHBoxLayout()
        self.sp_c0 = QSpinBox(); self.sp_c0.setRange(0, 16383)
        self.sp_r0 = QSpinBox(); self.sp_r0.setRange(0, 1048575)
        self.sp_c1 = QSpinBox(); self.sp_c1.setRange(0, 16383)
        self.sp_r1 = QSpinBox(); self.sp_r1.setRange(0, 1048575)
        for sp in (self.sp_c0, self.sp_r0, self.sp_c1, self.sp_r1):
            sp.setFixedWidth(70)
        pos.addWidget(QLabel("从"))
        pos.addWidget(self.sp_c0)
        pos.addWidget(self.sp_r0)
        pos.addWidget(QLabel("到"))
        pos.addWidget(self.sp_c1)
        pos.addWidget(self.sp_r1)
        pos.addStretch(1)
        f.addRow("位置：", pos)

        self.lbl_pos = QLabel("")
        self.lbl_pos.setStyleSheet("color:#666")
        f.addRow("", self.lbl_pos)

        root.addLayout(f)

        # ---- 按钮 ----
        row = QHBoxLayout()
        b_apply = QPushButton("应用修改")
        b_apply.clicked.connect(self._apply)
        b_del = QPushButton("删除图表")
        b_del.clicked.connect(self._delete)
        row.addWidget(b_apply)
        row.addWidget(b_del)
        row.addStretch(1)
        root.addLayout(row)

        self.lbl_hint = QLabel(
            "提示：图表数据是添加时的快照，不是活引用 —— "
            "改数据区域请用「删除图表」后重新插入。")
        self.lbl_hint.setWordWrap(True)
        self.lbl_hint.setStyleSheet("color:#666")
        root.addWidget(self.lbl_hint)

        bb = QDialogButtonBox(QDialogButtonBox.Close)
        bb.rejected.connect(self.reject)
        root.addWidget(bb)

        self._refresh()

    # ------------------------------------------------------------------
    def _refresh(self):
        self.list.clear()
        for i, c in enumerate(self.sheet.chart_list()):
            tn = (self.sheet.CHART_TYPES[c["type"]]
                  if c["type"] < len(self.sheet.CHART_TYPES) else "?")
            a = c["anchor"]
            self.list.addItem(
                "%d. %s「%s」 %s:%s  %d 系列 × %d 点"
                % (i + 1, tn, c["title"] or "(无标题)",
                   addr(a[0], a[1]), addr(a[2], a[3]),
                   c["nseries"], c["npoints"]))

    def _pick(self, row):
        charts = self.sheet.chart_list()
        if not (0 <= row < len(charts)):
            return
        c = charts[row]
        self.cb_type.setCurrentIndex(c["type"])
        self.ed_title.setText(c["title"])
        a = c["anchor"]
        self.sp_c0.setValue(a[0]); self.sp_r0.setValue(a[1])
        self.sp_c1.setValue(a[2]); self.sp_r1.setValue(a[3])
        self._sync_pos_label()

    def _sync_pos_label(self):
        self.lbl_pos.setText("锚点 %s%d : %s%d"
                             % (col_name(self.sp_c0.value()), self.sp_r0.value() + 1,
                                col_name(self.sp_c1.value()), self.sp_r1.value() + 1))

    # ------------------------------------------------------------------
    def _apply(self):
        i = self.list.currentRow()
        if i < 0:
            QMessageBox.information(self, "未选中", "请先在上方选择一个图表。")
            return
        self.sheet.set_chart_type(i, self.cb_type.currentIndex())
        self.sheet.set_chart_title(i, self.ed_title.text().strip())
        self.sheet.set_chart_anchor(i, self.sp_c0.value(), self.sp_r0.value(),
                                    self.sp_c1.value(), self.sp_r1.value())
        self._refresh()
        self.list.setCurrentRow(i)

    def _delete(self):
        i = self.list.currentRow()
        if i < 0:
            QMessageBox.information(self, "未选中", "请先在上方选择一个图表。")
            return
        if QMessageBox.question(self, "确认", "删除选中的图表？") != QMessageBox.Yes:
            return
        self.sheet.remove_chart(i)
        self._refresh()
