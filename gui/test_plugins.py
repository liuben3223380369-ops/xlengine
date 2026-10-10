"""
插件与新特性自检。

覆盖：插件故障隔离、迷你图边界、主题可逆、模板可算、二维码可存盘。
"""

import os
import sys
import tempfile

PASS = 0
FAIL = 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  [OK]   %s" % name)
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def main():
    from PySide6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv)

    # ---------------------------------------------------------------
    print("== 插件框架：故障隔离 ==")
    from gui.plugins import PluginHost

    class Bad:
        name = "坏插件"

        def init(self, host, wb):
            raise RuntimeError("模拟崩溃")

    class Good:
        name = "好插件"

        def init(self, host, wb):
            host.add_ribbon("好插件", "g", "按钮", lambda: None)

    h = PluginHost()
    h.register(Bad())
    h.register(Good())
    h.boot(None)                      # 不该抛
    check("坏插件不拖垮好插件", "好插件" in h.loaded)
    check("坏插件被记录", any("坏插件" in f[0] for f in h.failed))
    check("好插件的贡献仍在", len(h.ribbon_items()) == 1)

    calls = []
    h.on("t", lambda: calls.append(1))
    h.on("t", lambda: (_ for _ in ()).throw(ValueError()))
    h.on("t", lambda: calls.append(2))
    h.emit("t")
    check("坏监听器后好监听器仍执行", calls == [1, 2])

    h.unload("好插件")
    check("卸载只删自己的贡献", len(h.ribbon_items()) == 0)

    # ---------------------------------------------------------------
    print()
    print("== 迷你图 ==")
    from gui.sparkline import SparklineStore, collect_values

    class FakeSheet:
        """模拟一列数据。区域 c0==c1 时引擎按行取，所以索引用 r。"""

        def __init__(self, vals):
            self.v = vals

        def display(self, c, r):
            return self.v[r] if 0 <= r < len(self.v) else ""

    st = SparklineStore()
    st.set(5, 0, "line", 0, 0, 0, 4, "#4472c4")
    check("存进去能取回", st.has(5, 0) and st.get(5, 0)["type"] == "line")
    st.remove(5, 0)
    check("删除生效", not st.has(5, 0))

    sh = FakeSheet(["10", "50", "30", "80", "20"])
    spec = {"c0": 0, "r0": 0, "c1": 0, "r1": 4}
    check("取到 5 个数值", collect_values(sh, spec) == [10.0, 50.0, 30.0, 80.0, 20.0])

    sh2 = FakeSheet(["", "abc", "  ", "5"])
    check("跳过非数字", collect_values(sh2, {"c0": 0, "r0": 0, "c1": 0, "r1": 3}) == [5.0])
    sh3 = FakeSheet(["", "x"])
    check("全非数字返回空", collect_values(sh3, {"c0": 0, "r0": 0, "c1": 0, "r1": 1}) == [])

    # 全相等 / 单点：这两处历史上最容易除零或画成空白
    from PySide6.QtGui import QImage, QPainter, QColor
    from PySide6.QtCore import QRect
    from gui.sparkline import draw

    for label, vals in [("全相等", [7.0, 7.0, 7.0]),
                        ("单点", [3.0]),
                        ("两点", [1.0, 9.0]),
                        ("含负数", [-5.0, 0.0, 5.0])]:
        for kind in ("line", "column", "winloss"):
            img = QImage(60, 24, QImage.Format_RGB32)
            img.fill(QColor("#ffffff"))
            p = QPainter(img)
            try:
                draw(p, QRect(2, 2, 56, 20), vals, kind, "#4472c4")
                ok = True
            except Exception as e:
                ok = False
                print("      异常:", e)
            finally:
                p.end()
            check("绘制 %s/%s 不崩" % (label, kind), ok)

    # ---------------------------------------------------------------
    print()
    print("== 主题 ==")
    from gui import grid
    from gui.theme import apply

    def snap():
        return (grid.HEADER_BG.name(), grid.GRID_LINE.name(),
                grid.CELL_FG.name(), grid.CELL_BG.name())

    light = snap()
    apply(True, app)
    dark = snap()
    check("深色确实变了", dark != light)
    check("深色前景是浅色", dark[2] == "#e8e8e8")
    apply(False, app)
    check("切回浅色可逆", snap() == light)

    # ---------------------------------------------------------------
    print()
    print("== 模板 ==")
    from gui.app import MainWindow
    from gui.templates import names, apply_template

    w = MainWindow()
    check("有 4 个模板", len(names()) == 4)
    for i, n in enumerate(names()):
        w.wb.add_sheet(n)
        sh = w.wb.sheet(w.wb.sheet_count - 1)
        apply_template(sh, i)
        w.wb.recalc()
        ur = sh.used_range()
        check("模板「%s」有内容" % n, ur[2] >= ur[0] and ur[3] >= ur[1])

    # 预算表：差额 = 实际 - 预算，超支应为正
    sh = w.wb.sheet(w.wb.sheet_count - 4)
    diff = sh.display(3, 1)
    try:
        d = float(diff)
    except (TypeError, ValueError):
        d = None
    check("预算差额方向正确（超支为正）", d is not None and d > 0,
          "实际=%s" % diff)

    # ---------------------------------------------------------------
    print()
    print("== 二维码 ==")
    from gui.qrcode import available, make_png, make_matrix

    if not available():
        print("  [跳过] segno 未安装")
    else:
        m = make_matrix("HELLO")
        n = len(m)
        check("标准 QR 尺寸 21", n == 21, str(n))

        def corner(r0, c0):
            for i in range(7):
                for j in range(7):
                    edge = (i in (0, 6) or j in (0, 6))
                    mid = (2 <= i <= 4 and 2 <= j <= 4)
                    if (edge or mid) and not m[r0 + i][c0 + j]:
                        return False
                    if not (edge or mid) and m[r0 + i][c0 + j]:
                        return False
            return True

        check("左上定位角", corner(0, 0))
        check("右上定位角", corner(0, n - 7))
        check("左下定位角", corner(n - 7, 0))

        data, err = make_png("https://example.com")
        check("PNG 生成成功", bool(data), err)
        check("PNG 魔数正确", data[:8] == b"\x89PNG\r\n\x1a\n")

        _, err2 = make_png("")
        check("空内容被拒绝", err2 != "" and not _ if False else err2 != "")

        d = tempfile.mkdtemp()
        p = os.path.join(d, "qr.xlsx")
        w2 = MainWindow()
        rc = w2.wb.add_image(0, 2, 0, 4, 5, data, "png", "二维码")
        check("插入返回 0", rc == 0, w2.wb.last_error)
        check("存盘成功", w2.wb.save(p))

        import zipfile
        z = zipfile.ZipFile(p)
        media = [x for x in z.namelist() if "media" in x]
        check("包里有图片", len(media) == 1, str(media))
        if media:
            check("图片是 PNG", z.read(media[0])[:4] == b"\x89PNG")
        check("zip 结构完整", z.testzip() is None)

    print()
    print("通过 %d 项，失败 %d 项" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
