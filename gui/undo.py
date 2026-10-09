"""
撤销 / 重做。

设计上有一个必须想清楚的点：**快照的粒度与时机**。

方案 A：每次操作前整体深拷贝工作簿 —— 简单，但 10 万格的表按一次键就拷一遍，
不可接受（而且引擎的 Workbook 是 C++ 对象，Python 侧根本拷不了）。

方案 B：记录逆操作 —— 正确但复杂，每种操作都要写对应的撤销逻辑，
漏一种就是"撤销后数据不对"，比不能撤销更糟。

这里选 **方案 C：只对"被改动的格子"做前后值快照**。
一次操作通常只碰几个格子（批量操作也就几百个），快照很小。
结构性编辑（插入/删除行列）无法用格子快照表达，单独标记为不可撤销。

关键实现细节：
  - snapshot 必须在**改之前**取，undo 时把 before 写回
  - 存的是"设置所需的输入"（文本/公式/格式码），不是显示值。
    存显示值会把 =A1+B1 变成 30 这种常量，公式就死了。
"""
from collections import deque


class UndoStack:
    MAX = 100          # 上限，防止长时间编辑吃内存

    def __init__(self):
        self._undo = deque()
        self._redo = deque()

    # ------------------------------------------------------------------
    # 记录
    # ------------------------------------------------------------------
    def begin(self, sheet, label):
        """开始记录一次操作。返回本次的记录句柄。"""
        return {"sheet": sheet, "label": label, "cells": {}, "struct": None}

    def record_cell(self, rec, col, row, before_text, before_numfmt, before_style):
        rec["cells"][(col, row)] = (before_text, before_numfmt, before_style)

    def struct_op(self, rec, kind, payload):
        """结构性编辑（插入/删除行列）——无法用格子快照撤销。"""
        rec["struct"] = (kind, payload)

    def push(self, rec):
        if not rec:
            return
        if not rec["cells"] and not rec["struct"]:
            return
        self._undo.append(rec)
        if len(self._undo) > self.MAX:
            self._undo.popleft()
        self._redo.clear()          # 新操作作废重做链

    # ------------------------------------------------------------------
    # 查询
    # ------------------------------------------------------------------
    def can_undo(self):
        return bool(self._undo)

    def can_redo(self):
        return bool(self._redo)

    def undo_label(self):
        return self._undo[-1]["label"] if self._undo else ""

    def redo_label(self):
        return self._redo[-1]["label"] if self._redo else ""

    # ------------------------------------------------------------------
    # 执行
    # ------------------------------------------------------------------
    def pop_undo(self):
        return self._undo.pop() if self._undo else None

    def push_redo(self, rec):
        self._redo.append(rec)
        if len(self._redo) > self.MAX:
            self._redo.popleft()

    def pop_redo(self):
        return self._redo.pop() if self._redo else None

    def push_undo(self, rec):
        self._undo.append(rec)

    def clear(self):
        self._undo.clear()
        self._redo.clear()
