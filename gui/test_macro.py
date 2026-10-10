"""宏系统自检 —— 重点验证 AST 安全检查能拦住正则黑名单拦不住的东西。"""

import sys


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  [OK]   %s" % name)
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


PASS = 0
FAIL = 0


def main():
    from gui.macro import check_safety, parse_addr, parse_range, MacroStore

    print("== 地址解析 ==")
    check("A1 -> (0,0)", parse_addr("A1") == (0, 0))
    check("B3 -> (1,2)", parse_addr("B3") == (1, 2))
    check("AA1 -> (26,0)", parse_addr("AA1") == (26, 0))
    check("小写 b3 也认", parse_addr("b3") == (1, 2))
    check("$B$3 去绝对符", parse_addr("$B$3") == (1, 2))
    check("A1:C3 区间", parse_range("A1:C3") == (0, 0, 2, 2))
    check("单格当区间", parse_range("B2") == (1, 1, 1, 1))
    check("反向区间归一化", parse_range("C3:A1") == (0, 0, 2, 2))

    print()
    print("== 正常宏应放行 ==")
    ok_codes = {
        "算术": "api.set('A1', 1 + 2 * 3)",
        "循环": "for i in range(10):\n    api.set(f'A{i+1}', i)",
        "列表推导": "xs = [i * i for i in range(5)]\napi.log(sum(xs))",
        "字符串方法": "api.set('A1', 'hello'.upper())",
        "try/except": "try:\n    v = float(api.get('A1'))\nexcept ValueError:\n    v = 0",
    }
    for n, c in ok_codes.items():
        check(n, check_safety(c) is None, str(check_safety(c)))

    print()
    print("== 危险宏应拦截 ==")
    bad_codes = {
        "import os": "import os\nos.system('rm -rf /')",
        "from os import": "from os import system\nsystem('x')",
        "eval": "eval('1+1')",
        "exec": "exec('x=1')",
        "open": "open('/etc/passwd').read()",
        "__import__": "__import__('os').system('x')",
        "__class__逃逸": "(1).__class__.__bases__[0].__subclasses__()",
        "__globals__": "api.log.__globals__",
        "__subclasses__": "int.__subclasses__()",
        "subprocess": "import subprocess\nsubprocess.run(['ls'])",
        "getattr": "getattr(api, 'x')",
        "socket": "import socket\nsocket.socket()",
        # 下面三条是**正则黑名单拦不住**的典型绕过，
        # AST 能拦住是因为它们最终都落到 Name / Attribute 节点上
        "拼字符串绕过eval": "getattr(__builtins__, 'ev' + 'al')('1+1')",
        "拼字符串绕过import": "__import__('o' + 's')",
        "encode绕过": "getattr(__builtins__, 'ex'+'ec')('x=1')",
    }
    for n, c in bad_codes.items():
        r = check_safety(c)
        check("拦截 %s" % n, r is not None, "（竟然放行了！）")

    print()
    print("== 语法错误 ==")
    check("语法错被报出", check_safety("def (:\n") is not None)

    print()
    print("== 持久化 ==")
    import tempfile, os
    p = os.path.join(tempfile.mkdtemp(), "m.json")
    st = MacroStore(p)
    ms = st.load()
    check("首次载入有示例宏", len(ms) >= 1, str(len(ms)))
    check("示例宏含 code", all("code" in m for m in ms))
    check("示例宏能过安全检查",
          all(check_safety(m["code"]) is None for m in ms))
    check("保存再读回", st.save(ms) and len(st.load()) == len(ms))

    print()
    print("通过 %d 项，失败 %d 项" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
