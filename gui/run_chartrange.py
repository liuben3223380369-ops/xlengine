"""验证图表数据区域可改：区域改了系列要真的跟着变。"""
import os, sys
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, "/data/workspace/xlengine")
from PySide6.QtWidgets import QApplication
from gui.app import MainWindow

R = "/data/workspace/chartrange_result.txt"
L = []
def log(x): L.append(str(x))

app = QApplication(sys.argv)
w = MainWindow()
sh = w.grid.sheet
# A列分类，B/C 两列数据，另有 D 列更多数据
for r, (a, b, c, d) in enumerate([("一月", 10, 20, 99),
                                   ("二月", 30, 40, 98),
                                   ("三月", 50, 60, 97),
                                   ("四月", 70, 80, 96)]):
    sh.set(0, r, a); sh.set(1, r, str(b)); sh.set(2, r, str(c)); sh.set(3, r, str(d))
w.wb.recalc()

# 用 A1:C4 建图（A分类，B/C 两个系列）
sh.add_chart(0, 0, 0, 2, 3, "初始", has_header=1)
c = sh.chart_list()[0]
log("初始: 系列=%d 点=%d 区域=%s" % (c["nseries"], c["npoints"], c["source"]["rect"]))
log("  系列名=%s" % c["series"])

# 改成只取 B 列：A1:B4
ok = sh.set_chart_range(0, 0, 0, 1, 3, has_header=True, cat_from_first_col=True)
log("改区域 A1:B4: %s | err=%s" % (ok, sh._wb.last_error))
c = sh.chart_list()[0]
log("  系列=%d (应1) 点=%d (应3) 区域=%s" % (c["nseries"], c["npoints"], c["source"]["rect"]))
log("  系列名=%s (应只剩一个)" % c["series"])

# 改成 A1:D4 三个系列
ok = sh.set_chart_range(0, 0, 0, 3, 3, has_header=True, cat_from_first_col=True)
log("改区域 A1:D4: %s" % ok)
c = sh.chart_list()[0]
log("  系列=%d (应3) 点=%d (应3)" % (c["nseries"], c["npoints"]))

# 按行做系列
ok = sh.set_chart_range(0, 0, 0, 2, 3, has_header=True, cat_from_first_col=False)
log("改成按行: %s" % ok)
c = sh.chart_list()[0]
log("  系列=%d (应3=三行) 系列名=%s" % (c["nseries"], c["series"]))

# 非法区域要回滚，不能把图表弄成半成品
before = sh.chart_list()[0]
ok = sh.set_chart_range(0, 0, 5, 0, 2, has_header=False)
log("缩到单列(无数据列): %s (应False) err=%r" % (ok, sh._wb.last_error))
after = sh.chart_list()[0]
log("  回滚检查: 系列=%d (应与改前 %d 相同)" % (after["nseries"], before["nseries"]))

# 刷新：改单元格数值后
sh.set(1, 1, "999")
w.wb.recalc()
log("改 B2=999 后刷新: %d 个图表" % sh.refresh_charts())

w.wb.save("/data/workspace/xlengine/out/图表改区域.xlsx")
log("保存: %s" % (w.wb.last_error or "OK"))

with open(R, "w") as f:
    f.write("\n".join(L) + "\n")
