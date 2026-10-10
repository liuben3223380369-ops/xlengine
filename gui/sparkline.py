"""
迷你图（Sparkline）—— 单元格内嵌的微型图表，Excel 叫 Sparklines。

三种类型：
  line    折线
  column  柱状
  winloss 盈亏（正值向上、负值向下，等宽不等高）

数据怎么存：
  存在 Python 层（内存 dict），**不写入 xlsx**。
  Excel 的迷你图要写在 extLst/sparklineGroup 里，格式较复杂；
  这里作为界面渲染特性实现，存盘后迷你图不保留 —— 这是明确的取舍，
  不是遗漏。

数值健壮性（这块最容易崩）：
  · 空数据 / 全是非数字 -> 不画
  · 最大值 == 最小值 -> 除零，必须单独处理（画成一条中线）
  · 单个数据点 -> 折线画成一个点，柱画成一根
"""

LINE = "line"
COLUMN = "column"
WINLOSS = "winloss"

TYPES = {LINE: "折线", COLUMN: "柱状", WINLOSS: "盈亏"}


def _to_float(v):
    if v is None:
        return None
    s = str(v).strip().replace(",", "")
    if not s:
        return None
    # 去掉百分号
    if s.endswith("%"):
        s = s[:-1]
        try:
            return float(s) / 100.0
        except ValueError:
            return None
    try:
        return float(s)
    except ValueError:
        return None


class SparklineStore:
    """(col, row) -> {'type', 'c0','r0','c1','r1', 'color'}"""

    def __init__(self):
        self._d = {}

    def set(self, col, row, kind, c0, r0, c1, r1, color="#4472c4"):
        self._d[(col, row)] = {
            "type": kind, "c0": c0, "r0": r0, "c1": c1, "r1": r1,
            "color": color,
        }

    def remove(self, col, row):
        self._d.pop((col, row), None)

    def get(self, col, row):
        return self._d.get((col, row))

    def has(self, col, row):
        return (col, row) in self._d

    def clear(self):
        self._d.clear()

    def __len__(self):
        return len(self._d)

    def items(self):
        return list(self._d.items())


def collect_values(sheet, spec):
    """
    按区域取一维数值序列。

    区域是单行就按列取，单列就按行取；两者都不是（二维块）时
    Excel 是按行优先展开，这里照做。
    """
    c0, r0, c1, r1 = spec["c0"], spec["r0"], spec["c1"], spec["r1"]
    out = []
    if r0 == r1:                      # 单行 -> 横向
        for c in range(c0, c1 + 1):
            v = _to_float(sheet.display(c, r0))
            if v is not None:
                out.append(v)
    elif c0 == c1:                    # 单列 -> 纵向
        for r in range(r0, r1 + 1):
            v = _to_float(sheet.display(c0, r))
            if v is not None:
                out.append(v)
    else:                             # 二维块 -> 行优先
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                v = _to_float(sheet.display(c, r))
                if v is not None:
                    out.append(v)
    return out


def draw(painter, rect, values, kind, color):
    """
    在 rect 内画迷你图。调用方已保证 values 非空。

    所有边界情况都在这里收敛：
      · 空 -> 直接返回
      · 全相等 -> 画中线，避免除零
      · 单点 -> 折线画点、柱画单根
    """
    if not values:
        return

    from PySide6.QtGui import QColor, QPen, QBrush
    from PySide6.QtCore import Qt, QPointF

    pad = 2
    x0 = rect.left() + pad
    y0 = rect.top() + pad
    w = max(rect.width() - 2 * pad, 1)
    h = max(rect.height() - 2 * pad, 1)

    lo = min(values)
    hi = max(values)

    col = QColor(color)

    # ---- 全相等：任何归一化都会除零，单独画一条中线 ----
    if hi == lo:
        mid = y0 + h / 2.0
        painter.setPen(QPen(col))
        if kind == COLUMN:
            n = len(values)
            bw = max(w / max(n, 1) * 0.6, 1)
            for i in range(n):
                cx = x0 + (i + 0.5) * (w / max(n, 1))
                painter.fillRect(int(cx - bw / 2), int(mid - h * 0.3),
                                 int(bw), int(h * 0.3), QBrush(col))
        else:
            painter.drawLine(int(x0), int(mid), int(x0 + w), int(mid))
        return

    n = len(values)
    step = w / max(n - 1, 1) if kind == LINE else w / max(n, 1)

    def y_of(v):
        return y0 + h - (v - lo) / (hi - lo) * h

    if kind == COLUMN:
        bw = max(step * 0.6, 1)
        base = y_of(max(lo, 0)) if lo < 0 else y0 + h
        base = min(max(base, y0), y0 + h)
        for i, v in enumerate(values):
            cx = x0 + (i + 0.5) * step
            yv = y_of(v)
            top = min(yv, base)
            hh = max(abs(base - yv), 1)
            painter.fillRect(int(cx - bw / 2), int(top), int(bw), int(hh),
                             QBrush(col))

    elif kind == WINLOSS:
        # 盈亏：只看正负，不看幅度，所以等宽不等高
        bw = max(step * 0.6, 1)
        mid = y0 + h / 2.0
        for i, v in enumerate(values):
            cx = x0 + (i + 0.5) * step
            if v >= 0:
                painter.fillRect(int(cx - bw / 2), int(mid - h * 0.4),
                                 int(bw), int(h * 0.4), QBrush(QColor("#4caf50")))
            else:
                painter.fillRect(int(cx - bw / 2), int(mid),
                                 int(bw), int(h * 0.4), QBrush(QColor("#f44336")))

    else:  # LINE
        pen = QPen(col)
        pen.setWidth(1)
        painter.setPen(pen)
        pts = [QPointF(x0 + i * step, y_of(v)) for i, v in enumerate(values)]
        if len(pts) == 1:
            # 单点画不出线，画个小圆点，否则整格空白像是没生效
            painter.setBrush(QBrush(col))
            painter.drawEllipse(pts[0], 1.5, 1.5)
        else:
            for i in range(len(pts) - 1):
                painter.drawLine(pts[i], pts[i + 1])
