#!/usr/bin/env python3
"""用第三方实现交叉验证 xlengine 写出的 xlsx（含图表）。

为什么需要这一步：自己写的 writer 配自己写的 reader 只会"自洽"，不会暴露
格式不合规。两个 bug 都是这么抓到的：
  1. deflate 用了 zlib 流而非 ZIP 要求的 raw deflate —— 自测全绿，zipfile 拒绝
  2. sheet XML 用了 r:id 却没声明 xmlns:r —— 自测全绿，openpyxl 报 unbound prefix
"""
import sys, os, zipfile, openpyxl

ok = True
failures = []


def check(cond, msg, detail=""):
    global ok
    if cond:
        print(f"  [OK] {msg}")
    else:
        print(f"  [FAIL] {msg} {detail}")
        ok = False
        failures.append(msg)


# ---------------------------------------------------------------- 基础文件
print("out/示例.xlsx（无图表）")
p = "out/示例.xlsx"
with zipfile.ZipFile(p) as z:
    bad = z.testzip()
    check(bad is None, f"ZIP 结构校验（{len(z.namelist())} 个部件）", str(bad))

wb = openpyxl.load_workbook(p)
check(wb.sheetnames == ["销售数据", "参数"], "openpyxl 解析表名", str(wb.sheetnames))
ws = wb["销售数据"]
for addr, want in [("A1", "地区"), ("B2", 120), ("F2", "=SUM(B2:E2)"),
                   ("B8", "=F6*参数!B1*参数!B2")]:
    got = ws[addr].value
    check(got == want, f"{addr} = {want!r}", f"实际 {got!r}")

# ---------------------------------------------------------------- 带图表文件
print("\nout/带图表.xlsx（含 2 个图表）")
p2 = "out/带图表.xlsx"
with zipfile.ZipFile(p2) as z:
    bad = z.testzip()
    check(bad is None, "ZIP 结构校验", str(bad))
    names = z.namelist()
    for part in ["xl/charts/chart1.xml", "xl/charts/chart2.xml",
                 "xl/drawings/drawing1.xml",
                 "xl/drawings/_rels/drawing1.xml.rels",
                 "xl/worksheets/_rels/sheet1.xml.rels"]:
        check(part in names, f"部件存在: {part}")
    ct = z.read("[Content_Types].xml").decode()
    check("drawingml.chart+xml" in ct, "ContentTypes 声明 chart")
    check("drawing+xml" in ct, "ContentTypes 声明 drawing")

wb2 = openpyxl.load_workbook(p2)
ws2 = wb2["销售数据"]
check(len(ws2._charts) == 2, f"openpyxl 识别到 2 个图表", f"实际 {len(ws2._charts)}")

if len(ws2._charts) >= 1:
    ch = ws2._charts[0]
    check(type(ch).__name__ == "BarChart", "图表1 类型 BarChart", type(ch).__name__)
    check(len(ch.series) == 4, "图表1 有 4 个系列", f"实际 {len(ch.series)}")
    if len(ch.series) == 4:
        refs = []
        for ser in ch.series:
            try:
                refs.append(ser.val.numRef.f if (ser.val and ser.val.numRef) else None)
            except Exception:
                refs.append(None)
        check(refs[0] == "销售数据!$B$2:$B$5", "图表1 系列1 引用联动", str(refs[0]))
        check(all(r is not None for r in refs), "图表1 全部系列都有引用", str(refs))

if len(ws2._charts) >= 2:
    ch2 = ws2._charts[1]
    check(type(ch2).__name__ == "PieChart", "图表2 类型 PieChart", type(ch2).__name__)

# ------------------------------------------------- 多表与中文表名
#
# 表名有两条可能的数据来源：工作簿里的记录，和 Sheet 自身的名字。
# 只用其中一条就会出现"界面显示新名、存盘写旧名"这种只在存盘后才看得出的不一致，
# 所以这里必须读回文件、用第三方库确认落盘的名字与内存里的一致。
print("\n多表与中文表名")
try:
    wb3 = openpyxl.load_workbook("out/多表.xlsx")
    check(wb3.sheetnames == ["销售数据", "参数"], "表名往返一致", str(wb3.sheetnames))
    ws3 = wb3["销售数据"]
    check(ws3["C2"].value == "=B2*参数!B1", "跨表引用公式保留", str(ws3["C2"].value))
