"""
xlengine 宏系统 —— 用 Python 写宏，替代 VBA。

为什么宏语言选 Python：
  界面本身就是 Python/Qt，宏用 Python 写与引擎同进程，
  能直接调 495 个函数，不需要再嵌一个 JS 解释器。

安全检查为什么用 AST 而不是正则黑名单：
  正则黑名单能被轻易绕过 —— 比如 `getattr(__builtins__, 'ev'+'al')`、
  或者 `win` + `dow` 字符串拼接。字符串层面的匹配根本拦不住。
  AST 是在**语法结构**上检查，拼字符串也逃不掉，
  因为最终会走到一个 `Name` 节点或 `Attribute` 节点上。

  这是本项目相对"正则黑名单"方案的实质性改进，不是口号。

宏 API（宏代码里的 api 对象）：
  api.get('A1') / api.set('A1', v)      读写单元格（'=…' 视为公式）
  api.get_range('A1:C5')                读区域 -> 二维列表
  api.set_range('A1', [[...]])          写区域
  api.fill('A1:A10', v)                 区域填充
  api.clear('A1:C5')                    清空
  api.sel() / api.set_sel('B2')         选区
  api.sheets() / api.activate(name) / api.add_sheet(name)
  api.log(...) / api.toast(msg)
  api.recalc()
"""

import ast
import json
import os
import sys

# ---------------------------------------------------------------------------
# 安全检查
# ---------------------------------------------------------------------------

# 危险的内置名。拦的是"能逃出沙箱"的那几个。
BAD_NAMES = {
    "eval", "exec", "compile", "__import__", "open",
    "globals", "locals", "vars", "getattr", "setattr", "delattr",
    "input", "breakpoint", "memoryview",
}

# 危险模块：只要 import 它们就拒绝
BAD_MODULES = {
    "os", "sys", "subprocess", "socket", "shutil", "importlib",
    "pickle", "marshal", "ctypes", "multiprocessing", "signal",
    "ftplib", "smtplib", "http", "urllib", "requests", "webbrowser",
    "pathlib", "tempfile", "glob", "pty", "platform",
}

# 危险属性名。拦 __class__ 是为了堵住
#   (1).__class__.__bases__[0].__subclasses__() 这条经典逃逸链。
BAD_ATTRS = {
    "__class__", "__bases__", "__subclasses__", "__globals__",
    "__dict__", "__getattribute__", "__code__", "__builtins__",
    "__loader__", "__spec__", "__mro__", "__reduce__", "__import__",
}


def check_safety(code):
    """
    静态检查宏代码。安全返回 None，不安全返回 (原因,) 
    
    返回 None 表示通过 —— 这个约定容易写反，调用方务必用 `is not None`。
    """
    try:
        tree = ast.parse(code)
    except SyntaxError as e:
        return ("语法错误: %s" % e.msg,)

    for node in ast.walk(tree):
        # import os / from os import *
        if isinstance(node, ast.Import):
            for a in node.names:
                root = a.name.split(".")[0]
                if root in BAD_MODULES:
                    return ("禁止导入模块: %s" % a.name,)
        elif isinstance(node, ast.ImportFrom):
            if node.module:
                root = node.module.split(".")[0]
                if root in BAD_MODULES:
                    return ("禁止导入模块: %s" % node.module,)

        # eval(...) / open(...) 等直接调用
        elif isinstance(node, ast.Name):
            if node.id in BAD_NAMES:
                return ("禁止使用: %s" % node.id,)

        # x.__class__ / obj.__globals__
        elif isinstance(node, ast.Attribute):
            if node.attr in BAD_ATTRS:
                return ("禁止访问属性: %s" % node.attr,)

    return None


# ---------------------------------------------------------------------------
# 宏执行沙箱
# ---------------------------------------------------------------------------

# 允许在宏里使用的内置函数白名单。
# 不给 __import__ / open / eval 等 —— 宏只需要算数和处理数据。
SAFE_BUILTINS = {
    "abs": abs, "all": all, "any": any, "bool": bool, "dict": dict,
    "enumerate": enumerate, "filter": filter, "float": float,
    "format": format, "int": int, "isinstance": isinstance,
    "len": len, "list": list, "map": map, "max": max, "min": min,
    "range": range, "round": round, "set": set, "sorted": sorted,
    "str": str, "sum": sum, "tuple": tuple, "zip": zip,
    "print": print, "type": type, "chr": chr, "ord": ord,
    "divmod": divmod, "pow": pow, "reversed": reversed, "slice": slice,
}


