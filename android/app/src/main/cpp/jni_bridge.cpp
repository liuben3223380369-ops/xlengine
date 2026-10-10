#include <jni.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sheet.hpp"
#include "xlsx.hpp"

// ---------------------------------------------------------------------------
// JNI 桥接：把 C++ 引擎暴露给 Java
// ---------------------------------------------------------------------------
// 引擎核心与终端无关，Android 上直接复用。
//
// 这里曾经只暴露一个 eval() —— 于是 APK 打开是个"公式输入框"，而不是表格
// 软件。真正要成为表格 App，必须把**整个工作簿**暴露出来：多表、网格读写、
// 打开/保存 xlsx、重算。界面层（WebView 里的 JS 网格）才能建立在这些之上。
//
// 设计取舍：
//   - 状态放在一个全局 Workbook 上（Android 端单文档足够），
//     而不是在 Java 侧持有指针来回传 —— 少一层生命周期管理的出错可能。
//   - 返回 String / int 这类基本类型，不返回对象：JNI 里构造 Java 对象
//     很啰嗦，而网格数据本来就适合用 JSON 一次性传。
namespace {

// 全局工作簿。JNI 的多个方法之间要保持状态，没有它就没法"打开后编辑再保存"。
std::unique_ptr<xl::Workbook> g_wb;
int g_sheet = 0;
std::string g_err;

xl::Workbook& wb() {
    if (!g_wb) {
        g_wb.reset(new xl::Workbook());
        // Workbook 默认构造后至少要有一张表，否则界面上是一片空白
        if (g_wb->sheetCount() == 0) g_wb->addSheet("Sheet1");
        g_sheet = 0;
    }
    return *g_wb;
}

xl::Sheet* cur() {
    xl::Workbook& w = wb();
    if (g_sheet < 0) g_sheet = 0;
    if ((size_t)g_sheet >= w.sheetCount()) g_sheet = (int)w.sheetCount() - 1;
    if (g_sheet < 0) return nullptr;
    return &w.sheet((size_t)g_sheet);
}

jstring js(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

// JSON 字符串转义。
// 注意控制字符必须转成 \u00XX —— 直接输出裸换行会让 JSON 非法，
// 而 JS 侧 JSON.parse 抛错的表现是"整个网格空白"，很难往这边查。
std::string jesc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    o += "\\u00";
                    o += hex[(c >> 4) & 0xF];
                    o += hex[c & 0xF];
                } else {
                    o += (char)c;
                }
        }
    }
    return o;
}

// 判断用户输入该当数字、公式还是文本。
//
// 这一步不能省：直接 setValue 成字符串的话，输入 123 会得到文本 "123"，
// 之后 =A1+1 算不出来。Excel 的行为就是"看着像数字就当数字"。
bool setCellSmart(xl::Sheet& sh, int c, int r, const std::string& t) {
    if (t.empty()) { sh.setValue(c, r, xl::Value::empty()); return true; }

    // 去掉前导空白再判断，否则 " =1+1" 会被当成文本
    size_t b = t.find_first_not_of(" \t");
    if (b == std::string::npos) { sh.setValue(c, r, xl::Value::empty()); return true; }
    std::string s = t.substr(b);

    if (s[0] == '=') {
        // 引擎存的是不含 '=' 的公式原文，写出去时再补 —— 存了 '=' 会变成 ==SUM()
        std::string e = sh.setFormula(c, r, s.substr(1));
        if (!e.empty()) { g_err = e; return false; }
        return true;
    }

    // 数字：只认常规写法，不接受 "1e5" 之外的怪形式由引擎负责
    char* endp = nullptr;
    double d = ::strtod(s.c_str(), &endp);
    if (endp && *endp == '\0' && !s.empty()) {
        // strtod 会把 "nan"/"inf" 也吃掉，那不该当数字存
        std::string low = s;
        for (char& ch : low) if (ch >= 'A' && ch <= 'Z') ch += 32;
        if (low != "nan" && low != "inf" && low != "-inf" && low != "infinity") {
            sh.setValue(c, r, xl::Value::num(d));
            return true;
        }
    }

    sh.setValue(c, r, xl::Value::str(t));
    return true;
}

}  // namespace

