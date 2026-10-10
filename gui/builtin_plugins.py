"""
内置插件 —— 演示插件框架的用法，也提供几个实用小功能：

  · macro     : 宏系统（宏管理器面板 + 自动运行宏）
  · stats     : 选区统计（演示事件监听）
  · demo      : 一键填入示例数据
  · sparkline : 迷你图（单元格内嵌微型图表）
  · theme     : 浅色 / 深色主题切换
  · template  : 常用表格模板（预算/考勤/销售/报价单）

这个文件同时是插件的**示例**：照着写一个类、实现 init()，
再在 install_builtins 里登记，就能扩展 xlengine 而不用改主程序。
"""


class MacroPlugin:
    name = "macro"
    version = "1.0"

    def init(self, host, workbench):
        self.host = host
        self.workbench = workbench
        self.store = None

        host.add_ribbon(self.name, "自动化", "宏", self._open, "打开宏管理器")
        host.add_context_menu(self.name, "宏管理器…", self._open)

        # 打开工作簿后自动运行标记了 autorun 的宏
        host.on("fileOpen", self._autorun)

    def _open(self):
        from gui.macrodialog import open_macro_dialog
        open_macro_dialog(self.workbench)

    def _autorun(self, *a):
        """运行所有标记了 autorun 的宏。出错了不能影响别的工作簿操作。"""
        from gui.macro import MacroStore, MacroRunner
        try:
            store = MacroStore()
            for m in store.load():
                if not m.get("autorun"):
                    continue
                try:
                    MacroRunner(self.workbench).run(m.get("code", ""))
                except Exception:
                    pass
        except Exception:
            pass


class StatsPlugin:
    """演示"监听事件"的用法：选区变化时往状态栏写一句。"""

    name = "stats"
    version = "1.0"

    def init(self, host, workbench):
        self.workbench = workbench

        def on_sel(*a):
            try:
                ui = getattr(workbench, "excel_ui", None)
                if ui:
                    ui.update_stats()
            except Exception:
                pass

        host.on("selectionChanged", on_sel)


class DemoPlugin:
    name = "demo"
    version = "1.0"

    def init(self, host, workbench):
        host.add_ribbon(self.name, "示例", "填入示例数据",
                        workbench._fill_demo, "填入一份含公式的示例表")


class SparklinePlugin:
    """迷你图。Excel 里叫 Sparklines。"""

    name = "sparkline"
    version = "1.0"

    def init(self, host, workbench):
        self.workbench = workbench
        host.add_ribbon(self.name, "图表", "迷你图", self._insert,
                        "在单元格内插入微型图表")
        host.add_context_menu(self.name, "插入迷你图…", self._insert)

    def _insert(self):
        from gui.sparkdialog import open_spark_dialog
        open_spark_dialog(self.workbench)


class ThemePlugin:
    """主题切换。颜色是模块级 QColor，原地改即可全局生效。"""

    name = "theme"
    version = "1.0"

    def init(self, host, workbench):
        self.workbench = workbench
        self.dark = False
        host.add_ribbon(self.name, "视图", "深色/浅色", self._toggle,
                        "在深浅两套配色间切换")

    def _toggle(self):
        from gui.theme import apply
        self.dark = not self.dark
        apply(self.dark)
        self.workbench.grid.viewport().update()
        self.workbench.status_label.setText(
            "已切换到%s主题" % ("深色" if self.dark else "浅色"))


class TemplatePlugin:
    """模板：一键生成带公式的表格骨架。"""

    name = "template"
    version = "1.0"

    def init(self, host, workbench):
        self.workbench = workbench
        host.add_ribbon(self.name, "示例", "从模板新建…", self._pick,
                        "用预置模板生成一张表")

    def _pick(self):
        from PySide6.QtWidgets import QInputDialog
        from gui.templates import names, apply_template
        opts = names()
        name, ok = QInputDialog.getItem(
            self.workbench, "选择模板", "模板:", opts, 0, False)
        if not ok or not name:
            return
        idx = opts.index(name)
        wb = self.workbench.wb
        wb.add_sheet(name)
        sh = wb.sheet(wb.sheet_count - 1)
        apply_template(sh, idx)
        wb.recalc()
        self.workbench._refresh_tabs()
        self.workbench.grid.set_sheet(wb.sheet_count - 1)
        self.workbench.grid.viewport().update()
        self.workbench.dirty = True
        self.workbench._update_title()
        self.workbench.status_label.setText("已按模板生成「%s」" % name)


class QRPlugin:
    """二维码：生成后作为浮动图片插入工作表。"""

    name = "qrcode"
    version = "1.0"

    def init(self, host, workbench):
        self.workbench = workbench
        host.add_ribbon(self.name, "插入", "二维码", self._insert,
                        "把文本/网址生成二维码插入当前位置")
        host.add_context_menu(self.name, "插入二维码…", self._insert)

    def _insert(self):
        from gui.qrdialog import open_qr_dialog
        open_qr_dialog(self.workbench)


def install_builtins(host, workbench):
    """登记全部内置插件并返回 host。"""
    for cls in (MacroPlugin, StatsPlugin, DemoPlugin,
                SparklinePlugin, ThemePlugin, TemplatePlugin, QRPlugin):
        try:
            host.register(cls())
        except Exception:
            pass
    host.boot(workbench)
    return host
