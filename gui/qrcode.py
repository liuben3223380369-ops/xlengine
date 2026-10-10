"""
二维码生成。

用 segno（纯 Python、零依赖、LGPL）生成，而不是自己实现 QR 编码 ——
理由是验证：QR 编码涉及 Reed-Solomon 纠错、8 种掩码的惩罚评分、
格式信息 BCH 校验，任何一步算错的结果都是"生成一个看起来像二维码
但扫不出来的图"。自己写没法验证，用成熟库才能真正确认可用。

这也是本项目一贯的原则：**能验证才叫做完**。

segno 不可用时整个模块不可用，调用方需处理 ImportError。
"""

SUPPORTED = None


def available():
    try:
        import segno  # noqa: F401
        return True
    except ImportError:
        return False


def make_png(text, scale=8, border=2, dark="#000000", light="#ffffff"):
    """
    生成二维码 PNG 字节。失败返回 (None, 错误信息)。
    
    返回 (bytes|None, str) —— 用元组而不是抛异常，
    因为调用方（界面）需要把错误显示给用户，而不是崩掉。
    """
    try:
        import io
        import segno
    except ImportError as e:
        return (None, "缺少 segno 库: %s" % e)

    text = (text or "").strip()
    if not text:
        return (None, "内容为空")

    try:
        # micro=False 很关键：segno 默认允许 Micro QR（最小 11x11），
        # 它只有一个定位角，部分扫码器识别不了。
        # 强制走标准 QR（版本1 起 21x21），兼容性最好。
        q = segno.make(text, error="m", micro=False)
        buf = io.BytesIO()
        q.save(buf, kind="png", scale=scale, border=border,
               dark=dark, light=light)
        return (buf.getvalue(), "")
    except Exception as e:
        return (None, "%s: %s" % (type(e).__name__, e))


def make_matrix(text):
    """返回二维布尔矩阵（True=深色模块），供测试用。"""
    try:
        import segno
    except ImportError:
        return None
    q = segno.make(text, error="m", micro=False)
    return [[bool(b) for b in row] for row in q.matrix]


def size_of(text):
    """二维码边长（模块数）。"""
    m = make_matrix(text)
    return len(m) if m else 0
