"""验证撤销 / 重做真的能还原（含公式不被降级成常量）。"""
import os, sys
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, "/data/workspace/xlengine")
from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication
from gui.app import MainWindow

R = "/data/workspace/undo_result.txt"
L = []
def log(x): L.append(str(x))

app = QApplication(sys.argv)
w = MainWindow()
w.resize(1000, 650)
sh = w.grid.sheet

# 场景1：格子输入
sh.set(0, 0, "=1+2"); sh.set(0, 1, "10")
w.wb.recalc()
log("初始 A1=%s (公式 %s)" % (sh.display(0,0), sh.raw(0,0)))

w.grid.set_cursor(0, 0)
w.grid.edit_current("999")
w.grid._close_editor(save=True)
w.wb.recalc()
log("改成999后 A1=%s | 能否撤销=%s" % (sh.display(0,0), w.undo.can_undo()))

w._do_undo(); w.wb.recalc()
log("撤销后 A1=%s | raw=%s | 公式是否活着=%s"
    % (sh.display(0,0), sh.raw(0,0), sh.raw(0,0).startswith("=")))

w._do_redo(); w.wb.recalc()
log("重做后 A1=%s" % sh.display(0,0))
w._do_undo(); w.wb.recalc()

# 场景2：批量格式操作的撤销
w.grid.set_cursor(0, 0, )
w.grid.set_cursor(1, 0, extend=True)
w._apply_to_selection(lambda s_, c, r: s_.style(c, r, bold=True), "加粗")
log("加粗后 B1 bold=%s | 撤销栈=%d" % (sh.get_style(1,0).get("bold"), len(w.undo._undo)))
w._do_undo()
log("撤销加粗后 B1 bold=%s" % sh.get_style(1,0).get("bold"))

# 场景3：填充的撤销
sh.set(0, 5, "=A1*2")
w.wb.recalc()
w.grid.set_cursor(0, 5)
w.grid._do_fill(0, 5, 0, 7)
w.wb.recalc()
log("填充后 A6=%s A8=%s" % (sh.display(0,5), sh.display(0,7)))
w._do_undo(); w.wb.recalc()
log("撤销填充后 A7=%s (应为空) A8=%s (应为空)"
    % (repr(sh.display(0,6)), repr(sh.display(0,7))))

with open(R, "w") as f:
    f.write("\n".join(L) + "\n")
