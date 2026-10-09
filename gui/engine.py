"""
xlengine 的 ctypes 封装。

下面这层刻意保持很薄 —— 只做三件事：找到 .so、声明函数签名、把 char* 收成 str。
所有电子表格逻辑都在 C++ 引擎里，Python 侧一行都不重新实现。
"""
import ctypes
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))


def _find_lib():
    """按优先级找共享库。"""
    cands = [
        os.path.join(_HERE, "libxlengine.so"),
        os.path.join(_HERE, "..", "libxlengine.so"),
        os.path.join(_HERE, "..", "src", "libxlengine.so"),
        "/data/workspace/xlengine/libxlengine.so",
    ]
    if sys.platform == "win32":
        cands.insert(0, os.path.join(_HERE, "xlengine.dll"))
    elif sys.platform == "darwin":
        cands.insert(0, os.path.join(_HERE, "libxlengine.dylib"))
    for c in cands:
        if os.path.exists(c):
            return c
    raise RuntimeError(
        "找不到 xlengine 共享库。先执行 `make lib` 编译。\n已查找:\n  "
        + "\n  ".join(cands)
    )


_LIB_PATH = _find_lib()
_lib = ctypes.CDLL(_LIB_PATH)

# ---- 类型声明 ----
# char* 的返回一律用 c_void_p 接，再由 _s() 转成 str 并释放 ——
# 直接用 c_char_p 的话 ctypes 会自作主张缓存/改写，且拿不到释放机会。
_lib.xl_wb_new.restype = ctypes.c_void_p
_lib.xl_wb_new.argtypes = []
_lib.xl_wb_free.argtypes = [ctypes.c_void_p]

_lib.xl_wb_save.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_lib.xl_wb_load.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_lib.xl_wb_sheet_count.argtypes = [ctypes.c_void_p]
_lib.xl_wb_sheet_name.restype = ctypes.c_void_p
_lib.xl_wb_sheet_name.argtypes = [ctypes.c_void_p, ctypes.c_int]
_lib.xl_wb_add_sheet.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

_lib.xl_last_error.restype = ctypes.c_char_p
_lib.xl_str_free.argtypes = [ctypes.c_void_p]

_lib.xl_set_num.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_double]
_lib.xl_set_str.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
_lib.xl_set_bool.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_set_formula.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
_lib.xl_erase.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_recalc.argtypes = [ctypes.c_void_p]

_lib.xl_display.restype = ctypes.c_void_p
_lib.xl_display.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_formula.restype = ctypes.c_void_p
_lib.xl_formula.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_value_type.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_value_num.restype = ctypes.c_double
_lib.xl_value_num.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_has_cell.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_used_range.argtypes = [ctypes.c_void_p, ctypes.c_int,
                               ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                               ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]

_lib.xl_set_numfmt.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
_lib.xl_style_font.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                               ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
_lib.xl_style_fill.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
_lib.xl_style_align.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                ctypes.c_int, ctypes.c_int]
_lib.xl_style_border.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_char_p]
_lib.xl_style_range.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                ctypes.c_char_p, ctypes.c_char_p,
                                ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
_lib.xl_get_style.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                              ctypes.POINTER(ctypes.c_int),
                              ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p),
                              ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                              ctypes.POINTER(ctypes.c_int)]