except Exception as ex:
    check(False, "多表往返", str(ex))

# ------------------------------------------------- 数字格式
#
# 格式码要能被第三方读出来，才算真的落盘了。自测只能证明"我写了我能读"，
# 这里用 openpyxl 确认 xf -> numFmtId -> formatCode 这条链是通的。
print("\n数字格式（out/数字格式.xlsx）")
import datetime
try:
    wb4 = openpyxl.load_workbook("out/数字格式.xlsx")
    ws4 = wb4["格式"]
    expect = {
        "B2": ("#,##0.00", 1234.5678),
        "B3": ("0.00%", 0.1234),
        "B4": ("yyyy-mm-dd", None),
        "B5": ("h:mm:ss", 0.625),
        "B6": ("#,##0,", 1234567.0),
        "B7": ("0", 1234.5678),
    }
    # 注意：openpyxl 会按格式码把序列号还原成 datetime / time 对象 ——
    # 这本身正说明格式被识别了，但断言数值时不能再当成 float 比。
    for cell, (code, val) in expect.items():
        got = ws4[cell].number_format
        check(got == code, f"{cell} 格式码 = {code}", f"实际 {got}")
        if val is None:
            continue
        v = ws4[cell].value
        if isinstance(v, (int, float)):
            check(abs(float(v) - val) < 1e-9,
                  f"{cell} 数值精确（不出现 1234.567800000000034）", str(v))
        elif isinstance(v, datetime.time):
            sec = v.hour * 3600 + v.minute * 60 + v.second
            check(abs(sec - val * 86400) < 2, f"{cell} 时间还原正确", str(v))
        elif isinstance(v, datetime.datetime):
            check(True, f"{cell} 被还原为日期 {v.date()}", "")
    # 未设置格式的格子必须是 General
    check(ws4["D2"].number_format == "General", "未设格式的格子是 General",
          str(ws4["D2"].number_format))
    # 日期序列号要能被第三方正确解释
    d = ws4["B4"].value
    if isinstance(d, datetime.datetime):
        check(d.year == 2026 and d.month == 9 and d.day == 27,
              "日期被第三方还原为 2026-09-27", str(d))
    else:
        check(abs(float(d) - 46262) < 1.5, "日期序列号量级正确", str(d))
except Exception as ex:
    check(False, "数字格式往返", str(ex))

# ------------------------------------------------- 样式与合并
#
# 样式写进 xlsx 后，格式是否合规、openpyxl 能否读出来，只能靠第三方判断。
# 自己写的 writer 配自己写的 reader 永远只会"自洽"。
print("\n样式与合并（out/样式示例.xlsx）")
try:
    wb5 = openpyxl.load_workbook("out/样式示例.xlsx")
    ws5 = wb5["样式"]

    # 合并区域
    merged = [str(m) for m in ws5.merged_cells.ranges]
    check(merged == ["A1:D1"], "合并区域 A1:D1", str(merged))

    # 标题：粗体 + 红字 + 居中
    t = ws5["A1"]
    check(t.font.b is True, "标题粗体", str(t.font.b))
    check(t.font.sz == 14, "标题字号 14", str(t.font.sz))
    fc = t.font.color
    rgb = fc.rgb if fc is not None else None
    check(rgb in ("FFFF0000", "00FF0000"), "标题红色字体", str(rgb))
    check(t.alignment.horizontal == "center", "标题水平居中", str(t.alignment.horizontal))

    # 表头：黄底 + 细边框 + 居中
    h = ws5["A2"]
    fill = h.fill.fgColor.rgb if h.fill and h.fill.fgColor else None
    check(fill in ("FFFFFF00", "00FFFF00"), "表头黄色填充", str(fill))
    check(h.border.left.style == "thin", "表头细边框", str(h.border.left.style))
    check(h.alignment.horizontal == "center", "表头居中", str(h.alignment.horizontal))

    # 数据：千分位格式 + 右对齐
    d = ws5["B3"]
    check(d.number_format == "#,##0", "数据千分位格式", str(d.number_format))
    check(d.alignment.horizontal == "right", "数据右对齐", str(d.alignment.horizontal))
    check(float(d.value) == 120, "数值正确", str(d.value))
