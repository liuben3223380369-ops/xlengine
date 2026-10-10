"""验证数据验证、批注、图表编辑，并用 openpyxl 确认存盘正确。"""
import os, sys
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, "/data/workspace/xlengine")
from PySide6.QtWidgets import QApplication
from gui.app import MainWindow

R = "/data/workspace/dvnote_result.txt"
L = []
def log(x): L.append(str(x))

app = QApplication(sys.argv)
w = MainWindow()
sh = w.grid.sheet
for i, v in enumerate([85, 92, 78]):
    sh.set(0, i, str(v))
w.wb.recalc()

# ---- 数据验证 ----
log("dv 初始=%d" % sh.dv_count())
sh.add_dv(0, 0, 0, 2, dtype=1, op=1, f1="0", f2="100", err_msg="请输入 0-100")
sh.add_dv(1, 0, 1, 2, dtype=3, f1="是,否,也许")
log("dv 添加后=%d" % sh.dv_count())
for d in sh.dv_list():
    log("  [%s] type=%d op=%d f1=%r f2=%r" % (d["rect"], d["type"], d["op"], d["f1"], d["f2"]))

# ---- 批注 ----
sh.add_note(0, 0, "最高分", "张老师")
sh.add_note(1, 1, "需确认\n是否缺考", "李助教")
log("note 添加后=%d" % sh.note_count())
for n in sh.note_list():
    log("  (%d,%d) [%s] %r" % (n["col"], n["row"], n["author"], n["text"]))
sh.remove_note(0, 0)
log("note 删一条后=%d (应为1)" % sh.note_count())

# ---- 图表 ----
sh.set(2, 0, "=A1*2"); sh.set(2, 1, "=A2*2"); sh.set(2, 2, "=A3*2")
w.wb.recalc()
ok = sh.add_chart(0, 0, 0, 2, 2, "成绩图", has_header=0)
log("add_chart: %s | 图表数=%d" % (ok, sh.chart_count()))
for c in sh.chart_list():
    log("  type=%d title=%r anchor=%s 系列=%d" %
        (c["type"], c["title"], c["anchor"], c["nseries"]))

# 改类型与标题
sh.set_chart_type(0, 2)          # 折线
sh.set_chart_title(0, "成绩折线图")
sh.set_chart_anchor(0, 5, 0, 13, 16)
c = sh.chart_list()[0]
log("改后: type=%d(应2) title=%r anchor=%s(应[5,0,13,16])" %
    (c["type"], c["title"], c["anchor"]))

# 存盘验证
w.wb.save("/data/workspace/xlengine/out/验证批注图表.xlsx")
log("保存: %s" % (w.wb.last_error or "OK"))

# 删图表
sh.remove_chart(0)
log("删图表后=%d (应为0)" % sh.chart_count())

with open(R, "w") as f:
    f.write("\n".join(L) + "\n")
