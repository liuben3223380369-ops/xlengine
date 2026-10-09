"""
电子表格网格控件。

不用 QTableWidget，而是继承 QAbstractScrollArea 自己画，原因有三：
  1. 虚拟化 —— 只画可见格子，才能撑住百万行。QTableWidget 建百万个 item 会直接卡死。
  2. 完全控制合并单元格、冻结窗格、填充柄的绘制
  3. 单元格外观来自引擎的样式（粗体/填充/对齐/边框），需要按格取

坐标系（与引擎一致，全部从 0 开始）：
    col -> 0,1,2...  显示成 A,B,C
    row -> 0,1,2...  显示成 1,2,3
"""
from PySide6.QtCore import Qt, QRect, QRectF, QPoint, Signal, QTimer
from PySide6.QtGui import (QColor, QFont, QFontMetrics, QPainter, QPen,
                           QBrush, QPalette, QKeySequence, QAction)
from PySide6.QtWidgets import (QAbstractScrollArea, QLineEdit, QApplication,
                               QMenu, QWidget, QInputDialog)

from . import engine
from . import engine as _e_lib

# ---- 外观常量 ----
HEADER_BG = QColor("#f0f0f0")
HEADER_FG = QColor("#444444")
GRID_LINE = QColor("#d0d0d0")
HEADER_LINE = QColor("#b0b0b0")
SELECT_BG = QColor("#cfe3ff")     # 选区底色（半透明感的浅蓝）
SELECT_BORDER = QColor("#1a73e8") # 选区边框
CURSOR_BORDER = QColor("#1a73e8")
FILL_HANDLE = QColor("#1a73e8")

DEF_COL_W = 80
DEF_ROW_H = 24
HEADER_H = 24      # 列标栏高度
HEADER_W = 46      # 行号栏宽度


def col_name(col):
    """0->A, 25->Z, 26->AA。与 Excel 一致（没有第 0 列这种东西）。"""
    s = ""
    n = col
    while True:
        s = chr(ord('A') + n % 26) + s
        n = n // 26 - 1
        if n < 0:
            break
    return s


def addr(col, row):
    return "%s%d" % (col_name(col), row + 1)


def parse_addr(text):
    """'B3' -> (1, 2)。解析失败返回 None。UI 里地址框要用到。"""
    t = (text or "").strip().upper()
    c = 0
    i = 0
    while i < len(t) and t[i].isalpha():
        c = c * 26 + (ord(t[i]) - ord('A') + 1)
        i += 1
    rest = t[i:]
    if not c or not rest.isdigit():
        return None
    return (c - 1, int(rest) - 1)


