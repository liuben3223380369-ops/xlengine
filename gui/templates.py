"""
常用模板 —— 一键生成带公式的表格骨架。

每个模板是一段"填充脚本"，跑完就得到一份能直接用的表：
单元格、公式、数字格式、列宽都设好。

为什么公式用 set_formula 而不是直接写结果：
  模板的价值在于"改一个数，合计跟着变"。写死常量就退化成一张死表。
"""

# 每个模板: (名称, 说明, fill(sheet) -> None)
# fill 里用 sh.set_str / set_num / set_formula / set_numfmt / style_range


def _budget(sh):
    """家庭预算表"""
    heads = ["项目", "预算", "实际", "差额", "占比"]
    for i, h in enumerate(heads):
        sh.set_str(i, 0, h)
    sh.style_range(0, 0, 4, 0, bold=True, fill="#ddebf7", border=1)

    rows = ["餐饮", "交通", "住房", "娱乐", "医疗", "其他"]
    budgets = [1500, 400, 3000, 500, 300, 400]
    actuals = [1680, 360, 3000, 720, 180, 350]
    for i, (n, b, a) in enumerate(zip(rows, budgets, actuals)):
        r = i + 1
        sh.set_str(0, r, n)
        sh.set_num(1, r, float(b))
        sh.set_num(2, r, float(a))
        # 差额 = 实际 - 预算。正数=超支，负数=结余。
        # 一开始我写成 B-C（预算-实际），结果超支显示成负数，
        # 与"差额为正即超支"的直觉相反。
        sh.set_formula(3, r, "C%d-B%d" % (r + 1, r + 1))
        # 占比 = 实际 / 实际总额
        sh.set_formula(4, r, "C%d/$C$8" % (r + 1))
        sh.set_numfmt(4, r, "0.0%")

    # 合计行
    r = 7
    sh.set_str(0, r, "合计")
    sh.set_formula(1, r, "SUM(B2:B7)")
    sh.set_formula(2, r, "SUM(C2:C7)")
    sh.set_formula(3, r, "C8-B8")
    sh.style_range(0, r, 4, r, bold=True, fill="#f2f2f2", border=1)

    for i in range(5):
        sh.set_col_width(i, 90)


def _attendance(sh):
    """考勤表"""
    sh.set_str(0, 0, "员工考勤表")
    sh.style_range(0, 0, 0, 0, bold=True, size=14)

    heads = ["姓名", "出勤天数", "请假", "迟到", "出勤率"]
    for i, h in enumerate(heads):
        sh.set_str(i, 2, h)
    sh.style_range(0, 2, 4, 2, bold=True, fill="#ddebf7", border=1)

    names = ["张三", "李四", "王五", "赵六"]
    data = [(22, 0, 1), (21, 1, 0), (20, 2, 2), (22, 0, 0)]
    for i, (n, (d, lv, lt)) in enumerate(zip(names, data)):
        r = i + 3
        sh.set_str(0, r, n)
        sh.set_num(1, r, float(d))
        sh.set_num(2, r, float(lv))
        sh.set_num(3, r, float(lt))
        # 出勤率 = 出勤 / 应出勤(22)
        sh.set_formula(4, r, "B%d/22" % (r + 1))
        sh.set_numfmt(4, r, "0.0%")

    r = 7
    sh.set_str(0, r, "平均")
    sh.set_formula(1, r, "AVERAGE(B4:B7)")
    sh.set_formula(4, r, "AVERAGE(E4:E7)")
    sh.set_numfmt(4, r, "0.0%")
    sh.style_range(0, r, 4, r, bold=True, fill="#f2f2f2", border=1)

    for i in range(5):
        sh.set_col_width(i, 95)


def _sales(sh):
    """销售报表"""
    heads = ["区域", "一季度", "二季度", "三季度", "四季度", "全年", "达成率"]
    for i, h in enumerate(heads):
        sh.set_str(i, 0, h)
    sh.style_range(0, 0, 6, 0, bold=True, fill="#ddebf7", border=1)

    regions = ["华东", "华南", "华北", "西南"]
    data = [
        (320, 380, 410, 450),
        (280, 310, 350, 390),
        (250, 290, 320, 360),
        (180, 210, 240, 270),
    ]
    targets = [1500, 1300, 1200, 900]
    for i, (rg, q) in enumerate(zip(regions, data)):
        r = i + 1
        sh.set_str(0, r, rg)
        for j, v in enumerate(q):
            sh.set_num(1 + j, r, float(v))
        sh.set_formula(5, r, "SUM(B%d:E%d)" % (r + 1, r + 1))
        # 达成率 = 全年 / 目标
        sh.set_formula(6, r, "F%d/%d" % (r + 1, targets[i]))
        sh.set_numfmt(6, r, "0.0%")

    r = 5
    sh.set_str(0, r, "合计")
    for j in range(5):
        col = chr(66 + j)
        sh.set_formula(1 + j, r, "SUM(%s2:%s5)" % (col, col))
    sh.style_range(0, r, 6, r, bold=True, fill="#f2f2f2", border=1)

    for i in range(7):
        sh.set_col_width(i, 90)


def _invoice(sh):
    """简易报价单"""
    sh.set_str(0, 0, "报价单")
    sh.style_range(0, 0, 0, 0, bold=True, size=14)

    heads = ["品名", "单价", "数量", "金额"]
    for i, h in enumerate(heads):
        sh.set_str(i, 2, h)
    sh.style_range(0, 2, 3, 2, bold=True, fill="#ddebf7", border=1)

    items = [("服务费", 500, 2), ("材料费", 120, 8), ("人工费", 300, 3)]
    for i, (n, p, q) in enumerate(items):
        r = i + 3
        sh.set_str(0, r, n)
        sh.set_num(1, r, float(p))
        sh.set_num(2, r, float(q))
        sh.set_formula(3, r, "B%d*C%d" % (r + 1, r + 1))

    r = 6
    sh.set_str(0, r, "小计")
    sh.set_formula(3, r, "SUM(D4:D6)")
    sh.set_str(0, r + 1, "税(6%)")
    sh.set_formula(3, r + 1, "D7*0.06")
    sh.set_str(0, r + 2, "总计")
    sh.set_formula(3, r + 2, "D7+D8")
    sh.style_range(0, r + 2, 3, r + 2, bold=True, fill="#f2f2f2", border=1)

    for i in range(4):
        sh.set_col_width(i, 110)


TEMPLATES = [
    ("家庭预算表", "收支预算与实际对比，自动算差额与占比", _budget),
    ("员工考勤表", "出勤/请假/迟到统计，自动算出勤率", _attendance),
    ("销售报表", "四季度的区域业绩，自动汇总与达成率", _sales),
    ("报价单", "品名单价数量，自动算金额与税", _invoice),
]


def names():
    return [t[0] for t in TEMPLATES]


def apply_template(sh, index):
    """在当前表上套用模板。返回模板名或 None。"""
    if not (0 <= index < len(TEMPLATES)):
        return None
    name, _, fn = TEMPLATES[index]
    fn(sh)
    return name
