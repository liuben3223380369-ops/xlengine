"""验证冻结窗格与筛选箭头真的画出来了。"""
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, "/data/workspace/xlengine")

RESULT = "/data/workspace/freeze_result.txt"

from PySide6.QtCore import QTimer          # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

from gui.app import MainWindow  # noqa: E402

lines = []


def log(s):
    lines.append(str(s))


app = QApplication(sys.argv)
w = MainWindow()
w.resize(1100, 700)
w._fill_demo()
w.grid.set_freeze(1, 1)          # 冻结首列 + 首行
w.grid.set_filter(0, 0, 6, 6)    # A1:G7 自动筛选
w.show()


def shot():
    try:
        w.grab().save("/data/workspace/xlengine/out/gui_冻结.png")
        log("截图: 已保存")
    except Exception as e:
        log("截图失败: %s" % e)

    g = w.grid
    log("冻结设置 cols=%d rows=%d" % (g._frozen_cols, g._frozen_rows))
    log("冻结区尺寸 %dx%d" % (g.frozen_w(), g.frozen_h()))
    log("筛选列数 %d" % len(g._filter_cols))

    # 关键：滚动后冻结列/行的坐标不应变化
    x1 = g.x_of_col(0)
    y1 = g.y_of_row(0)
    xd = g.x_of_col(5)   # 非冻结列
    g.horizontalScrollBar().setValue(300)
    g.verticalScrollBar().setValue(400)
    x2 = g.x_of_col(0)
    y2 = g.y_of_row(0)
    xd2 = g.x_of_col(5)

    log("滚动 300px 后 A列 x: %d -> %d" % (x1, x2))
    log("  A列是否不动: %s" % (x1 == x2))
    log("滚动 400px 后 行1 y: %d -> %d" % (y1, y2))
    log("  冻结行是否不动: %s" % (y1 == y2))
    log("非冻结列 F x: %d -> %d (应变化)" % (xd, xd2))
    log("  非冻结列是否滚动: %s" % (xd != xd2))

    with open(RESULT, "w") as f:
        f.write("\n".join(lines) + "\n")
    app.quit()


QTimer.singleShot(700, shot)
app.exec()
