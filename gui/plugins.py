"""
xlengine 插件框架。

设计要点（思路参考通用插件架构，实现为本项目的独立代码）：

  · register()  登记插件，boot()  顺序初始化
  · 事件总线     on / emit，插件之间靠事件解耦
  · 扩展点       ribbon（工具栏按钮）、context_menu（右键项）、shortcut（快捷键）
  · 故障隔离     单个插件 init 抛异常不能拖垮整个应用 ——
                 这点很关键：插件是后来加的，主程序必须能容忍它出错

为什么每个扩展点都记 owner：
  卸载插件时要能精确移除它的贡献，不能把别的插件的按钮一起删了。
"""

import traceback


class PluginHost:
    def __init__(self):
        self.plugins = []          # 已登记的插件对象
        self.loaded = []           # 初始化成功的插件名
        self.failed = []           # (插件名, 错误摘要)
        self._listeners = {}       # 事件名 -> [回调]
        self._ribbon = []          # [{owner, group, label, fn, tooltip}]
        self._ctx = []             # [{owner, label, fn}]
        self._shortcuts = []       # [{owner, key, fn}]

    # ------------------------------------------------------------------
    # 登记与启动
    # ------------------------------------------------------------------
    def register(self, plugin):
        """plugin 需有 name 属性，可选 version / init(host, workbench)。"""
        if plugin is None:
            return
        name = getattr(plugin, "name", None)
        if not name:
            return
        self.plugins.append(plugin)

    def boot(self, workbench=None):
        for p in self.plugins:
            try:
                init = getattr(p, "init", None)
                if init is None:
                    # 没有 init 的插件视为纯声明式，直接算加载成功
                    self.loaded.append(p.name)
                    continue
                init(self, workbench)
                self.loaded.append(p.name)
            except Exception as e:
                # 关键：绝不往上抛。一个插件坏了不该让程序起不来。
                self.failed.append((p.name, "%s: %s" % (type(e).__name__, e)))
                traceback.print_exc()
        self.emit("booted", workbench)

    # ------------------------------------------------------------------
    # 事件
    # ------------------------------------------------------------------
    def on(self, evt, fn):
        self._listeners.setdefault(evt, []).append(fn)

    def emit(self, evt, *args):
        """单个监听器抛异常不影响其它监听器，也不影响调用方。"""
        for fn in list(self._listeners.get(evt, [])):
            try:
                fn(*args)
            except Exception:
                traceback.print_exc()

    # ------------------------------------------------------------------
    # 扩展点
    # ------------------------------------------------------------------
    def add_ribbon(self, owner, group, label, fn, tooltip=""):
        self._ribbon.append({
            "owner": owner, "group": group, "label": label,
            "fn": fn, "tooltip": tooltip,
        })

    def add_context_menu(self, owner, label, fn):
        self._ctx.append({"owner": owner, "label": label, "fn": fn})

    def add_shortcut(self, owner, key, fn):
        self._shortcuts.append({"owner": owner, "key": key, "fn": fn})

    # ------------------------------------------------------------------
    # 查询 / 卸载
    # ------------------------------------------------------------------
    def ribbon_items(self, group=None):
        if group is None:
            return list(self._ribbon)
        return [x for x in self._ribbon if x["group"] == group]

    def context_items(self):
        return list(self._ctx)

    def shortcuts(self):
        return list(self._shortcuts)

    def unload(self, name):
        """按 owner 精确移除，只删这个插件的贡献。"""
        self._ribbon = [x for x in self._ribbon if x["owner"] != name]
        self._ctx = [x for x in self._ctx if x["owner"] != name]
        self._shortcuts = [x for x in self._shortcuts if x["owner"] != name]
        self.loaded = [x for x in self.loaded if x != name]

    # ------------------------------------------------------------------
    def report(self):
        lines = []
        lines.append("已加载 %d 个插件" % len(self.loaded))
        for n in self.loaded:
            lines.append("   [OK]   %s" % n)
        for n, err in self.failed:
            lines.append("   [失败] %s : %s" % (n, err))
        return "\n".join(lines)
