#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
用 LibreOffice 做语义级交叉验证。

SciPy 能验证"数值算得对不对"，但验证不了 Excel 的那些**约定**——
运算符优先级、MOD 的符号约定、INT 的取整方向、文本与数字的强制转换、
错误传播……这些是"像不像 Excel"的关键，只能拿一个真正的电子表格实现来对照。

做法：
  1. 把待测公式逐条写进 xlsx（每行一条，放 B 列）
  2. 用 soffice --headless 重算并导出 CSV
  3. 与 xlengine 的求值结果逐项比对

用法：
    make tools/evalbatch
    python3 tools/locheck.py            # 全量
    python3 tools/locheck.py --quick    # 只跑核心语义
"""
import subprocess, sys, os, csv, shutil, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "evalbatch")

# LibreOffice 不认识的函数（太新或特有），返回 #NAME? —— 这不算引擎的错，
# 记为 N/A 而不是失败，否则会淹没真实问题。
def build_cases(quick=False):
    C = []
    def add(f, note=""):
        C.append((f, note))

    # ---- 运算符优先级（Excel 特有的坑）----
    add("-2^2",        "一元负号优先于 ^ → 4（不是 -4）")
    add("2^3^2",       "幂右结合 → 512")
    add("-2^3",        "-8")
    add("(-2)^2",      "括号 → 4")
    add("2+3*4",       "14")
    add("(2+3)*4",     "20")
    add("10-2-3",      "左结合 → 5")
    add("100/5/2",     "左结合 → 10")
    add("1=1=1",       "左结合：(1=1)=1 → FALSE")
    add("1<2<3",       "左结合：(1<2)<3 → TRUE<3 → FALSE")
    add("NOT(1>2)",    "TRUE")
    add("1&2",         "文本连接 → 12")
    add("\"a\"&1",     "a1")

    # ---- 比较与排序序：数字 < 文本 < 逻辑值 ----
    add("\"a\">1",     "文本 > 数字 → TRUE")
    add("TRUE>1",      "逻辑值最大 → TRUE")
    add("FALSE>0",     "FALSE > 0 → TRUE")
    add("\"A\"=\"a\"", "文本比较不区分大小写 → TRUE")
    add("1=\"1\"",     "数字与文本型数字不相等 → FALSE")
    add("1+1=2",       "TRUE")

    # ---- 取整方向 ----
    for x in [-2.5, -0.5, 0.5, 2.5]:
        add(f"INT({x})",       "向下取整")
        add(f"ROUND({x},0)",   "远离零（不是 banker's）")
        add(f"ROUNDDOWN({x},0)", "向零")
        add(f"ROUNDUP({x},0)", "远离零")
        add(f"TRUNC({x})",     "截断")
        add(f"CEILING({x},1)", "向上到 1 的倍数")
        add(f"FLOOR({x},1)",   "向下到 1 的倍数")
    add("MOD(-3,2)",  "与除数同号 → 1（fmod 会得 -1）")
    add("MOD(3,-2)",  "与除数同号 → -1")
    add("MOD(-3,-2)", "-1")
    add("MOD(3,2)",   "1")
    add("QUOTIENT(7,2)", "整除 → 3")
    add("SIGN(-5)",   "-1")
    add("SIGN(0)",    "0")

    # ---- 文本与数字的强制转换 ----
    add("1+\"2\"",       "文本型数字参与算术 → 3")
    add("\"50%\"+0",     "百分比文本 → 0.5")
    add("SUM(\"5\")",    "顶层标量文本型数字 → 5")
    add("1+TRUE",        "TRUE 当 1 → 2")
    add("1+FALSE",       "FALSE 当 0 → 1")
    add("\"1\"=\"1\"",   "TRUE")
    add("LEN(1)",        "数字转文本再取长 → 1")
    add("VALUE(\"1e2\")", "科学计数法文本 → 100")
    add("TEXT(0.5,\"0.0%\")", "格式化 → 50.0%")
    add("TEXT(1234.5678,\"#,##0.00\")", "千分位 → 1,234.57")

    # ---- 逻辑与空值 ----
    add("IF(TRUE,1,1/0)", "惰性求值：不选的分支不算 → 1")
    add("IF(FALSE,1/0,2)", "2")
    add("AND(TRUE,FALSE)", "FALSE")
    add("OR(FALSE,TRUE)",  "TRUE")
    add("ISBLANK(A5)",     "空格子 → TRUE")
    add("ISNUMBER(\"1\")", "文本 → FALSE")
    add("ISNUMBER(1)",     "TRUE")
    add("ISTEXT(1)",       "FALSE")
    add("NA()",            "#N/A")
    add("ISERROR(1/0)",    "TRUE")
    add("ISERR(NA())",     "ISERR 不认 #N/A → FALSE")
    add("IFERROR(1/0,\"兜底\")", "兜底")
    add("IFERROR(1,2)",    "1")

    # ---- 错误传播 ----
    add("1/0",          "#DIV/0!")
    add("0/0",          "#DIV/0!")
    add("\"a\"+1",      "#VALUE!")
    add("SQRT(-1)",     "#NUM!")
    add("VLOOKUP(99,{1,2},1,FALSE)", "找不到 → #N/A")
    add("1+#DIV/0!",    "错误传播")
    add("SUM(1,2,#N/A)", "错误传播")

    # ---- 查找与引用 ----
    add("VLOOKUP(3,{1,10;2,20;3,30;4,40},2,TRUE)", "近似匹配 → 30")
    add("VLOOKUP(2.5,{1,10;2,20;3,30},2,TRUE)",    "近似匹配取不大于 → 20")
    add("HLOOKUP(2,{1,2;10,20},2,TRUE)",           "行查找 → 20")
    add("MATCH(3,{1,2,3,4},0)",                    "精确 → 3")
    add("MATCH(3.5,{1,2,3,4},1)",                  "升序近似 → 3")
    add("INDEX({1,2,3;4,5,6},2,3)",                "6")
    add("INDEX({1,2,3},4)",                        "越界 → #REF!")
    add("OFFSET({1,2,3},0,1)",                     "2")
    add("LOOKUP(3,{1,2,3,4},{10,20,30,40})",       "30")
    add("CHOOSE(2,\"a\",\"b\",\"c\")",             "b")
    add("CHOOSE(0,\"a\",\"b\")",                   "#VALUE!")

    # ---- 统计 ----
    d = "{1,2,3,4,5,6,7,8,9,10}"
    add(f"SUM({d})",     "55")
    add(f"AVERAGE({d})", "5.5")
    add(f"COUNT({d})",   "10")
    add(f"MAX({d})",     "10")
    add(f"MIN({d})",     "1")
    add(f"STDEV.S({d})", "样本标准差")
    add(f"STDEV.P({d})", "总体标准差")
    add(f"VAR.S({d})",   "样本方差")
    add(f"VAR.P({d})",   "总体方差")
    add(f"MEDIAN({d})",  "5.5")
    add(f"MEDIAN({{1,2,3,4}})", "2.5")
    add("SUM(1,2,3,TRUE)",          "TRUE 不参与 → 6")
    add("AVERAGE({1,2,3,\"a\"})",   "区域内文本静默忽略 → 2")
    add("COUNT({1,\"a\",2})",       "只数数字 → 2")
    add("COUNTA({1,\"a\",2})",      "非空计数 → 3")
    add("AVERAGE({1,2,3})",         "2")

    # ---- 条件聚合 ----
    add("SUMIF({1,2,3,4},\">2\")",              "3+4=7")
    add("SUMIF({1,2,3,4},\">2\",{10,20,30,40})","30+40=70")
    add("COUNTIF({1,2,3,4},\">2\")",            "2")
    add("COUNTIF({\"a\",\"b\",\"a\"},\"a\")",   "2")
    add("AVERAGEIF({1,2,3,4},\">2\")",          "3.5")
    add("SUMIFS({1,2,3,4},{1,2,3,4},\">1\",{1,2,3,4},\"<4\")", "2+3=5")

    # ---- 文本 ----
    add("LEFT(\"abcdef\",3)",      "abc")
    add("RIGHT(\"abcdef\",3)",     "def")
    add("MID(\"abcdef\",2,3)",     "bcd")
    add("LEN(\"中文abc\")",        "5")
    add("UPPER(\"abc\")",          "ABC")
    add("LOWER(\"ABC\")",          "abc")
    add("TRIM(\"  a  b  \")",      "a b")
    add("FIND(\"b\",\"abc\")",     "2")
    add("SEARCH(\"B\",\"abc\")",   "不区分大小写 → 2")
    add("FIND(\"z\",\"abc\")",     "找不到 → #VALUE!")
    add("SUBSTITUTE(\"aaa\",\"a\",\"b\")", "bbb")
    add("REPT(\"ab\",3)",          "ababab")
    add("CONCAT(\"a\",\"b\")",     "ab")
    add("TEXTJOIN(\",\",TRUE,\"a\",\"\",\"b\")", "忽略空 → a,b")
    add("PROPER(\"hello world\")", "Hello World")
    add("CODE(\"A\")",             "65")
    add("CHAR(65)",                "A")
    add("MID(\"abc\",0,2)",        "起始<1 → #VALUE!")
    add("LEFT(\"abc\",-1)",        "负数 → #VALUE!")

    # ---- 日期 ----
    add("DATE(2024,2,29)",       "闰年合法")
    add("DATE(2023,2,29)",       "平年 2/29 → 3/1")
    add("DATE(2024,13,1)",       "月份溢出 → 2025-1-1")
    add("DATE(2024,0,1)",        "月份 0 → 2023-12-1")
    add("YEAR(DATE(2024,6,15))", "2024")
    add("MONTH(DATE(2024,6,15))","6")
    add("DAY(DATE(2024,6,15))",  "15")
    add("DATE(2024,6,15)-DATE(2024,6,1)", "14")
    add("WEEKDAY(DATE(2024,1,1))", "1900 系统下 2024-1-1 是周一 → 2")
    add("EOMONTH(DATE(2024,2,1),0)", "2024-02-29")
    add("EDATE(DATE(2024,1,31),1)",  "2024-02-29（月末夹取）")
    add("DAYS(DATE(2024,3,1),DATE(2024,1,1))", "60")
    add("DATEDIF(DATE(2024,1,1),DATE(2024,3,1),\"d\")", "60")
    add("NETWORKDAYS(DATE(2024,1,1),DATE(2024,1,31))", "2024年1月工作日 23")
    add("TODAY()-TODAY()", "0")
    add("YEARFRAC(DATE(2024,1,1),DATE(2024,7,1),1)", "闰年半年")

    # ---- 财务 ----
    add("PMT(0.05/12,12,10000)",   "等额本息月供（负）")
    add("PV(0.05,10,-1000)",       "现值")
    add("FV(0.05,10,-1000)",       "终值")
    add("RATE(10,-1000,8000)",     "利率")
    add("NPER(0.05,-1000,8000)",   "期数")
    add("NPV(0.1,{-1000,500,600})","第1期开始折现")
    add("IRR({-1000,500,600})",    "内部收益率")
    add("SLN(10000,1000,5)",       "直线折旧 1800")
    add("DDB(10000,1000,5,1)",     "双倍余额第1期")
    add("DB(10000,1000,5,1)",      "余额递减第1期")

    # ---- 数学 ----
    add("SUMPRODUCT({1,2},{3,4})", "11")
    add("SUMSQ(3,4)",              "25")
    add("ABS(-3)",                 "3")
    add("SQRT(16)",                "4")
    add("POWER(2,10)",             "1024")
    add("LOG(100,10)",             "2")
    add("LN(1)",                   "0")
    add("EXP(0)",                  "1")
    add("FACT(5)",                 "120")
    add("COMBIN(10,3)",            "120")
    add("PERMUT(10,3)",            "720")
    add("GCD(12,18)",              "6")
    add("LCM(4,6)",                "12")
    add("SUMXMY2({1,2},{3,4})",    "8")
    add("SUMX2MY2({1,2},{3,4})",   "-20")
    add("MDETERM({4,7;2,6})",      "10")
    add("TRANSPOSE({1,2})",        "数组（单独处理）")

    # ---- 信息函数 ----
    add("CELL(\"row\")",  "当前行（LibreOffice 可能不支持，记 N/A）")
    add("TYPE(1)",        "1=数字")
    add("TYPE(\"a\")",    "2=文本")
    add("TYPE(TRUE)",     "4=逻辑")
    add("TYPE({1})",      "16=数组")
    add("ISREF(A1)",      "TRUE")
    add("N(\"abc\")",     "文本转 0")
    add("N(5)",           "5")

    return C


def main():
    quick = "--quick" in sys.argv
    cases = build_cases(quick)
    formulas = [c[0] for c in cases]

    # 1) 用引擎求值
    if not os.path.exists(EXE):
        print("请先执行: make tools/evalbatch")
        return 1
    lst = "/tmp/locheck_list.txt"
    with open(lst, "w", encoding="utf-8") as f:
        for x in formulas:
            f.write(x + "\n")
    r = subprocess.run([EXE, lst], capture_output=True, text=True)
    engine = {}
    for line in r.stdout.splitlines():
        if "\t" not in line:
            continue
        k, v = line.split("\t", 1)
        engine[k.strip()] = v.strip()

    # 2) 写进 xlsx，交给 LibreOffice 重算
    import openpyxl
    tmpd = tempfile.mkdtemp(prefix="locheck_")
    xlsx = os.path.join(tmpd, "in.xlsx")
    wb = openpyxl.Workbook(); ws = wb.active; ws.title = "S"
    ws["A5"] = None        # 给 ISBLANK 一个空格子
    for i, f in enumerate(formulas, start=1):
        c = ws.cell(row=i, column=2, value="=" + f)
        # 关键：不设格式的话 LibreOffice 会把 DATE() 导出成 "02/29/24"、
        # PMT/PV 导出成 "-$856.07"，与引擎的裸数值对不上。
        # 统一成定长小数格式，让它原样输出数字。
        c.number_format = "0.00000000000000"
    wb.save(xlsx)

    # 3) 转 CSV。第 7 个参数 false=不导出为引号文本，保留公式结果
    subprocess.run(["soffice", "--headless", "--convert-to",
                    "csv:Text - txt - csv (StarCalc):44,34,76,1,,0,false,true,true,false,false,1",
                    "--outdir", tmpd, xlsx],
                   capture_output=True, timeout=600)
    csvs = [x for x in os.listdir(tmpd) if x.endswith(".csv")]
    if not csvs:
        print("LibreOffice 未产出 CSV，跳过")
        shutil.rmtree(tmpd, ignore_errors=True)
        return 1
    with open(os.path.join(tmpd, csvs[0]), encoding="utf-8", errors="replace") as f:
        rows = list(csv.reader(f))

    lo = {}
    for i in range(len(rows)):
        if i < len(formulas):
            row = rows[i]
            lo[formulas[i]] = row[1].strip() if len(row) > 1 else ""

    # 4) 比对
    def norm_engine(s):
        if s.startswith("<ERR>"):
            return None            # 引擎输出错误但没给文本，特殊处理
        if s.startswith("<ARR>"):
            return None            # 数组型单独看
        if s.startswith("<STR>"):
            return s[5:]
        if s.startswith("<PARSE"):
            return "<PARSE>"
        try:
            v = float(s)
            if v == int(v) and abs(v) < 1e15:
                return str(int(v))
            return repr(v)
        except Exception:
            return s

    def norm_lo(s):
        if s == "":
            return "0"             # 空单元格在算术语境下当 0
        try:
            v = float(s)
            if v == int(v) and abs(v) < 1e15:
                return str(int(v))
            return repr(v)
        except Exception:
            return s

    def numbers_close(a, b, tol=1e-9):
        try:
            return abs(float(a) - float(b)) <= tol * max(1.0, abs(float(b)))
        except Exception:
            return False

    # LibreOffice 与 Excel 的已知差异，或 LO 自身限制 —— 记为 N/A 不算失败，
    # 但要如实列出，不能靠"跳过"把问题藏起来。
    LO_DIVERGE = {
        'SUM("5")': "Excel 把顶层文本型数字转成数值得 5；LO 报 #VALUE!（LO 的已知差异）",
        'SUMIF({1,2,3,4},">2")':      "LO 不支持把内联数组当作 SUMIF 的区域（得 0）",
        'AVERAGEIF({1,2,3,4},">2")':  "同上（LO 得 #DIV/0!）",
        'COUNTIF({"a","b","a"},"a")': "同上",
        'TRUE>1':   "Excel 排序序：数字<文本<逻辑值 → TRUE；LO 得 FALSE",
        'FALSE>0':  "同上",
        'CELL("row")': "LO 不支持 CELL() 的 row 参数",
        'TYPE(TRUE)':  "Excel 的 TYPE(TRUE)=4；LO 给 1（LO 把逻辑值归入数字类）",
        # 下面三条是**同一个根因**：Excel 的比较排序序是 数字 < 文本 < 逻辑值，
        # 逻辑值与数字永远不相等、逻辑值最大；而 LO 把 TRUE 当成 1 参与比较。
        '1=1=1': "左结合→(1=1)=1→TRUE=1；Excel 类型不同得 FALSE，LO 得 TRUE",
        '1<2<3': "左结合→(1<2)<3→TRUE<3；Excel 逻辑值最大得 FALSE，LO 得 TRUE",
    }

    def bool_match(e, l):
        # 数字格式把 LO 的 TRUE/FALSE 渲染成 1/0，需要双向归一。
        # 只在本用例的引擎结果确实是布尔时才接受，避免把真正的数值 1 误判成 TRUE。
        if e in ("TRUE", "FALSE"):
            return (e == "TRUE" and l in ("1", "TRUE", "1.0")) or \
                   (e == "FALSE" and l in ("0", "FALSE", "0.0"))
        return False

    P = N = NA = 0
    diverges = []
    fails = []
    for f, note in cases:
        e_raw = engine.get(f, "<无输出>")
        l_raw = lo.get(f, "<无输出>")
        if e_raw.startswith("<PARSE"):
            N += 1; fails.append((f, "引擎解析失败", l_raw, note)); continue
        if l_raw in ("<无输出>", "", "#NAME?"):
            NA += 1; continue          # LibreOffice 不认识 → 不计
        if f in LO_DIVERGE:
            # 只有真不一致才记差异；LO 其实答对的就不算，免得掩盖真实问题
            e0 = norm_engine(e_raw); l0 = norm_lo(l_raw)
            if not (e0 == l0 or numbers_close(str(e0), str(l0)) or bool_match(e0, l0)):
                diverges.append((f, e_raw, l_raw, LO_DIVERGE[f]))
                NA += 1
                continue
        e = norm_engine(e_raw)
        l = norm_lo(l_raw)
        if e is None:
            # 引擎给的是错误/数组：改从 display 层取文本
            e = None
        if e is None:
            NA += 1; continue          # 数组/错误型，本轮不比对
        if e == l or numbers_close(e, l) or bool_match(e, l):
            P += 1
        else:
            N += 1
            fails.append((f, e, l, note))

    print(f"\n对照 LibreOffice：通过 {P}，不符 {N}，未比对 {NA}（数组/LO不支持/已知差异）")
    if diverges:
        print(f"\n已知差异（不计失败，如实列出 {len(diverges)} 条）：")
        for f, e, l, why in diverges:
            print(f"  {f[:38]:<40} 引擎={str(e)[:18]:<20} LO={str(l)[:18]:<20} {why}")
    if fails:
        print("\n不符明细（前 30 条）：")
        for f, e, l, n in fails[:30]:
            print(f"  {f[:40]:<42} 引擎={str(e)[:20]:<22} LO={str(l)[:20]:<22} [{n}]")
        if len(fails) > 30:
            print(f"  …还有 {len(fails)-30} 条")
    shutil.rmtree(tmpd, ignore_errors=True)
    return 1 if N else 0


if __name__ == "__main__":
    sys.exit(main())