except Exception as ex:
    check(False, "样式与合并往返", str(ex))

# ------------------------------------------------- 条件格式
# 条件格式要写进两个部件：sheetN.xml 的 cfRule + styles.xml 的 dxfs。
# 只写一处的话，自己的 reader 能读回来，openpyxl 却什么也看不到 ——
# 所以必须靠第三方确认。
print("\n条件格式（out/条件格式.xlsx）")
try:
    wb6 = openpyxl.load_workbook("out/条件格式.xlsx")
    ws6 = wb6["成绩"]
    rules = []
    for cf in ws6.conditional_formatting:
        for r in cf.rules:
            rules.append((str(cf.sqref), r.type, r.operator, r.dxfId))
    check(len(rules) == 3, "读回 3 条规则", str(rules))
    check(any(t == "cellIs" and op == "lessThan" for _, t, op, _ in rules),
          "含 lessThan 规则", str(rules))
    check(any(t == "top10" for _, t, _, _ in rules), "含 top10 规则", str(rules))
    # dxf 样式必须真的挂着，否则规则形同虚设
    got_dxf = []
    for cf in ws6.conditional_formatting:
        for r in cf.rules:
            d = r.dxf
            bg = d.fill.bgColor.rgb if (d and d.fill and d.fill.bgColor) else None
            bo = bool(d.font.b) if (d and d.font) else False
            got_dxf.append((bg, bo))
    check(any(bg == "FFFF0000" for bg, _ in got_dxf), "dxf 红色填充已挂上", str(got_dxf))
    check(any(bo for _, bo in got_dxf), "dxf 粗体已挂上", str(got_dxf))
except Exception as ex:
    check(False, "条件格式往返", str(ex))

# ------------------------------------------------- 数据验证
# 数据验证要写进 sheetN.xml 的 <dataValidations>，位置在
# <conditionalFormatting> 之后、<drawing> 之前。
print("\n数据验证（out/数据验证.xlsx）")
try:
    wb7 = openpyxl.load_workbook("out/数据验证.xlsx")
    ws7 = wb7["录入表"]
    dvs = list(ws7.data_validations.dataValidation)
    check(len(dvs) == 3, "读回 3 条规则", str([d.type for d in dvs]))
    types = sorted(d.type for d in dvs)
    check(types == ["decimal", "list", "whole"], "类型齐全", str(types))
    whole = [d for d in dvs if d.type == "whole"][0]
    check(whole.operator == "between", "whole 运算符为 between", str(whole.operator))
    check(whole.formula1 == "1" or whole.formula1 == "18", "whole 下界", str(whole.formula1))
    lst = [d for d in dvs if d.type == "list"][0]
    # list 不写 operator —— Excel 不接受
    check(lst.operator is None, "list 未写 operator", str(lst.operator))
    check("是" in str(lst.formula1), "list 含候选项", str(lst.formula1))
except Exception as ex:
    check(False, "数据验证往返", str(ex))

# ------------------------------------------------- 批注
# 批注是独立部件，必须"三件套齐全"：commentsN.xml + sheet rels + ContentTypes
# 缺任何一件 Excel 都打不开或读不到，而自己的 reader 可能照样说成功。
print("\n批注（out/批注.xlsx）")
try:
    wb8 = openpyxl.load_workbook("out/批注.xlsx")
    ws8 = wb8["核算表"]
    got = {}
    for addr, cell in ws8._cells.items() if hasattr(ws8, "_cells") else []:
        pass
    # 按已知坐标取
    for addr, want in (("B2", "这笔含机票，需附行程单"),
                       ("C3", "已核对发票"),
                       ("B4", None)):
        cm = ws8[addr].comment
        if want is None:
            check(cm is not None and "超标" in cm.text, "%s 有多行批注" % addr,
                  str(cm.text) if cm else None)
        else:
            check(cm is not None and cm.text == want, "%s 批注内容" % addr,
                  str(cm.text) if cm else None)
    check(ws8["B2"].comment is not None and ws8["B2"].comment.author == "张三",
          "批注作者", str(ws8["B2"].comment.author) if ws8["B2"].comment else None)
    # 三件套：包内必须真有 comments 部件与 rels
    import zipfile
    z = zipfile.ZipFile("out/批注.xlsx")
    names = z.namelist()
    check("xl/comments1.xml" in names, "包内含 comments1.xml", str(names))
    check("xl/worksheets/_rels/sheet1.xml.rels" in names, "包内含 sheet1.xml.rels", str(names))
    ct = z.read("[Content_Types].xml").decode()
    check("comments+xml" in ct, "ContentTypes 有 comments override", None)
