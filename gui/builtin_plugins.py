"""
内置插件 —— 演示插件框架的用法，也提供几个实用小功能：

  · macro   : 宏系统（宏管理器面板 + 自动运行宏）
  · stats   : 选区统计（已在 excel_ui 里，这里演示事件监听）
  · demo    : 一键填入示例数据

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


def install_builtins(host, workbench):
    """登记全部内置插件并返回 host。"""
    for cls in (MacroPlugin, StatsPlugin, DemoPlugin):
        try:
            host.register(cls())
        except Exception:
            pass
    host.boot(workbench)
    return host