extern "C" {

// ---- 工作簿 ----

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_wbNew(JNIEnv*, jclass) {
    g_wb.reset(new xl::Workbook());
    // Workbook() 默认已带一张表，这里无条件 addSheet 会变成两张 ——
    // 界面上多出一个空表签，用户会以为打开的文件有问题。
    if (g_wb->sheetCount() == 0) g_wb->addSheet("Sheet1");
    g_sheet = 0;
    g_err.clear();
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_wbLoad(JNIEnv* env, jclass, jstring jp) {
    if (!jp) return 1;
    const char* c = env->GetStringUTFChars(jp, nullptr);
    std::string p(c ? c : "");
    if (c) env->ReleaseStringUTFChars(jp, c);

    g_wb.reset(new xl::Workbook());
    std::string err;
    if (!g_wb->load(p, err)) {
        // 加载失败不能留一个空工作簿继续跑，否则用户以为"打开成功但内容没了"
        g_wb.reset(new xl::Workbook());
        g_wb->addSheet("Sheet1");
        g_sheet = 0;
        g_err = err.empty() ? "加载失败" : err;
        return 1;
    }
    if (g_wb->sheetCount() == 0) g_wb->addSheet("Sheet1");
    g_sheet = 0;
    g_err.clear();
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_wbSave(JNIEnv* env, jclass, jstring jp) {
    if (!jp) return 1;
    const char* c = env->GetStringUTFChars(jp, nullptr);
    std::string p(c ? c : "");
    if (c) env->ReleaseStringUTFChars(jp, c);

    std::string err;
    if (!wb().save(p, err)) {
        g_err = err.empty() ? "保存失败" : err;
        return 1;
    }
    g_err.clear();
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_xlengine_app_Engine_lastError(JNIEnv* env, jclass) {
    return js(env, g_err);
}

// ---- 工作表 ----

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_sheetCount(JNIEnv*, jclass) {
    return (jint)wb().sheetCount();
}

JNIEXPORT jstring JNICALL
Java_com_xlengine_app_Engine_sheetName(JNIEnv* env, jclass, jint i) {
    auto ns = wb().sheetNames();
    if (i < 0 || (size_t)i >= ns.size()) return js(env, "");
    return js(env, ns[(size_t)i]);
}

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_setSheet(JNIEnv*, jclass, jint i) {
    if (i < 0 || (size_t)i >= wb().sheetCount()) return 1;
    g_sheet = (int)i;
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_addSheet(JNIEnv* env, jclass, jstring jn) {
    const char* c = jn ? env->GetStringUTFChars(jn, nullptr) : nullptr;
    std::string n(c ? c : "");
    if (c) env->ReleaseStringUTFChars(jn, c);
    if (n.empty()) n = "Sheet" + std::to_string(wb().sheetCount() + 1);
    wb().addSheet(n);
    g_sheet = (int)wb().sheetCount() - 1;
    return g_sheet;
}

// ---- 单元格 ----

JNIEXPORT jstring JNICALL
Java_com_xlengine_app_Engine_cellRaw(JNIEnv* env, jclass, jint c, jint r) {
    xl::Sheet* s = cur();
    if (!s) return js(env, "");
    //
    // 必须是"公式原文 + 前导 ="，不能用 displayRaw。
    // displayRaw 是"不套数字格式的显示值"（比如 1234.5 而不是 1,234.5），
    // 对公式格它返回的是**算出来的值**。拿它回填编辑框的话，
    // 用户点一下格子再点走，公式就被写死成常量了 —— 而且不报错。
    //
    std::string f;
    if (s->cellFormula(c, r, f) && !f.empty()) return js(env, "=" + f);
    return js(env, s->displayRaw(c, r));
}

JNIEXPORT jstring JNICALL
Java_com_xlengine_app_Engine_cellText(JNIEnv* env, jclass, jint c, jint r) {
    xl::Sheet* s = cur();
    return js(env, s ? s->display(c, r) : "");
}

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_setCell(JNIEnv* env, jclass, jint c, jint r, jstring jt) {
    xl::Sheet* s = cur();
    if (!s) { g_err = "没有工作表"; return 1; }
    const char* p = jt ? env->GetStringUTFChars(jt, nullptr) : nullptr;
    std::string t(p ? p : "");
    if (p) env->ReleaseStringUTFChars(jt, p);

    try {
        if (!setCellSmart(*s, c, r, t)) return 1;
    } catch (const std::exception& e) {
        g_err = std::string("异常: ") + e.what();
        return 1;
    }
    g_err.clear();
    return 0;
}

// 增量重算：只算脏的那部分。
// 每次改一格都全量重算的话，手机上几千条公式会明显卡顿。
JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_recalc(JNIEnv*, jclass) {
    xl::Sheet* s = cur();
    if (!s) return 1;
    try {
        s->recalcDirty();
    } catch (const std::exception& e) {
        g_err = std::string("异常: ") + e.what();
        return 1;
    }
    return 0;
}

// ---- 网格批量读取 ----
//
// 逐格 JNI 调用在手机上很慢（一次调用几十微秒，100 个可见格子就是几毫秒，
// 滑动时直接掉帧）。所以一次把整个可见区域取回来。
JNIEXPORT jstring JNICALL
Java_com_xlengine_app_Engine_grid(JNIEnv* env, jclass,
                                  jint c0, jint r0, jint c1, jint r1) {
    xl::Sheet* s = cur();
    if (!s) return js(env, "[]");

    std::string o = "[";
    for (int r = r0; r <= r1; r++) {
        if (r > r0) o += ",";
        o += "[";
        for (int c = c0; c <= c1; c++) {
            if (c > c0) o += ",";
            o += "\"" + jesc(s->display(c, r)) + "\"";
        }
        o += "]";
    }
    o += "]";
    return js(env, o);
}

// "c0,r0,c1,r1"，没有内容时返回空串
JNIEXPORT jstring JNICALL
Java_com_xlengine_app_Engine_usedRange(JNIEnv* env, jclass) {
    xl::Sheet* s = cur();
    if (!s) return js(env, "");
    int c0, r0, c1, r1;
    if (!s->usedRange(s->name(), c0, r0, c1, r1)) return js(env, "");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d", c0, r0, c1, r1);
    return js(env, buf);
}

// ---- 结构性编辑 ----

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_insertRows(JNIEnv*, jclass, jint at, jint n) {
    xl::Sheet* s = cur();
    if (!s) return 1;
    s->insertRows(at, n);
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_deleteRows(JNIEnv*, jclass, jint at, jint n) {
    xl::Sheet* s = cur();
    if (!s) return 1;
    s->deleteRows(at, n);
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_insertCols(JNIEnv*, jclass, jint at, jint n) {
    xl::Sheet* s = cur();
    if (!s) return 1;
    s->insertCols(at, n);
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_xlengine_app_Engine_deleteCols(JNIEnv*, jclass, jint at, jint n) {
    xl::Sheet* s = cur();
    if (!s) return 1;
    s->deleteCols(at, n);
    return 0;
}

// ---- 保留原有的公式求值 ----
//
// 界面上有"快速求值"入口，也给刚装上的用户一个最直接的验证途径。
JNIEXPORT jstring JNICALL
Java_com_xlengine_app_Engine_eval(JNIEnv* env, jclass, jstring jf) {
    if (!jf) return env->NewStringUTF("");
    const char* c = env->GetStringUTFChars(jf, nullptr);
    if (!c) return env->NewStringUTF("");
    std::string f(c);
    env->ReleaseStringUTFChars(jf, c);

    size_t b = f.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return env->NewStringUTF("");
    f = f.substr(b);
    if (!f.empty() && f[0] == '=') f = f.substr(1);

    std::string out;
    try {
        xl::Sheet s2;
        s2.setName("A");
        std::string e = s2.setFormula(20, 20, f);
        if (!e.empty()) return env->NewStringUTF(e.c_str());
        s2.recalc();
        out = s2.display(20, 20);
    } catch (const std::exception& e) {
        out = std::string("异常: ") + e.what();
    } catch (...) {
        out = "异常: 未知错误";
    }
    return env->NewStringUTF(out.c_str());
}

}  // extern "C"