class GridView(QAbstractScrollArea):
    """核心网格控件。"""

    # 选区变化 -> 主窗口更新公式栏与地址框
    selectionChanged = Signal()
    # 需要主窗口刷新状态栏（如"就绪"/"计算中"）
    statusMessage = Signal(str)
    # 内容变化 -> 标记未保存
    modified = Signal()
    # 即将改动这些格子 -> 主窗口先取快照再让我改。
    # 必须在**改之前**发：改完再取到的就是新值，撤销等于没做。
    cellsWillChange = Signal(object, str)

    def __init__(self, workbook, parent=None):
        super().__init__(parent)
        self.wb = workbook
        self.sheet_index = 0
        self.setFocusPolicy(Qt.StrongFocus)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.viewport().setMouseTracking(True)

        # 选区：anchor 是起点，cursor 是活动端（Excel 里反白显示的那格）
        self.anchor_col = 0
        self.anchor_row = 0
        self.cursor_col = 0
        self.cursor_row = 0

        self._col_widths = {}    # 列号 -> 像素宽（只存改过的）
        self._row_heights = {}
        self._editing = False
        self._editor = None
        self._drag_fill = None
        self._drag_move = None       # (来源区域, 落点左上角, 提示矩形)
        self._move_candidate = None  # 悬停在选区边框上时待命
        self._drag_select = False
        self._resizing = None    # ('col'|'row', index, start_pos)
        self._frozen_cols = 0    # 冻结窗格：左侧列数 / 顶部行数
        self._filter_cols = set()  # 自动筛选覆盖的列（画下拉箭头用）
        self._frozen_rows = 0

        self._font = QFont()
        self._font.setPointSize(10)
        self._fm = QFontMetrics(self._font)

        self.verticalScrollBar().valueChanged.connect(self._on_scroll)
        self.horizontalScrollBar().valueChanged.connect(self._on_scroll)

        self._update_scrollbars()
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self._context_menu)

    # ------------------------------------------------------------------
    # 表 / 尺寸
    # ------------------------------------------------------------------
    @property
    def sheet(self):
        return self.wb.sheet(self.sheet_index)

    def set_sheet(self, idx):
        if idx == self.sheet_index:
            return
        self.sheet_index = idx
        self._col_widths.clear()
        self._row_heights.clear()
        self._refresh_freeze()
        self.anchor_col = self.anchor_row = 0
        self.cursor_col = self.cursor_row = 0
        self._update_scrollbars()
        self.viewport().update()
        self.selectionChanged.emit()

    def _refresh_freeze(self):
        """从引擎读冻结设置。切换表、设置冻结后都要调。"""
        try:
            self._frozen_cols, self._frozen_rows = self.sheet.freeze()
        except Exception:
            self._frozen_cols, self._frozen_rows = 0, 0

    def set_filter(self, c0, r0, c1, r1):
        """设置自动筛选并立即画出箭头。"""
        try:
            engine._lib.xl_set_autofilter(self.wb._p, self.sheet_index, c0, r0, c1, r1)
        except Exception:
            pass
        self._filter_cols = set(range(c0, c1 + 1))
        self.viewport().update()

    def set_freeze(self, cols, rows):
        """设置冻结并立即生效。"""
        if cols == self._frozen_cols and rows == self._frozen_rows:
            return
        try:
            engine._lib.xl_set_freeze(self.wb._p, self.sheet_index, cols, rows)
        except Exception:
            pass
        self._frozen_cols, self._frozen_rows = cols, rows
        self._update_scrollbars()
        self.viewport().update()

    def frozen_w(self):
        """冻结列区占的像素宽度。"""
        if not self._frozen_cols:
            return 0
        return sum(self.col_w(c) for c in range(self._frozen_cols))

    def frozen_h(self):
        if not self._frozen_rows:
            return 0
        return sum(self.row_h(r) for r in range(self._frozen_rows))

    def col_w(self, col):
        return self._col_widths.get(col, DEF_COL_W)

    def row_h(self, row):
        return self._row_heights.get(row, DEF_ROW_H)

    def set_col_w(self, col, w):
        self._col_widths[col] = max(20, min(1000, int(w)))
        self.sheet.set_col_width(col, max(1.0, w / 7.0))  # 引擎存的是字符数
        self._update_scrollbars()
        self.viewport().update()

    def set_row_h(self, row, h):
        self._row_heights[row] = max(12, min(500, int(h)))
        self.sheet.set_row_height(row, h)
        self._update_scrollbars()
        self.viewport().update()

    def _load_layout(self):
        """从引擎读回列宽行高（打开文件时用）。"""
        self._col_widths.clear()
        self._row_heights.clear()
        sh = self.sheet
        for c in range(0, 200):
            w = sh.col_width(c)
            if w > 0:
                self._col_widths[c] = int(w * 7)
        for r in range(0, 500):
            h = sh.row_height(r)
            if h > 0:
                self._row_heights[r] = int(h)
        self._update_scrollbars()

    def _update_scrollbars(self):
        # 给一个足够大的虚拟范围，用户能滚到很远，但不必真建那么多格子
        total_w = sum(self.col_w(c) for c in range(200)) + 2000
        total_h = sum(self.row_h(r) for r in range(500)) + 5000
        vsb = self.verticalScrollBar()
        hsb = self.horizontalScrollBar()
        vp = self.viewport()
        vsb.setRange(0, max(0, total_h - vp.height()))
        vsb.setPageStep(vp.height())
        vsb.setSingleStep(DEF_ROW_H)
        hsb.setRange(0, max(0, total_w - vp.width()))
        hsb.setPageStep(vp.width())
        hsb.setSingleStep(DEF_COL_W)

    def _on_scroll(self):
        self._close_editor(save=True)
        self.viewport().update()

    # ------------------------------------------------------------------
    # 坐标换算
    # ------------------------------------------------------------------
    def col_at_x(self, x):
        """屏幕 x -> 列号。

        冻结列不参与滚动，所以落在冻结区里要按"无偏移"反查；
        落在滚动区才补偿滚动偏移。两者混在一起算会让冻结区里的点击错位。
        """
        fw = self.frozen_w()
        if self._frozen_cols and x < HEADER_W + fw and x >= HEADER_W:
            acc = HEADER_W
            for c in range(self._frozen_cols):
                if x < acc + self.col_w(c):
                    return c
                acc += self.col_w(c)
            return self._frozen_cols - 1
        x += self.horizontalScrollBar().value()
        col = 0
        acc = HEADER_W
        while True:
            w = self.col_w(col)
            if x < acc + w or col > 5000:
                return col
            acc += w
            col += 1

    def row_at_y(self, y):
        fh = self.frozen_h()
        if self._frozen_rows and y < HEADER_H + fh and y >= HEADER_H:
            acc = HEADER_H
            for r in range(self._frozen_rows):
                if y < acc + self.row_h(r):
                    return r
                acc += self.row_h(r)
            return self._frozen_rows - 1
        y += self.verticalScrollBar().value()
        row = 0
        acc = HEADER_H
        while True:
            h = self.row_h(row)
            if y < acc + h or row > 100000:
                return row
            acc += h
            row += 1

    def x_of_col(self, col):
        acc = HEADER_W
        for c in range(col):
            acc += self.col_w(c)
        # 冻结列不减滚动偏移 —— 这就是"冻结"的全部秘密
        if col < self._frozen_cols:
            return acc
        return acc - self.horizontalScrollBar().value()

    def y_of_row(self, row):
        acc = HEADER_H
        for r in range(row):
            acc += self.row_h(r)
        if row < self._frozen_rows:
            return acc
        return acc - self.verticalScrollBar().value()

    def cell_rect(self, col, row):
        """含合并跨度。"""
        st, sc, sr = self.sheet.merge_info(col, row)
        if st == 2:
            w = sum(self.col_w(col + i) for i in range(sc))
            h = sum(self.row_h(row + i) for i in range(sr))
        else:
            w, h = self.col_w(col), self.row_h(row)
        return QRect(self.x_of_col(col), self.y_of_row(row), w, h)

    def visible_range(self):
        """当前可见的 (c0,r0,c1,r1)。只画这些，百万行也不卡。"""
        vp = self.viewport().rect()
        c0 = self.col_at_x(max(HEADER_W, 0) + 1)
        r0 = self.row_at_y(max(HEADER_H, 0) + 1)
        c1 = self.col_at_x(vp.width() - 1)
        r1 = self.row_at_y(vp.height() - 1)
        return (c0, r0, max(c1, c0), max(r1, r0))

    # ------------------------------------------------------------------
    # 选区
    # ------------------------------------------------------------------
    def sel_rect(self):
        return (min(self.anchor_col, self.cursor_col),
                min(self.anchor_row, self.cursor_row),
                max(self.anchor_col, self.cursor_col),
                max(self.anchor_row, self.cursor_row))

    def set_cursor(self, col, row, extend=False):
        col = max(0, col)
        row = max(0, row)
        if not extend:
            self.anchor_col, self.anchor_row = col, row
        self.cursor_col, self.cursor_row = col, row
        self._ensure_visible(col, row)
        self.viewport().update()
        self.selectionChanged.emit()

    def _ensure_visible(self, col, row):
        """滚动到让 (col,row) 可见。

        冻结区会盖住左上角，所以"可见"的边界不是 HEADER_W/HEADER_H，
        而是冻结区的外沿 —— 否则目标格会被冻结区挡住，看起来没滚过去。
        """
        vp = self.viewport().rect()
        fw, fh = self.frozen_w(), self.frozen_h()
        left = HEADER_W + fw
        top = HEADER_H + fh
        x, y = self.x_of_col(col), self.y_of_row(row)
        w, h = self.col_w(col), self.row_h(row)
        hsb, vsb = self.horizontalScrollBar(), self.verticalScrollBar()
        # 冻结列不用滚，它本来就一直可见
        if col >= self._frozen_cols:
            if x < left:
                hsb.setValue(hsb.value() + x - left)
            elif x + w > vp.width():
                hsb.setValue(hsb.value() + x + w - vp.width() + 2)
        if row >= self._frozen_rows:
            if y < top:
                vsb.setValue(vsb.value() + y - top)
            elif y + h > vp.height():
                vsb.setValue(vsb.value() + y + h - vp.height() + 2)

    # ------------------------------------------------------------------
    # 绘制
    # ------------------------------------------------------------------
    def paintEvent(self, ev):
        """分四个区域绘制。

        冻结窗格把视口切成田字格：
            A 冻列×冻行（不动）  B 滚列×冻行（横向滚）
            C 冻列×滚行（纵向滚） D 滚列×滚行（双向滚）

        每个区域单独 setClipRect 再画，这样滚出去的格子自然被裁掉，
        而冻结区始终保持在原位。一次性画整个视口是做不到"部分不动"的。
        """
        p = QPainter(self.viewport())
        p.setFont(self._font)
        vp = self.viewport().rect()

        fw, fh = self.frozen_w(), self.frozen_h()
        fc, fr = self._frozen_cols, self._frozen_rows
        sc0, sr0, sc1, sr1 = self.sel_rect()

        # 四个区域的屏幕矩形
        ax0, ay0 = HEADER_W, HEADER_H
        areas = []
        if fc and fr:
            areas.append((ax0, ay0, fw, fh, 0, 0, fc - 1, fr - 1))
        if fr:
            areas.append((ax0 + fw, ay0, vp.width() - ax0 - fw, fh,
                          self.col_at_x(ax0 + fw + 1), 0,
                          self.col_at_x(vp.width() - 1), fr - 1))
        if fc:
            areas.append((ax0, ay0 + fh, fw, vp.height() - ay0 - fh,
                          0, self.row_at_y(ay0 + fh + 1),
                          fc - 1, self.row_at_y(vp.height() - 1)))
        areas.append((ax0 + fw, ay0 + fh,
                      vp.width() - ax0 - fw, vp.height() - ay0 - fh,
                      self.col_at_x(ax0 + fw + 1) if fc else self.col_at_x(ax0 + 1),
                      self.row_at_y(ay0 + fh + 1) if fr else self.row_at_y(ay0 + 1),
                      self.col_at_x(vp.width() - 1),
                      self.row_at_y(vp.height() - 1)))

        for (x0, y0, aw, ah, c0, r0, c1, r1) in areas:
            if aw <= 0 or ah <= 0:
                continue
            clip = QRect(int(x0), int(y0), int(aw), int(ah))
            p.setClipRect(clip)
            c1 = max(c1, c0) + 1
            r1 = max(r1, r0) + 1
            for r in range(r0, min(r1, r0 + 400)):
                y = self.y_of_row(r)
                if y > y0 + ah:
                    break
                h = self.row_h(r)
                for c in range(c0, min(c1, c0 + 200)):
                    x = self.x_of_col(c)
                    if x > x0 + aw:
                        break
                    rect = QRect(x, y, self.col_w(c), h)
                    if rect.intersects(clip):
                        self._paint_cell(p, c, r, rect, (sc0, sr0, sc1, sr1))
            # 网格线
            p.setPen(QPen(GRID_LINE, 1))
            x = self.x_of_col(c0)
            for c in range(c0, min(c1 + 2, c0 + 202)):
                p.drawLine(x, y0, x, y0 + ah)
                x += self.col_w(c)
            y = self.y_of_row(r0)
            for r in range(r0, min(r1 + 2, r0 + 402)):
                p.drawLine(x0, y, x0 + aw, y)
                y += self.row_h(r)
            p.setClipRect(vp)

        self._paint_selection(p, sc0, sr0, sc1, sr1)
        self._paint_drag_hint(p)
        if fc or fr:
            self._paint_freeze(p, vp)
        self._paint_headers(p, vp)
        p.end()

    def _paint_freeze(self, p, vp):
        """冻结区的外沿加粗线，让用户一眼看出冻结在哪儿。"""
        fw, fh = self.frozen_w(), self.frozen_h()
        p.setPen(QPen(QColor("#1a73e8"), 2))
        if self._frozen_cols and HEADER_W + fw < vp.width():
            p.drawLine(HEADER_W + fw, HEADER_H, HEADER_W + fw, vp.height())
        if self._frozen_rows and HEADER_H + fh < vp.height():
            p.drawLine(HEADER_W, HEADER_H + fh, vp.width(), HEADER_H + fh)

    def _paint_cell(self, p, col, row, rect, sel):
        sc0, sr0, sc1, sr1 = sel
        sh = self.sheet

        st, _, _ = sh.merge_info(col, row)
        if st == 1:      # 被合并吞掉的格子不画
            return

        in_sel = sc0 <= col <= sc1 and sr0 <= row <= sr1
        text = sh.display(col, row)
        if not text:
            if in_sel:
                p.fillRect(rect, QBrush(SELECT_BG))
            return

        sty = sh.get_style(col, row)
        # 背景：选区优先于单元格填充色（这样选中时仍看得出选区）
        if in_sel:
            p.fillRect(rect, QBrush(SELECT_BG))
        elif sty.get("fill"):
            p.fillRect(rect, QBrush(_qcolor(sty["fill"])))

        # 字体
        f = QFont(self._font)
        if sty.get("bold"):
            f.setBold(True)
        if sty.get("italic"):
            f.setItalic(True)
        if sty.get("size"):
            f.setPointSize(sty["size"])
        p.setFont(f)
        if sty.get("font_color"):
            p.setPen(QPen(_qcolor(sty["font_color"])))
        else:
            p.setPen(QPen(QColor("#000000")))

        # 对齐：数字靠右、文本靠左，与 Excel 默认一致
        vtype = sh.value_type(col, row)
        if sty.get("halign"):
            ha = sty["halign"]
        else:
            ha = 3 if vtype == 1 else 1     # 3=右 1=左
        flags = Qt.AlignVCenter | (Qt.AlignRight if ha == 3 else
                                  (Qt.AlignHCenter if ha == 2 else Qt.AlignLeft))
        pad = 3
        tr = rect.adjusted(pad, 0, -pad, 0)
        elide = self._fm.elidedText(text.replace("\n", " "), Qt.ElideRight, tr.width())
        p.drawText(tr, flags, elide)
        p.setFont(self._font)

    @staticmethod
    def _halign_flag(h):
        return Qt.AlignRight if h == 3 else (Qt.AlignHCenter if h == 2 else Qt.AlignLeft)

    def _paint_selection(self, p, c0, r0, c1, r1):
        if c0 == c1 and r0 == r1:
            rect = self.cell_rect(c0, r0)
            p.setPen(QPen(CURSOR_BORDER, 2))
            p.drawRect(rect.adjusted(0, 0, -1, -1))
        else:
            x0 = self.x_of_col(c0)
            y0 = self.y_of_row(r0)
            x1 = self.x_of_col(c1) + self.col_w(c1)
            y1 = self.y_of_row(r1) + self.row_h(r1)
            rect = QRect(x0, y0, x1 - x0, y1 - y0)
            if rect.width() < 1 or rect.height() < 1:
                return
            p.setPen(QPen(SELECT_BORDER, 1))
            p.drawRect(rect.adjusted(0, 0, -1, -1))

        # 填充柄：右下角小方块，拖它能自动填充
        cr = self.cell_rect(self.cursor_col, self.cursor_row)
        hx = cr.right() - 4
        hy = cr.bottom() - 4
        if hx > HEADER_W and hy > HEADER_H:
            p.setPen(Qt.NoPen)
            p.setBrush(QBrush(FILL_HANDLE))
            p.drawRect(QRect(hx - 3, hy - 3, 7, 7))

        # 填充预览
        if self._drag_fill:
            fc0, fr0, fc1, fr1 = self._drag_fill
            x0 = self.x_of_col(fc0)
            y0 = self.y_of_row(fr0)
            x1 = self.x_of_col(fc1) + self.col_w(fc1)
            y1 = self.y_of_row(fr1) + self.row_h(fr1)
            p.setPen(QPen(SELECT_BORDER, 1, Qt.DashLine))
            p.drawRect(QRect(x0, y0, x1 - x0, y1 - y0).adjusted(0, 0, -1, -1))

    def _paint_drag_hint(self, p):
        """拖拽移动时画目标位置的虚线框，让用户知道会落在哪儿。"""
        if not self._drag_move or not self._drag_move[2]:
            return
        dc0, dr0, dc1, dr1 = self._drag_move[2]
        x0 = self.x_of_col(dc0)
        y0 = self.y_of_row(dr0)
        w = sum(self.col_w(c) for c in range(dc0, dc1 + 1))
        h = sum(self.row_h(r) for r in range(dr0, dr1 + 1))
        pen = QPen(QColor("#1a73e8"), 2, Qt.DashLine)
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        p.drawRect(QRect(x0, y0, w, h))

    def _paint_headers(self, p, vp):
        """列标栏 + 行号栏。

        自己算可见范围，不再依赖 paintEvent 传入 —— 现在有四个绘制区，
        表头是统一画在最上层的。
        """
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(HEADER_BG))
        p.drawRect(QRect(0, 0, HEADER_W, HEADER_H))
        p.drawRect(QRect(HEADER_W, 0, vp.width() - HEADER_W, HEADER_H))
        p.drawRect(QRect(0, HEADER_H, HEADER_W, vp.height() - HEADER_H))

        sc0, sr0, sc1, sr1 = self.sel_rect()
        fw, fh = self.frozen_w(), self.frozen_h()
        fc, fr = self._frozen_cols, self._frozen_rows

        # 列标：冻结列 + 可见的滚动列
        cols = list(range(fc)) + list(range(
            max(fc, self.col_at_x(HEADER_W + fw + 1)),
            self.col_at_x(vp.width() - 1) + 2))
        p.setFont(self._font)
        for c in cols:
            x = self.x_of_col(c)
            w = self.col_w(c)
            if x + w <= HEADER_W or x >= vp.width():
                continue
            # 被冻结区遮住的滚动列不画（它本来就在冻结区底下）
            if fc and c >= fc and x < HEADER_W + fw:
                continue
            sel = sc0 <= c <= sc1
            p.setPen(QPen(SELECT_BORDER if sel else HEADER_FG))
            p.setFont(_bold(self._font) if sel else self._font)
            p.drawText(QRect(x, 0, w, HEADER_H), Qt.AlignCenter, col_name(c))
            # 自动筛选箭头
            if self._filter_cols and c in self._filter_cols:
                self._paint_filter_arrow(p, x + w - 9, HEADER_H // 2 - 2)

        rows = list(range(fr)) + list(range(
            max(fr, self.row_at_y(HEADER_H + fh + 1)),
            self.row_at_y(vp.height() - 1) + 2))
        for r in rows:
            y = self.y_of_row(r)
            h = self.row_h(r)
            if y + h <= HEADER_H or y >= vp.height():
                continue
            if fr and r >= fr and y < HEADER_H + fh:
                continue
            sel = sr0 <= r <= sr1
            p.setPen(QPen(SELECT_BORDER if sel else HEADER_FG))
            p.setFont(_bold(self._font) if sel else self._font)
            p.drawText(QRect(0, y, HEADER_W, h), Qt.AlignCenter, str(r + 1))

        p.setFont(self._font)
        p.setPen(QPen(HEADER_LINE, 1))
        p.drawLine(0, HEADER_H, vp.width(), HEADER_H)
        p.drawLine(HEADER_W, 0, HEADER_W, vp.height())

    def _paint_filter_arrow(self, p, x, y):
        """筛选下拉箭头：一个小三角。"""
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(QColor("#666666")))
        p.drawPolygon([QPoint(x, y), QPoint(x + 7, y), QPoint(x + 3, y + 4)])

    # ------------------------------------------------------------------
    # 编辑
    # ------------------------------------------------------------------
    def edit_current(self, initial=None):
        c, r = self.cursor_col, self.cursor_row
        rect = self.cell_rect(c, r)
        if rect.top() < HEADER_H or rect.left() < HEADER_W:
            self._ensure_visible(c, r)
            rect = self.cell_rect(c, r)
        ed = QLineEdit(self.viewport())
        ed.setFont(self._font)
        ed.setText(initial if initial is not None else self.sheet.raw(c, r))
        ed.setGeometry(rect.adjusted(0, 0, 1, 1))
        ed.setFocus()
        ed.selectAll()
        ed.editingFinished.connect(lambda: self._close_editor(save=True))
        ed.show()
        self._editor = ed
        self._editing = True

    def _close_editor(self, save=False):
        if not self._editor:
            self._editing = False
            return
        ed = self._editor
        text = ed.text()
        self._editor = None
        self._editing = False
        if save:
            c, r = self.cursor_col, self.cursor_row
            self.cellsWillChange.emit([(c, r)], "输入内容")
            if self.sheet.set(c, r, text):
                self.wb.recalc()
                self.modified.emit()
            else:
                self.statusMessage.emit("公式错误: " + self.wb.last_error)
        ed.setParent(None)
        ed.deleteLater()
        self.viewport().update()
        self.setFocus()

    def is_editing(self):
        return self._editing and self._editor is not None

    # ------------------------------------------------------------------
    # 键盘
    # ------------------------------------------------------------------
    def keyPressEvent(self, ev):
        k = ev.key()
        mods = ev.modifiers()
        c, r = self.cursor_col, self.cursor_row

        if self.is_editing():
            if k in (Qt.Key_Return, Qt.Key_Enter):
                self._close_editor(save=True)
                self.set_cursor(c, r + 1)
                return
            if k == Qt.Key_Tab:
                self._close_editor(save=True)
                self.set_cursor(c + 1, r)
                return
            if k == Qt.Key_Escape:
                self._close_editor(save=False)
                return
            super().keyPressEvent(ev)
            return

        shift = bool(mods & Qt.ShiftModifier)
        ctrl = bool(mods & Qt.ControlModifier)

        if k == Qt.Key_F2:
            self.edit_current()
            return
        if k in (Qt.Key_Return, Qt.Key_Enter):
            if shift:
                self.set_cursor(c, r - 1)
            else:
                self.set_cursor(c, r + 1)
            return
        if k == Qt.Key_Tab:
            self.set_cursor(c + 1 if not shift else c - 1, r)
            return
        if k == Qt.Key_Delete:
            self._clear_selection()
            return
        if k == Qt.Key_Backspace:
            self._clear_selection()
            self.edit_current("")
            return

        if k == Qt.Key_Left:
            self.set_cursor(max(0, c - 1), r, shift)
        elif k == Qt.Key_Right:
            self.set_cursor(c + 1, r, shift)
        elif k == Qt.Key_Up:
            self.set_cursor(c, max(0, r - 1), shift)
        elif k == Qt.Key_Down:
            self.set_cursor(c, r + 1, shift)
        elif k == Qt.Key_PageDown:
            page = max(1, self.viewport().height() // DEF_ROW_H - 1)
            self.set_cursor(c, r + page, shift)
        elif k == Qt.Key_PageUp:
            page = max(1, self.viewport().height() // DEF_ROW_H - 1)
            self.set_cursor(c, max(0, r - page), shift)
        elif k == Qt.Key_Home:
            self.set_cursor(0, r if not ctrl else 0, shift or ctrl)
        elif k == Qt.Key_End:
            self.set_cursor(c, r, shift)
        elif k == Qt.Key_Escape:
            self.anchor_col, self.anchor_row = c, r
            self.viewport().update()
            self.selectionChanged.emit()
        else:
            # 直接打字进入编辑（与 Excel 一致：选中格子后直接输入会覆盖）
            t = ev.text()
            if t and (t.isprintable()) and not ctrl:
                self.edit_current(t)
                return
            super().keyPressEvent(ev)
            return
        ev.accept()

    # ------------------------------------------------------------------
    # 鼠标
    # ------------------------------------------------------------------
    def mousePressEvent(self, ev):
        pos = ev.position().toPoint() if hasattr(ev, "position") else ev.pos()
        x, y = pos.x(), pos.y()

        # 点表头：选整行/整列
        if y < HEADER_H and x > HEADER_W:
            col = self.col_at_x(x)
            self.anchor_col, self.anchor_row = col, 0
            self.cursor_col, self.cursor_row = col, 0
            self.viewport().update()
            self.selectionChanged.emit()
            return
        if x < HEADER_W and y > HEADER_H:
            row = self.row_at_y(y)
            self.anchor_col, self.anchor_row = 0, row
            self.cursor_col, self.cursor_row = 0, row
            self.viewport().update()
            self.selectionChanged.emit()
            return

        if self.is_editing():
            self._close_editor(save=True)

        col = self.col_at_x(x)
        row = self.row_at_y(y)

        # 拖拽移动：按在选区**边框**上才触发。
        # 边框之内是"重新选区"，边框之外也是"重新选区" ——
        # 不区分的话，想改选区时会不小心把数据搬走。
        sc0, sr0, sc1, sr1 = self.sel_rect()
        inside = sc0 <= col <= sc1 and sr0 <= row <= sr1
        on_edge = (inside and (col == sc0 or col == sc1 or row == sr0 or row == sr1)
                   and not (sc0 == sc1 and sr0 == sr1))
        if on_edge and ev.button() == Qt.LeftButton:
            self._drag_move = ((sc0, sr0, sc1, sr1), (col, row), None)
            self.setCursor(Qt.DragMoveCursor)
            return

        # 填充柄
        cr = self.cell_rect(self.cursor_col, self.cursor_row)
        if (abs(x - (cr.right() - 4)) < 6 and abs(y - (cr.bottom() - 4)) < 6
                and col == self.cursor_col and row == self.cursor_row):
            self._drag_fill = (col, row, col, row)
            self.viewport().update()
            return

        if ev.button() == Qt.RightButton:
            if not (self.sel_rect()[0] <= col <= self.sel_rect()[2]
                    and self.sel_rect()[1] <= row <= self.sel_rect()[3]):
                self.set_cursor(col, row)
            return

        self._drag_select = True
        self.set_cursor(col, row, bool(ev.modifiers() & Qt.ShiftModifier))

    def mouseMoveEvent(self, ev):
        pos = ev.position().toPoint() if hasattr(ev, "position") else ev.pos()
        x, y = pos.x(), pos.y()

        if self._drag_fill:
            col = self.col_at_x(x)
            row = self.row_at_y(y)
            self._drag_fill = (self.cursor_col, self.cursor_row,
                               max(self.cursor_col, col), max(self.cursor_row, row))
            self.viewport().update()
            return

        if self._drag_move:
            src, (ac, ar), _ = self._drag_move
            w = src[2] - src[0]
            h = src[3] - src[1]
            dc, dr = col - ac, row - ar
            dst = (src[0] + dc, src[1] + dr, src[0] + dc + w, src[1] + dr + h)
            self._drag_move = (src, (ac, ar), dst)
            self.viewport().update()
            return

        if self._drag_select:
            col = self.col_at_x(x)
            row = self.row_at_y(y)
            self.cursor_col, self.cursor_row = col, row
            self._ensure_visible(col, row)
            self.viewport().update()
            self.selectionChanged.emit()
            return

        # 悬停在表头分隔线上时给个可拖动的提示
        self._hover_resize(x, y)

    def _hover_resize(self, x, y):
        if y < HEADER_H and x > HEADER_W:
            acc = HEADER_W - self.horizontalScrollBar().value()
            for c in range(0, 500):
                acc += self.col_w(c)
                if abs(x - acc) < 3:
                    self.setCursor(Qt.SplitHCursor)
                    return
        elif x < HEADER_W and y > HEADER_H:
            acc = HEADER_H - self.verticalScrollBar().value()
            for r in range(0, 2000):
                acc += self.row_h(r)
                if abs(y - acc) < 3:
                    self.setCursor(Qt.SplitVCursor)
                    return
        self.unsetCursor()

    def mouseDoubleClickEvent(self, ev):
        pos = ev.position().toPoint() if hasattr(ev, "position") else ev.pos()
        if pos.y() < HEADER_H or pos.x() < HEADER_W:
            return
        self.edit_current()

    def mouseReleaseEvent(self, ev):
        if self._drag_move:
            src, _, dst = self._drag_move
            self._drag_move = None
            self.unsetCursor()
            if dst and dst[:2] != (src[0], src[1]):
                self._do_move(src, dst)
            self.viewport().update()
            return

        if self._drag_fill:
            c0, r0, c1, r1 = self._drag_fill
            self._drag_fill = None
            if (c0, r0) != (c1, r1):
                self._do_fill(c0, r0, c1, r1)
            self.viewport().update()
        self._drag_select = False

    # ------------------------------------------------------------------
    # 操作
    # ------------------------------------------------------------------
    def _clear_selection(self):
        c0, r0, c1, r1 = self.sel_rect()
        sh = self.sheet
        self.cellsWillChange.emit(
            [(c, r) for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)], "清除内容")
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                sh.erase(c, r)
        self.wb.recalc()
        self.modified.emit()
        self.viewport().update()

    def _do_fill(self, c0, r0, c1, r1):
        """填充柄：把源格向下/向右复制。

        公式里的相对引用要跟着走 —— 这是填充的灵魂。
        引擎侧已有引用重写能力，这里按"源格 -> 目标格"的偏移量逐格复制公式文本，
        由 set_formula 触发引擎内部的引用平移。
        """
        sh = self.sheet
        # 源就是当前光标所在的那一格，目标是从它到拖到的位置。
        # 走引擎的 fillRange：公式里的相对引用会按偏移平移，
        # 常量则原样复制。
        self.cellsWillChange.emit(
            [(c, r) for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)], "填充")
        n = sh.fill(c0, r0, c0, r0,
                    c0, r0,
                    c1 - c0 + 1, r1 - r0 + 1)
        if n < 0:
            self.statusMessage.emit("填充失败: " + self.wb.last_error)
            return
        self.wb.recalc()
        self.modified.emit()
        self.statusMessage.emit("已填充 %d 个单元格" % max(0, n - 1))

    def _context_menu(self, pos):
        m = QMenu(self)
        m.addAction("剪切\tCtrl+X", lambda: self._cut())
        m.addAction("复制\tCtrl+C", lambda: self._copy())
        m.addAction("粘贴\tCtrl+V", lambda: self._paste())
        m.addSeparator()
        m.addAction("清除内容\tDel", lambda: self._clear_selection())
        m.addSeparator()
        m.addAction("在上方插入行", lambda: self._insert_rows(True))
        m.addAction("在下方插入行", lambda: self._insert_rows(False))
        m.addAction("在左侧插入列", lambda: self._insert_cols(True))
        m.addAction("在右侧插入列", lambda: self._insert_cols(False))
        m.addSeparator()
        m.addAction("删除整行", lambda: self._delete_rows())
        m.addAction("删除整列", lambda: self._delete_cols())
        m.addSeparator()
        act_merge = m.addAction("合并单元格", lambda: self._toggle_merge())
        act_merge.setCheckable(True)
        st, _, _ = self.sheet.merge_info(self.cursor_col, self.cursor_row)
        act_merge.setChecked(st == 2)
        m.exec(self.viewport().mapToGlobal(pos))

    # ---- 剪贴板 ----
    def _copy(self):
        c0, r0, c1, r1 = self.sel_rect()
        sh = self.sheet
        rows = []
        for r in range(r0, r1 + 1):
            rows.append("\t".join(sh.display(c, r) for c in range(c0, c1 + 1)))
        QApplication.clipboard().setText("\n".join(rows))
        self.statusMessage.emit("已复制 %d 个单元格" % ((c1 - c0 + 1) * (r1 - r0 + 1)))

    def _cut(self):
        self._copy()
        self._clear_selection()

    def _paste(self):
        text = QApplication.clipboard().text()
        if not text:
            return
        sh = self.sheet
        r = self.cursor_row
        for line in text.split("\n"):
            c = self.cursor_col
            for cell in line.split("\t"):
                sh.set(c, r, cell)
                c += 1
            r += 1
        self.wb.recalc()
        self.modified.emit()
        self.viewport().update()
        self.statusMessage.emit("已粘贴")

    def _do_move(self, src, dst):
        """把选区搬到新位置。

        顺序必须是"先复制后擦除"，且都走引擎接口：
          - 复制走 fillRange，公式的相对引用会跟着平移
          - 擦除走 erase_range
        自己拼公式文本的话 =A1+1 搬到别处还指向 A1，数据就错了。
        """
        sh = self.sheet
        sc0, sr0, sc1, sr1 = src
        dc0, dr0 = dst[0], dst[1]
        # 先记快照供撤销
        # 源区 + 目标区都要快照：撤销时不仅要把数据搬回去，
        # 还要把目标位置原本的内容还原（否则会留下残骸）。
        # 宽度/高度必须是 (末-首+1)，写成 (末-首) 会少算一行一列。
        w0, h0 = sc1 - sc0 + 1, sr1 - sr0 + 1
        targets = [(c, r) for r in range(sr0, sr1 + 1) for c in range(sc0, sc1 + 1)]
        targets += [(c, r) for r in range(dr0, dr0 + h0) for c in range(dc0, dc0 + w0)]
        self.cellsWillChange.emit(list(dict.fromkeys(targets)), "移动选区")

        w, h = w0, h0
        n = self.sheet.copy_range(sc0, sr0, sc1, sr1, dc0, dr0)
        if n < 0:
            self.statusMessage.emit("移动失败: " + self.wb.last_error)
            return
        # 擦除原位置
        self.sheet.erase_range(sc0, sr0, sc1, sr1)
        self.wb.recalc()
        self.modified.emit()
        self.set_cursor(dc0, dr0)
        self.set_cursor(dc0 + w - 1, dr0 + h - 1, extend=True)
        self.statusMessage.emit("已移动到 %s" % addr(dc0, dr0))

    def _toggle_merge(self):
        c0, r0, c1, r1 = self.sel_rect()
        st, _, _ = self.sheet.merge_info(c0, r0)
        if st == 2:
            if self.sheet.unmerge(c0, r0, c1, r1):
                self.modified.emit()
                self.viewport().update()
                self.statusMessage.emit("已取消合并")
            else:
                self.statusMessage.emit("该位置没有可取消的合并")
            return
        if c0 == c1 and r0 == r1:
            self.statusMessage.emit("请先选择要合并的区域")
            return
        self.sheet.merge(c0, r0, c1, r1)
        self.modified.emit()
        self.viewport().update()
        self.statusMessage.emit("已合并 %s:%s" % (addr(c0, r0), addr(c1, r1)))

    def _insert_rows(self, above):
        c0, r0, c1, r1 = self.sel_rect()
        at = r0 if above else r1 + 1
        n = r1 - r0 + 1
        if self.sheet.insert_rows(at, n):
            self.wb.recalc()
            self.modified.emit()
            self.viewport().update()
            self.statusMessage.emit("已在第 %d 行处插入 %d 行" % (at + 1, n))
        else:
            self.statusMessage.emit("插入失败: " + self.wb.last_error)

    def _insert_cols(self, left):
        c0, r0, c1, r1 = self.sel_rect()
        at = c0 if left else c1 + 1
        n = c1 - c0 + 1
        if self.sheet.insert_cols(at, n):
            self.wb.recalc()
            self.modified.emit()
            self.viewport().update()
            self.statusMessage.emit("已在第 %s 列处插入 %d 列" % (col_name(at), n))
        else:
            self.statusMessage.emit("插入失败: " + self.wb.last_error)

    def _delete_rows(self):
        c0, r0, c1, r1 = self.sel_rect()
        if self.sheet.delete_rows(r0, r1 - r0 + 1):
            self.wb.recalc()
            self.modified.emit()
            self.viewport().update()
            self.statusMessage.emit("已删除第 %d–%d 行" % (r0 + 1, r1 + 1))
        else:
            self.statusMessage.emit("删除失败: " + self.wb.last_error)

    def _delete_cols(self):
        c0, r0, c1, r1 = self.sel_rect()
        if self.sheet.delete_cols(c0, c1 - c0 + 1):
            self.wb.recalc()
            self.modified.emit()
            self.viewport().update()
            self.statusMessage.emit("已删除 %s–%s 列" % (col_name(c0), col_name(c1)))
        else:
            self.statusMessage.emit("删除失败: " + self.wb.last_error)



def _qcolor(rrggbb):
    """'FF0000' -> QColor。引擎里颜色统一用 RRGGBB 字符串存。"""
    s = (rrggbb or "").lstrip("#")
    if len(s) != 6:
        return QColor()
    try:
        return QColor(int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))
    except ValueError:
        return QColor()


def _bold(base):
    f = QFont(base)
    f.setBold(True)
    return f
