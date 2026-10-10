"""二维码对话框 —— 输入内容，生成后作为浮动图片插入。"""

from PySide6.QtWidgets import (
    QComboBox, QDialog, QDialogButtonBox, QFormLayout, QLabel,
    QLineEdit, QMessageBox, QSpinBox, QVBoxLayout,
)

from gui.qrcode import available, make_png


def _col_name(c):
    s = ""
    c += 1
    while c > 0:
        c, m = divmod(c - 1, 26)
        s = chr(65 + m) + s
    return s


def _addr(c, r):
    return "%s%d" % (_col_name(c), r + 1)


class QRDialog(QDialog):
    def __init__(self, win, parent=None):
        super().__init__(parent or win)
        self.win = win
        self.setWindowTitle("插入二维码")
        self.resize(420, 220)
        self._build()

    def _build(self):
        lay = QVBoxLayout(self)
        form = QFormLayout()

        self.e_text = QLineEdit()
        self.e_text.setPlaceholderText("网址 / 文本 / 任意内容")

        self.sp_span = QSpinBox()
        self.sp_span.setRange(1, 12)
        # 默认跨 3 列 —— 二维码是正方形，而单元格是扁的，
        # 跨列数要多于跨行数才接近正方
        self.sp_span.setValue(3)
        self.sp_span.setSuffix(" 列")

        self.sp_rows = QSpinBox()
        self.sp_rows.setRange(1, 20)
        self.sp_rows.setValue(6)
        self.sp_rows.setSuffix(" 行")

        self.cb_err = QComboBox()
        self.cb_err.addItem("中 (M)", "m")
        self.cb_err.addItem("低 (L)", "l")
        self.cb_err.addItem("高 (Q)", "q")
        self.cb_err.addItem("最高 (H)", "h")

        form.addRow("内容:", self.e_text)
        form.addRow("宽度:", self.sp_span)
        form.addRow("高度:", self.sp_rows)
        form.addRow("纠错等级:", self.cb_err)
        lay.addLayout(form)

        self.tip = QLabel("图片将插入到当前单元格位置")
        lay.addWidget(self.tip)

        if not available():
            self.tip.setText(
                "未安装 segno 库，二维码功能不可用。\n"
                "安装: pip install segno")
            self.e_text.setEnabled(False)

        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.button(QDialogButtonBox.Ok).setText("插入")
        bb.accepted.connect(self._ok)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)

    def _ok(self):
        if not available():
            QMessageBox.warning(self, "二维码", "缺少 segno 库")
            return
        text = self.e_text.text().strip()
        if not text:
            QMessageBox.warning(self, "二维码", "请先输入内容")
            return

        data, err = make_png(text)
        if not data:
            QMessageBox.warning(self, "二维码", "生成失败: %s" % err)
            return

        win = self.win
        c, r = win.grid.cursor_col, win.grid.cursor_row
        w = self.sp_span.value() - 1
        h = self.sp_rows.value() - 1
        try:
            rc = win.wb.add_image(win.grid.sheet.index(),
                                  c, r, c + w, r + h, data,
                                  "png", "二维码")
        except Exception as e:
            QMessageBox.warning(self, "二维码", "插入失败: %s" % e)
            return

        if rc != 0:
            QMessageBox.warning(self, "二维码",
                                "插入失败: %s" % win.wb.last_error)
            return

        win.grid.viewport().update()
        win.dirty = True
        win._update_title()
        win.status_label.setText(
            "已插入二维码（%d 字节，锚点 %s）" % (len(data), _addr(c, r)))
        self.accept()


def open_qr_dialog(win):
    dlg = QRDialog(win)
    dlg.exec()
    return dlg
