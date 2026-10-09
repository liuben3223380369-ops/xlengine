"""离屏运行图形界面并截图，同时打印内部状态确认真的работа。"""
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, "/data/workspace/xlengine")

from PySide6.QtCore import QTimer          # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

from gui.app import MainWindow  # noqa: E402

app = QApplication(sys.argv)
w = MainWindow()
w.resize(1200, 760)
w._fill_demo()
w.show()


def shot():
    try:
        w.grab().save("/data/workspace/xlengine/out/gui_main.png")
        print("截图: 已保存")
    except Exception as e:
        print("截图失败:", e)

    sh = w.grid.sheet
    print("A1 =", repr(sh.display(0, 0)))
    print("B2 =", repr(sh.display(1, 1)))
    print("F2 =", repr(sh.display(5, 1)), " (应为合计)")
    print("G2 =", repr(sh.display(6, 1)), " (应为占比)")
    print("G2 原始 =", repr(sh.raw(6, 1)))
    print("F7 合计行 =", repr(sh.display(5, 6)))
    print("表名 =", w.wb.sheet_names())
    from gui.engine import func_count
    print("函数数 =", func_count())

    # 保存一份 xlsx 并用 openpyxl 验证
    ok = w.wb.save("/data/workspace/xlengine/out/gui_示例.xlsx")
    print("保存 xlsx:", ok, w.wb.last_error)
    app.quit()


QTimer.singleShot(800, shot)
app.exec()