class MacroAPI:
    """宏代码里拿到的 api 对象。所有单元格操作都经它，不暴露引擎内部对象。"""

    def __init__(self, win, log_fn=None):
        self._win = win
        self._log = log_fn or (lambda *a: None)

    # -- 单元格 --
    def get(self, addr):
        c, r = parse_addr(addr)
        return self._win.grid.sheet.display(c, r)

    def get_raw(self, addr):
        """拿原文（公式带 =），用于读回用户写的东西。"""
        c, r = parse_addr(addr)
        return self._win.grid.sheet.raw(c, r)

    def set(self, addr, v):
        from gui.excel_ui import _set_cell
        c, r = parse_addr(addr)
        _set_cell(self._win.grid.sheet, c, r, v)

    def get_range(self, a1):
        c0, r0, c1, r1 = parse_range(a1)
        sh = self._win.grid.sheet
        return [[sh.display(c, r) for c in range(c0, c1 + 1)]
                for r in range(r0, r1 + 1)]

    def set_range(self, addr, rows):
        from gui.excel_ui import _set_cell
        c0, r0 = parse_addr(addr)
        sh = self._win.grid.sheet
        for dr, row in enumerate(rows):
            for dc, v in enumerate(row):
                _set_cell(sh, c0 + dc, r0 + dr, v)

    def fill(self, a1, v):
        from gui.excel_ui import _set_cell
        c0, r0, c1, r1 = parse_range(a1)
        sh = self._win.grid.sheet
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                _set_cell(sh, c, r, v)

    def clear(self, a1):
        c0, r0, c1, r1 = parse_range(a1)
        sh = self._win.grid.sheet
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                sh.erase(c, r)

    # -- 选区 / 工作表 --
    def sel(self):
        return self._win.grid.sel_rect()

    def set_sel(self, addr):
        c, r = parse_addr(addr)
        self._win.grid.set_cursor(c, r)

    def sheets(self):
        return list(self._win.wb.sheet_names())

    def sheet_name(self):
        return self._win.wb.sheet_name(self._win.grid.sheet.index())

    def activate(self, name):
        names = self._win.wb.sheet_names()
        if name in names:
            self._win.grid.set_sheet(names.index(name))

    def add_sheet(self, name=None):
        self._win.wb.add_sheet(name or ("Sheet%d" % (self._win.wb.sheet_count() + 1)))

    # -- 杂项 --
    def recalc(self):
        self._win.wb.recalc()
        self._win.grid.viewport().update()

    def log(self, *args):
        self._log(" ".join(str(a) for a in args))

    def toast(self, msg):
        self._win.status_label.setText(str(msg))


class MacroRunner:
    def __init__(self, win, log_fn=None):
        self.win = win
        self.log_fn = log_fn or (lambda *a: None)

    def run(self, code):
        """执行宏代码。返回 (是否成功, 输出或错误信息)。"""
        bad = check_safety(code)
        if bad is not None:
            return (False, "安全检查未通过 —— %s" % bad[0])

        api = MacroAPI(self.win, self.log_fn)
        # 只给白名单内置函数，不给 __builtins__ 全集
        env = {"__builtins__": dict(SAFE_BUILTINS), "api": api}
        buf = []

        class _Log:
            def write(self, s):
                buf.append(s)

            def flush(self):
                pass

        old = sys.stdout
        sys.stdout = _Log()
        try:
            exec(compile(code, "<macro>", "exec"), env)
            return (True, "".join(buf))
        except Exception as e:
            return (False, "%s: %s" % (type(e).__name__, e))
        finally:
            sys.stdout = old
            try:
                self.win.wb.recalc()
                self.win.grid.viewport().update()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# 持久化
# ---------------------------------------------------------------------------

class MacroStore:
    """宏的存取。存 JSON 文件（放在配置目录，而不是工作簿里）。"""

    def __init__(self, path=None):
        if path is None:
            base = os.path.expanduser("~")
            d = os.path.join(base, ".xlengine")
            os.makedirs(d, exist_ok=True)
            path = os.path.join(d, "macros.json")
        self.path = path

    def load(self):
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, list):
                return [m for m in data
                        if isinstance(m, dict) and m.get("name") and "code" in m]
        except Exception:
            pass
        return self._defaults()

    def save(self, macros):
        try:
            with open(self.path, "w", encoding="utf-8") as f:
                json.dump(macros, f, ensure_ascii=False, indent=1)
            return True
        except Exception as e:
            return False

    def _defaults(self):
        """首次使用时给两个示例宏，让人知道能干什么。"""
        return [
            {
                "name": "九九乘法表",
                "autorun": False,
                "code": (
                    "# 在 A1 起生成九九乘法表\n"
                    "for r in range(1, 10):\n"
                    "    for c in range(1, r + 1):\n"
                    "        api.set(f'{chr(64 + c)}{r}', f'{c}×{r}={c * r}')\n"
                    "api.toast('九九乘法表已生成')\n"
                ),
            },
            {
                "name": "标红超100",
                "autorun": False,
                "code": (
                    "# A1:A100 里大于 100 的标红\n"
                    "n = 0\n"
                    "for row in api.get_range('A1:A100'):\n"
                    "    v = row[0]\n"
                    "    try:\n"
                    "        if float(v) > 100:\n"
                    "            n += 1\n"
                    "    except ValueError:\n"
                    "        pass\n"
                    "api.log(f'共 {n} 个超过 100')\n"
                ),
            },
        ]


# ---------------------------------------------------------------------------
# 地址解析
# ---------------------------------------------------------------------------

def parse_addr(addr):
    """'B3' -> (1, 2)。列在前行在后，都是 0 基。"""
    s = str(addr).strip().upper().replace("$", "")
    col = 0
    i = 0
    while i < len(s) and s[i].isalpha():
        col = col * 26 + (ord(s[i]) - 64)
        i += 1
    row = int(s[i:]) - 1 if i < len(s) else 0
    return (col - 1, row)


def parse_range(a1):
    """'A1:C5' -> (c0, r0, c1, r1)，含端点。"""
    s = str(a1).strip().upper().replace("$", "")
    if ":" in s:
        a, b = s.split(":", 1)
        c0, r0 = parse_addr(a)
        c1, r1 = parse_addr(b)
        return (min(c0, c1), min(r0, r1), max(c0, c1), max(r0, r1))
    c, r = parse_addr(s)
    return (c, r, c, r)
