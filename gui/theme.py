"""
主题（浅色 / 深色）。

实现方式：网格的颜色都是模块级 QColor 实例，而 QColor 是**可变对象**，
所以原地 setNamedColor 就能全局生效，不用改每一处绘制代码。

这一点值得记下来：如果当初把颜色写成不可变值（比如字符串常量），
做主题就得改几十处绘制代码，而且很容易漏掉一两个角落，
结果就是"深色模式下某处还是白底"。
"""

from PySide6.QtGui import QColor
from PySide6.QtWidgets import QApplication

# 与 grid.py 里的模块级 QColor 是同一个对象 —— 靠 import 拿到引用，
# 原地修改即可，不需要重新构造。
_GRID_MOD = None


def _grid_mod():
    global _GRID_MOD
    if _GRID_MOD is None:
        from gui import grid as _g
        _GRID_MOD = _g
    return _GRID_MOD


# 浅色（Excel 默认观感）
LIGHT = {
    "header_bg": "#f0f0f0",
    "header_fg": "#444444",
    "grid_line": "#d0d0d0",
    "header_line": "#b0b0b0",
    "select_bg": "#cfe3ff",
    "select_border": "#1a73e8",
    "cursor_border": "#1a73e8",
    "fill_handle": "#1a73e8",
    "cell_fg": "#000000",
    "cell_bg": "#ffffff",
}

# 深色
DARK = {
    "header_bg": "#2d2d30",
    "header_fg": "#c8c8c8",
    "grid_line": "#3f3f46",
    "header_line": "#55555c",
    "select_bg": "#2b4a72",
    "select_border": "#4da3ff",
    "cursor_border": "#4da3ff",
    "fill_handle": "#4da3ff",
    "cell_fg": "#e8e8e8",
    "cell_bg": "#1e1e1e",
}


def apply(dark=False, app=None):
    """
    切换主题。dark=True 为深色。返回 True/False。
    
    grid.py 里若还没有 CELL_FG / CELL_BG，就跳过那两项 —— 
    这样即使目标版本没改过，主题也不会整个崩掉。
    """
    g = _grid_mod()
    pal = DARK if dark else LIGHT

    pairs = [
        ("HEADER_BG", "header_bg"),
        ("HEADER_FG", "header_fg"),
        ("GRID_LINE", "grid_line"),
        ("HEADER_LINE", "header_line"),
        ("SELECT_BG", "select_bg"),
        ("SELECT_BORDER", "select_border"),
        ("CURSOR_BORDER", "cursor_border"),
        ("FILL_HANDLE", "fill_handle"),
        ("CELL_FG", "cell_fg"),
        ("CELL_BG", "cell_bg"),
    ]
    for attr, key in pairs:
        obj = getattr(g, attr, None)
        if isinstance(obj, QColor):
            obj.setNamedColor(pal[key])

    # 控件部分用 stylesheet
    app = app or QApplication.instance()
    if app is not None:
        if dark:
            app.setStyleSheet("""
                QMainWindow, QDialog, QMenuBar, QMenu {
                    background: #2d2d30; color: #e8e8e8;
                }
                QMenuBar::item:selected { background: #3f3f46; }
                QMenu::item:selected { background: #3f3f46; }
                QToolBar { background: #2d2d30; border: none; }
                QStatusBar { background: #2d2d30; color: #c8c8c8; }
                QLineEdit, QComboBox, QTextEdit, QListWidget {
                    background: #1e1e1e; color: #e8e8e8;
                    border: 1px solid #55555c;
                }
                QLabel { color: #e8e8e8; }
                QPushButton {
                    background: #3f3f46; color: #e8e8e8;
                    border: 1px solid #55555c; padding: 3px 8px;
                }
                QPushButton:hover { background: #4a4a52; }
                QTabWidget::pane { background: #1e1e1e; }
                QTabBar::tab { background: #2d2d30; color: #c8c8c8;
                               padding: 4px 10px; }
                QTabBar::tab:selected { background: #1e1e1e; }
            """)
        else:
            app.setStyleSheet("")
    return True
