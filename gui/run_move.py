"""验证拖拽移动选区：公式引用要跟着走，原位置要清空。"""
import os, sys
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, "/data/workspace/xlengine")
from PySide6.QtWidgets import QApplication
from gui.app import MainWindow

R = "/data/workspace/move_result.txt"
L = []
def log(x): L.append(str(x))

app = QApplication(sys.argv)
w = MainWindow()
sh = w.grid.sheet
g = w.grid

# A1=1 A2=2；B1 = A1*10，B2 = A2*10
sh.set(0, 0, "1"); sh.set(0, 1, "2")
sh.set(1, 0, "=A1*10"); sh.set(1, 1, "=A2*10")
w.wb.recalc()
log("移动前: B1=%s B2=%s" % (sh.display(1,0), sh.display(1,1)))
log("  B1 公式=%s" % sh.raw(1,0))

# 把 B1:B2 拖到 D1:D2
g._do_move((1, 0, 1, 1), (3, 0, 3, 1))
w.wb.recalc()
log("移动后 D1=%s D2=%s" % (sh.display(3,0), sh.display(3,1)))
log("  D1 公式=%s (应为 =C1*10，引用跟着平移)" % sh.raw(3,0))
log("  D2 公式=%s" % sh.raw(3,1))
log("原位置 B1=%r B2=%r (应为空)" % (sh.display(1,0), sh.display(1,1)))
log("撤销栈深度=%d" % len(w.undo._undo))

# 撤销后应还原
w._do_undo(); w.wb.recalc()
log("撤销后 B1=%s D1=%r" % (sh.display(1,0), sh.display(3,0)))

with open(R, "w") as f:
    f.write("\n".join(L) + "\n")
