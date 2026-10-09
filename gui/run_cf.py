"""验证条件格式管理器：添加、列出、删除、清除，以及存盘后 openpyxl 能读到。"""
import os, sys
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, "/data/workspace/xlengine")
from PySide6.QtWidgets import QApplication
from gui.app import MainWindow

R = "/data/workspace/cf_result.txt"
L = []
def log(x): L.append(str(x))

app = QApplication(sys.argv)
w = MainWindow()
sh = w.grid.sheet
for i, v in enumerate([85, 92, 78, 65, 88, 71]):
    sh.set(0, i, str(v))
w.wb.recalc()

log("初始规则数=%d" % sh.cf_count())

# 添加：大于 80 标红
sh.add_cf(0, 0, 0, 5, ctype=0, op=5, f1="80", fill="FFC7CE")
# 添加：自定义公式
sh.add_cf(0, 0, 0, 5, ctype=1, f1="A1<70", fill="FFFF00", bold=True)
log("添加后规则数=%d" % sh.cf_count())

for c in sh.cf_list():
    log("  规则 rect=%s type=%d op=%d f1=%r fill=%r" %
        (c["rect"], c["type"], c["op"], c["f1"], c["fill"]))

# 存盘验证
w.wb.save("/data/workspace/xlengine/out/条件格式管理器.xlsx")
log("保存: %s" % (w.wb.last_error or "OK"))

# 删除第 1 条
cfs = sh.cf_list()
keep = [c for i, c in enumerate(cfs) if i != 0]
sh.clear_cf()
for c in keep:
    r = c["rect"]
    sh.add_cf(r[0], r[1], r[2], r[3], c["type"], c["op"], c["f1"], c["f2"],
              c["fill"], c["font"], False, False, False, False, 10, True)
log("删除第1条后规则数=%d (应为1)" % sh.cf_count())
for c in sh.cf_list():
    log("  剩余 f1=%r fill=%r" % (c["f1"], c["fill"]))

sh.clear_cf()
log("全部清除后=%d (应为0)" % sh.cf_count())

with open(R, "w") as f:
    f.write("\n".join(L) + "\n")