_lib.xl_set_col_width.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_double]
_lib.xl_set_row_height.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_double]
_lib.xl_col_width.restype = ctypes.c_double
_lib.xl_col_width.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
_lib.xl_row_height.restype = ctypes.c_double
_lib.xl_row_height.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
_lib.xl_set_freeze.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_get_freeze.argtypes = [ctypes.c_void_p, ctypes.c_int,
                               ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
_lib.xl_set_autofilter.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_merge.argtypes = [ctypes.c_void_p, ctypes.c_int,
                          ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_merge_info.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                               ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]

_lib.xl_add_chart.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                              ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
_lib.xl_func_count.argtypes = []
_lib.xl_fill.argtypes = [ctypes.c_void_p, ctypes.c_int,
                         ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                         ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]


def _s(ptr):
    """把引擎 malloc 出来的 char* 收成 Python str 并释放。

    不要图省事用 c_char_p 直接接返回值：那样既拿不到释放机会，
    长时间运行会稳定泄漏（每个格子的显示文本都是一次分配）。
    """
    if not ptr:
        return ""
    try:
        return ctypes.string_at(ptr).decode("utf-8", "replace")
    finally:
        _lib.xl_str_free(ctypes.c_void_p(ptr))


def _e(py_str):
    return py_str.encode("utf-8") if py_str is not None else b""


class Sheet:
    """一张工作表的操作面。"""

    def __init__(self, wb, index):
        self._wb = wb
        self._i = index

    @property
    def index(self):
        return self._i

    # ---- 写入 ----
    def set_num(self, col, row, v):
        _lib.xl_set_num(self._wb._p, self._i, col, row, float(v))

    def set_str(self, col, row, s):
        _lib.xl_set_str(self._wb._p, self._i, col, row, _e(s))

    def set_bool(self, col, row, b):
        _lib.xl_set_bool(self._wb._p, self._i, col, row, 1 if b else 0)

    def set_formula(self, col, row, f):
        """返回 True 成功；失败时错误信息在 workbook.last_error。

        输入要不要带前导 = 都接受：
          - C 层的 xl_set_formula 存的是"不含 ="的公式文本
          - 但调用方几乎必然写成 "=SUM(A1:A5)"（Excel 里就是这么显示的）
          不在这里剥掉的话，存进去的会是 "=SUM(A1:A5)"，
          写出 xlsx 时再补一个 = 就变成 "==SUM(A1:A5)" —— Excel 打不开。
        """
        t = (f or "").strip()
        if t.startswith("="):
            t = t[1:]
        return _lib.xl_set_formula(self._wb._p, self._i, col, row, _e(t)) == 0

    def set(self, col, row, text):
        """智能写入：像 Excel 那样判断输入的是什么。

        以 = 开头   -> 公式
        纯数字      -> 数字
        true/false  -> 布尔
        其余        -> 文本
        """
        t = (text or "").strip()
        if t.startswith("="):
            return self.set_formula(col, row, t[1:])
        if t == "":
            _lib.xl_erase(self._wb._p, self._i, col, row)
            return True
        low = t.lower()
        if low in ("true", "false"):
            self.set_bool(col, row, low == "true")
            return True
        try:
            # 不要用 float(t) —— 它会接受 "nan" / "inf" / "1_000" 这些
            # Excel 里根本不是数字的东西。这里只认常规写法。
            if _looks_like_number(t):
                self.set_num(col, row, float(t.replace(",", "")))
                return True
        except ValueError:
            pass
        self.set_str(col, row, text)
        return True

    def erase(self, col, row):
        _lib.xl_erase(self._wb._p, self._i, col, row)

    # ---- 读取 ----
    def display(self, col, row):
        return _s(_lib.xl_display(self._wb._p, self._i, col, row))

    def formula(self, col, row):
        return _s(_lib.xl_formula(self._wb._p, self._i, col, row))

    def raw(self, col, row):
        """编辑框里该显示什么：有公式显示公式，否则显示值。"""
        f = self.formula(col, row)
        return ("=" + f) if f else self.display(col, row)

    def value_type(self, col, row):
        """0空 1数字 2文本 3布尔 4错误 5其它"""
        return _lib.xl_value_type(self._wb._p, self._i, col, row)

    def has_cell(self, col, row):
        return _lib.xl_has_cell(self._wb._p, self._i, col, row) != 0

    def used_range(self):
        a, b, c, d = (ctypes.c_int() for _ in range(4))
        if _lib.xl_used_range(self._wb._p, self._i,
                              ctypes.byref(a), ctypes.byref(b),
                              ctypes.byref(c), ctypes.byref(d)) != 0:
            return (0, 0, 0, 0)
        return (a.value, b.value, c.value, d.value)

    # ---- 格式与样式 ----
    def set_numfmt(self, col, row, code):
        _lib.xl_set_numfmt(self._wb._p, self._i, col, row, _e(code))

    def style(self, col, row, bold=None, italic=None, size=None,
              font_color=None, fill=None, halign=None, valign=None,
              border=None, border_color=None):
        if bold is not None or italic is not None or size is not None or font_color is not None:
            _lib.xl_style_font(self._wb._p, self._i, col, row,
                               1 if bold else 0, 1 if italic else 0, 0,
                               size or 0, _e(font_color) if font_color else b"")
        if fill is not None:
            _lib.xl_style_fill(self._wb._p, self._i, col, row, _e(fill))
        if halign is not None or valign is not None:
            _lib.xl_style_align(self._wb._p, self._i, col, row,
                                halign if halign is not None else 0,
                                valign if valign is not None else 0)
        if border is not None or border_color is not None:
            _lib.xl_style_border(self._wb._p, self._i, col, row,
                                 border or 0, _e(border_color) if border_color else b"")

    def style_range(self, c0, r0, c1, r1, **kw):
        _lib.xl_style_range(
            self._wb._p, self._i, c0, r0, c1, r1,
            1 if kw.get("bold") else 0,
            1 if kw.get("italic") else 0,
            kw.get("size", 0),
            _e(kw["font_color"]) if kw.get("font_color") else b"",
            _e(kw["fill"]) if kw.get("fill") else b"",
            kw.get("halign", -1), kw.get("valign", -1),
            kw.get("border", -1),
            _e(kw["border_color"]) if kw.get("border_color") else b"")

    def get_style(self, col, row):
        """返回 dict。UI 渲染时每个可见格子都调一次，所以尽量少分配。"""
        bold, italic, size = (ctypes.c_int() for _ in range(3))
        fc, fcol = ctypes.c_void_p(), ctypes.c_void_p()
        ha, va, bd = (ctypes.c_int() for _ in range(3))
        if _lib.xl_get_style(self._wb._p, self._i, col, row,
                             ctypes.byref(bold), ctypes.byref(italic), ctypes.byref(size),
                             ctypes.byref(fc), ctypes.byref(fcol),
                             ctypes.byref(ha), ctypes.byref(va), ctypes.byref(bd)) != 0:
            return {}
        return {
            "bold": bold.value != 0,
            "italic": italic.value != 0,
            "size": size.value,
            "font_color": _s(fc.value) if fc.value else "",
            "fill": _s(fcol.value) if fcol.value else "",
            "halign": ha.value,
            "valign": va.value,
            "border": bd.value,
        }

    # ---- 布局 ----
    def set_col_width(self, col, w):
        _lib.xl_set_col_width(self._wb._p, self._i, col, float(w))

    def set_row_height(self, row, h):
        _lib.xl_set_row_height(self._wb._p, self._i, row, float(h))

    def col_width(self, col):
        return _lib.xl_col_width(self._wb._p, self._i, col)

    def row_height(self, row):
        return _lib.xl_row_height(self._wb._p, self._i, row)

    def freeze(self):
        """(冻结列数, 冻结行数)"""
        c, r = ctypes.c_int(), ctypes.c_int()
        if _lib.xl_get_freeze(self._wb._p, self._i,
                              ctypes.byref(c), ctypes.byref(r)) != 0:
            return (0, 0)
        return (c.value, r.value)

    def merge(self, c0, r0, c1, r1):
        _lib.xl_merge(self._wb._p, self._i, c0, r0, c1, r1)

    def merge_info(self, col, row):
        """(状态, 跨列, 跨行)。状态：0 普通 1 被吞掉 2 合并区左上角"""
        sc, sr = ctypes.c_int(1), ctypes.c_int(1)
        st = _lib.xl_merge_info(self._wb._p, self._i, col, row,
                                ctypes.byref(sc), ctypes.byref(sr))
        return (st, sc.value, sr.value)

    def fill(self, srcC0, srcR0, srcC1, srcR1, dstC0, dstR0, cols, rows):
        """填充柄走这里。

        必须调引擎的 fillRange，不能"复制公式文本" ——
        复制文本时 =A1*2 填到第 4 行还是 =A1*2，引用不会跟着走。
        """
        return _lib.xl_fill(self._wb._p, self._i, srcC0, srcR0, srcC1, srcR1,
                            dstC0, dstR0, cols, rows)

    # ---- 结构性编辑 ----
    def insert_rows(self, at, count=1):
        return _lib.xl_insert_rows(self._wb._p, self._i, at, count) == 0

    def insert_cols(self, at, count=1):
        return _lib.xl_insert_cols(self._wb._p, self._i, at, count) == 0

    def delete_rows(self, at, count=1):
        return _lib.xl_delete_rows(self._wb._p, self._i, at, count) == 0

    def delete_cols(self, at, count=1):
        return _lib.xl_delete_cols(self._wb._p, self._i, at, count) == 0

    def unmerge(self, c0, r0, c1, r1):
        """返回 True 表示确实取消了合并；False 表示该处本来就没合并。"""
        return _lib.xl_unmerge(self._wb._p, self._i, c0, r0, c1, r1) == 0

    def export_pdf(self, path, font_path="", landscape=False):
        return _lib.xl_export_pdf(self._wb._p, self._i, _e(str(path)),
                                  _e(font_path), 1 if landscape else 0) == 0

    def add_chart(self, chart_type, c0, r0, c1, r1, title="",
                  has_header=True, series_in_rows=False):
        return _lib.xl_add_chart(self._wb._p, self._i, chart_type,
                                 c0, r0, c1, r1, _e(title),
                                 1 if has_header else 0,
                                 0 if series_in_rows else 1)


def _looks_like_number(t):
    """只认常规数字写法。

    float() 太宽松：它接受 "nan"、"inf"、"1_000"（Python 下划线写法），
    这些粘进单元格会让用户看到莫名其妙的结果。
    """
    if not t:
        return False
    t = t.replace(",", "")
    if t.startswith(("+", "-")):
        t = t[1:]
    if not t:
        return False
    dot = 0
    for ch in t:
        if ch == ".":
            dot += 1
            if dot > 1:
                return False
        elif not ch.isdigit():
            return False
    return True


class Workbook:
    def __init__(self):
        self._p = _lib.xl_wb_new()
        if not self._p:
            raise RuntimeError("创建工作簿失败")

    def __del__(self):
        try:
            if getattr(self, "_p", None):
                _lib.xl_wb_free(self._p)
        except Exception:
            pass

    @property
    def last_error(self):
        v = _lib.xl_last_error()
        return v.decode("utf-8", "replace") if v else ""

    def save(self, path):
        ok = _lib.xl_wb_save(self._p, _e(str(path))) == 0
        if not ok and not self.last_error:
            return False
        return ok

    def load(self, path):
        return _lib.xl_wb_load(self._p, _e(str(path))) == 0

    @property
    def sheet_count(self):
        return _lib.xl_wb_sheet_count(self._p)

    def sheet_name(self, i):
        return _s(_lib.xl_wb_sheet_name(self._p, i))

    def sheet_names(self):
        return [self.sheet_name(i) for i in range(self.sheet_count)]

    def export_pdf_all(self, path, font_path="", landscape=False):
        """全部工作表导出到一个 PDF。返回导出的表数，-1 表示失败。"""
        return _lib.xl_export_pdf_all(self._p, _e(str(path)), _e(font_path),
                                      1 if landscape else 0)

    def add_sheet(self, name=""):
        return _lib.xl_wb_add_sheet(self._p, _e(name))

    def sheet(self, i):
        return Sheet(self, i)

    def recalc(self):
        _lib.xl_recalc(self._p)


_lib.xl_func_names.restype = ctypes.c_void_p
_lib.xl_func_names.argtypes = []

_lib.xl_insert_rows.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_insert_cols.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_delete_rows.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_delete_cols.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_unmerge.argtypes = [ctypes.c_void_p, ctypes.c_int,
                            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.xl_export_pdf_all.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
_lib.xl_export_pdf.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p,
                               ctypes.c_char_p, ctypes.c_int]


def func_count():
    return _lib.xl_func_count()


def func_names():
    """全部函数名（含别名）。UI 的函数列表对话框用。"""
    return [x for x in _s(_lib.xl_func_names()).split(",") if x]

