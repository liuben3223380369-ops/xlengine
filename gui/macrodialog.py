"""宏管理器面板 —— 列表 + 代码编辑器 + 输出控制台。"""

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QHBoxLayout, QInputDialog, QLabel,
    QListWidget, QMessageBox, QPushButton, QSplitter, QTextEdit,
    QVBoxLayout, QCheckBox, QWidget, QLineEdit, QFormLayout,
)

from gui.macro import MacroRunner, MacroStore, check_safety


class MacroDialog(QDialog):
    def __init__(self, win, parent=None):
        super().__init__(parent or win)
        self.win = win
        self.store = MacroStore()
        self.macros = self.store.load()
        self.cur = -1
        self._dirty = False

        self.setWindowTitle("宏管理器")
        self.resize(900, 620)
        self._build()

    def _build(self):
        root = QVBoxLayout(self)

        split = QSplitter(Qt.Horizontal)
        root.addWidget(split, 1)

        # ---- 左：列表 ----
        left = QWidget()
        lv = QVBoxLayout(left)
        lv.addWidget(QLabel("宏列表"))
        self.list = QListWidget()
        self.list.currentRowChanged.connect(self._pick)
        lv.addWidget(self.list, 1)

        row = QHBoxLayout()
        b_new = QPushButton("新建")
        b_run = QPushButton("运行")
        b_del = QPushButton("删除")
        b_new.clicked.connect(self._new)
        b_run.clicked.connect(self._run)
        b_del.clicked.connect(self._delete)
        row.addWidget(b_new)
        row.addWidget(b_run)
        row.addWidget(b_del)
        lv.addLayout(row)

        self.chk_autorun = QCheckBox("打开工作簿时自动运行")
        self.chk_autorun.stateChanged.connect(self._toggle_autorun)
        lv.addWidget(self.chk_autorun)
        split.addWidget(left)

        # ---- 右：编辑 + 输出 ----
        right = QWidget()
        rv = QVBoxLayout(right)

        form = QFormLayout()
        self.name_edit = QLineEdit()
        self.name_edit.textChanged.connect(self._mark_dirty)
        form.addRow("名称:", self.name_edit)
        rv.addLayout(form)

        rv.addWidget(QLabel("Python 代码（可用 api 对象操作表格）"))
        self.code = QTextEdit()
        self.code.setFontFamily("Consolas")
        self.code.textChanged.connect(self._mark_dirty)
        rv.addWidget(self.code, 3)

        rv.addWidget(QLabel("输出"))
        self.out = QTextEdit()
        self.out.setReadOnly(True)
        self.out.setFontFamily("Consolas")
        self.out.setMaximumHeight(150)
        rv.addWidget(self.out, 1)

        split.addWidget(right)
        split.setStretchFactor(0, 0)
        split.setStretchFactor(1, 1)

        # ---- 底部 ----
        bb = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Close)
        bb.button(QDialogButtonBox.Save).clicked.connect(self._save)
        bb.button(QDialogButtonBox.Close).clicked.connect(self._close)
        root.addWidget(bb)

        self._refresh()

    # ------------------------------------------------------------------
    def _refresh(self, keep=-1):
        self.list.blockSignals(True)
        self.list.clear()
        for m in self.macros:
            tag = " [自动]" if m.get("autorun") else ""
            self.list.addItem(m["name"] + tag)
        self.list.blockSignals(False)
        if self.macros:
            idx = keep if 0 <= keep < len(self.macros) else 0
            self.list.setCurrentRow(idx)
        else:
            self.cur = -1
            self.name_edit.clear()
            self.code.clear()

    def _pick(self, i):
        if i < 0 or i >= len(self.macros):
            return
        self.cur = i
        m = self.macros[i]
        self.name_edit.blockSignals(True)
        self.name_edit.setText(m.get("name", ""))
        self.name_edit.blockSignals(False)
        self.code.blockSignals(True)
        self.code.setPlainText(m.get("code", ""))
        self.code.blockSignals(False)
        self.chk_autorun.blockSignals(True)
        self.chk_autorun.setChecked(bool(m.get("autorun")))
        self.chk_autorun.blockSignals(False)
        self._dirty = False

    def _mark_dirty(self):
        self._dirty = True

    def _collect(self):
        """把编辑器内容同步回当前宏。不调这个直接保存会丢改动。"""
        if self.cur < 0 or self.cur >= len(self.macros):
            return
        self.macros[self.cur]["name"] = self.name_edit.text().strip() or "未命名"
        self.macros[self.cur]["code"] = self.code.toPlainText()

    def _new(self):
        self._collect()
        self.macros.append({"name": "新宏", "autorun": False, "code": "# 在此编写宏\n"})
        self._refresh(len(self.macros) - 1)
        self.code.setFocus()

    def _delete(self):
        if self.cur < 0:
            return
        if QMessageBox.question(self, "删除", "确定删除「%s」？" %
                                self.macros[self.cur]["name"]) != QMessageBox.Yes:
            return
        del self.macros[self.cur]
        self.cur = -1
        self._refresh()

    def _toggle_autorun(self, st):
        if self.cur >= 0:
            self.macros[self.cur]["autorun"] = bool(st)
            self._dirty = True

    def _run(self):
        self._collect()
        if self.cur < 0:
            return
        m = self.macros[self.cur]
        code = m.get("code", "")

        bad = check_safety(code)
        if bad is not None:
            self.out.setPlainText("安全检查未通过: %s" % bad[0])
            return

        ok, msg = MacroRunner(self.win, self._out).run(code)
        self.out.setPlainText(
            ("[完成]\n" if ok else "[出错]\n") + (msg or "(无输出)"))

    def _out(self, s):
        self.out.append(str(s))

    def _save(self):
        self._collect()
        if not self.store.save(self.macros):
            QMessageBox.warning(self, "保存失败", "无法写入宏文件")
            return
        self._dirty = False
        keep = self.cur
        self._refresh(keep)
        self.out.setPlainText("已保存 %d 个宏" % len(self.macros))

    def _close(self):
        if self._dirty:
            r = QMessageBox.question(
                self, "未保存", "有改动未保存，要保存吗？",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel)
            if r == QMessageBox.Cancel:
                return
            if r == QMessageBox.Yes:
                self._save()
        self.accept()

    # ------------------------------------------------------------------
    def autorun_macros(self):
        return [m for m in self.macros if m.get("autorun")]


def open_macro_dialog(win):
    dlg = MacroDialog(win)
    dlg.exec()
    return dlg