except Exception as ex:
    check(False, "批注往返", str(ex))

# ------------------------------------------------- 视图属性
# 列宽/行高/冻结/筛选是"丢了不报错"的典型：文件照样能打开，
# 只有用户在 Excel 里才会发现表头不再冻结、列宽全没了。必须靠第三方确认。
print("\n视图属性（out/视图属性.xlsx）")
try:
    wb9 = openpyxl.load_workbook("out/视图属性.xlsx")
    ws9 = wb9["销售明细"]
    check(abs((ws9.column_dimensions["A"].width or 0) - 16) < 0.01,
          "列宽 A=16", str(ws9.column_dimensions["A"].width))
    check(abs((ws9.column_dimensions["B"].width or 0) - 12) < 0.01,
          "列宽 B=12", str(ws9.column_dimensions["B"].width))
    check(abs((ws9.row_dimensions[1].height or 0) - 30) < 0.01,
          "行高 1=30", str(ws9.row_dimensions[1].height))
    check(str(ws9.freeze_panes) == "B2", "冻结在 B2（1列1行）", str(ws9.freeze_panes))
    check(ws9.auto_filter.ref == "A1:D5", "筛选区域 A1:D5", str(ws9.auto_filter.ref))
except Exception as ex:
    check(False, "视图属性往返", str(ex))

# ------------------------------------------------- 嵌入图片
# 图片与图表共用 drawing，但 XML 完全不同：图表 r:id + graphicFrame，
# 图片 r:embed + pic。三件套：media 部件 + drawing rels + ContentTypes 的 Default
print("\n嵌入图片（out/嵌入图片.xlsx）")
try:
    wb10 = openpyxl.load_workbook("out/嵌入图片.xlsx")
    ws10 = wb10["图表附图片"]
    imgs = list(ws10._images)
    check(len(imgs) == 1, "读回 1 张图片", str(len(imgs)))
    if imgs:
        raw = imgs[0]._data()
        check(len(raw) > 100, "图片字节非空", "%d 字节" % len(raw))
        # PNG 魔数：确认二进制没被文本化破坏
        check(raw[:4] == b"\x89PNG", "PNG 魔数完整（二进制未被破坏）", str(raw[:4]))
        a = imgs[0].anchor
        check(a._from.col == 8 and a._from.row == 3, "锚点 H4", "%d,%d" % (a._from.col, a._from.row))
    import zipfile
    z = zipfile.ZipFile("out/嵌入图片.xlsx")
    names = z.namelist()
    media = [n for n in names if "/media/" in n]
    check(len(media) == 1, "包内含 1 个 media 部件", str(media))
    ct = z.read("[Content_Types].xml").decode()
    check('Extension="png"' in ct, "ContentTypes 登记 png Default", None)
    check("image/png" in ct, "png 的 MIME 正确", None)
    # 图表仍在（与图片共享 drawing 时 rId 不能撞车）
    dr = [n for n in names if n.startswith("xl/drawings/drawing") and n.endswith(".xml")]
    if dr:
        dx = z.read(dr[0]).decode()
        # 注意 graphicFrame 带 macro 属性，不能按 "<xdr:graphicFrame>" 精确匹配
        check("xdr:pic" in dx and "xdr:graphicFrame" in dx and "c:chart" in dx,
              "同一 drawing 内图表与图片共存", None)
except Exception as ex:
    check(False, "图片往返", str(ex))

# ------------------------------------------------- 定义名称
# definedNames 必须排在 <sheets> 之后（CT_Workbook 序列约束）。
# 自己的解析器不校验顺序，所以这一项只能靠第三方确认。
print("\n定义名称（out/定义名称.xlsx）")
try:
    wb11 = openpyxl.load_workbook("out/定义名称.xlsx")
    dn = wb11.defined_names
    items = {}
    try:
        items = {k: (v.value if hasattr(v, "value") else str(v)) for k, v in dn.items()}
    except Exception:
        items = {}
    check(items.get("销售额") == "销售数据!$B$2:$B$5", "名称 销售额", str(items.get("销售额")))
    check(items.get("季度") == "销售数据!$A$2:$A$5", "名称 季度", str(items.get("季度")))
    check(items.get("税率") == "0.13", "名称 税率（常量型）", str(items.get("税率")))
    # 关键：公式里用了名称，且结果已算对 —— 证明名称真的接进了求值链
    ws11 = wb11["销售数据"]
    check("SUM" in str(ws11["A7"].value), "A7 存的是 SUM(销售额) 公式", str(ws11["A7"].value))
    # 值已算出（不是残留公式字符串）
    # 缓存值要用 data_only=True 才读得到；默认读到的是公式文本本身
    try:
        wb11v = openpyxl.load_workbook("out/定义名称.xlsx", data_only=True)
        b7 = wb11v["销售数据"]["B7"].value
        b7 = float(b7) if b7 is not None else None
    except Exception:
        b7 = None
    check(b7 is not None and abs(b7 - 165) < 1e-9, "B7 MAX=165（缓存值）", str(b7))
except Exception as ex:
    check(False, "定义名称往返", str(ex))

# ---------------------------------------------------------------- PDF
#
# PDF 的自测只能证明"字节拼出来了"，证明不了"别人打得开"。
# 所以这里一律用第三方解析器复核：结构、页数、以及最关键的
# —— 文本能否被提取。提取不到就说明字体嵌入或 ToUnicode 有问题，
#   而这类问题在自测里完全看不出来（文件照样能生成）。
print("\nPDF 导出（pypdf 复核）")
try:
    import pypdf
except ImportError:
    print("  [跳过] 未安装 pypdf")
    sys.exit(0 if ok else 1)

pdf_cases = [
    ("out/表格.pdf", "表格导出", ["销售数据", "华北", "测试行"]),
    ("out/pdf_柱形图.pdf", "柱形图", ["销售分析", "华北", "线上"]),
    ("out/pdf_饼图.pdf", "饼图", ["销售分析", "%"]),
    ("out/pdf_折线图.pdf", "折线图", ["销售额", "季度"]),
]
for path, name, must in pdf_cases:
    if not os.path.exists(path):
        check(False, f"{name}: 文件存在 {path}")
        continue
    try:
        rd = pypdf.PdfReader(path)
        txt = "\n".join((p.extract_text() or "") for p in rd.pages)
        check(len(rd.pages) >= 1, f"{name}: 可解析，{len(rd.pages)} 页")
        for m in must:
            check(m in txt, f"{name}: 文本含 {m!r}")
        # 字体必须真的嵌进去了
        try:
            res = rd.pages[0]["/Resources"]
            has_font = "/Font" in res
            check(has_font, f"{name}: 页面含字体资源")
            if has_font:
                f0 = list(res["/Font"].values())[0].get_object()
                check(str(f0.get("/Subtype")) == "/Type0", f"{name}: 字体为 Type0")
                desc = f0["/DescendantFonts"][0].get_object()["/FontDescriptor"].get_object()
                check(len(desc["/FontFile2"].get_data()) > 1000,
                      f"{name}: FontFile2 非空")
        except Exception as ex:
            check(False, f"{name}: 字体检查", str(ex))
    except Exception as ex:
        check(False, f"{name}: pypdf 解析", str(ex))

print("\n交叉验证:", "全部通过" if ok else f"{len(failures)} 项失败")
sys.exit(0 if ok else 1)
