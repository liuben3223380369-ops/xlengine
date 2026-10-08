#include <jni.h>
#include <string>

#include "sheet.hpp"

// ---------------------------------------------------------------------------
// JNI 桥接：把 C++ 引擎暴露给 Java
// ---------------------------------------------------------------------------
// 引擎核心与终端无关，Android 上直接复用；唯一的适配点是把"公式文本进、
// 显示文本出"封装成一个 native 方法。
namespace {

// 把公式写在 U21（列索引 20、行索引 20）——刻意选一个远离左上角的格子，
// 避免和用户可能引用的 A1:B10 之类区域撞车。
std::string evalOne(const std::string& raw) {
    std::string f = raw;

    // 去掉前导空格与可选的 '='：引擎接受裸公式（如 "SUM({1,2,3})"），
    // 界面上让用户两种写法都可以。
    size_t b = f.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    f = f.substr(b);
    if (!f.empty() && f[0] == '=') f = f.substr(1);

    xl::Sheet s;
    s.setName("A");
    const int col = 20;
    const int row = 20;

    std::string err = s.setFormula(col, row, f);
    if (!err.empty()) return err;   // 语法/词法错误

    s.recalc();
    return s.display(col, row);     // 出错时是 #DIV/0! 这样的 Excel 风格文本
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_xlengine_app_Engine_eval(JNIEnv* env, jclass, jstring jf) {
    if (!jf) return env->NewStringUTF("");

    const char* c = env->GetStringUTFChars(jf, nullptr);
    if (!c) return env->NewStringUTF("");
    std::string f(c);
    env->ReleaseStringUTFChars(jf, c);

    std::string out;
    // 引擎在极端输入上理论上仍可能抛异常（JNI 层抛出会让进程崩溃），
    // 这里兜住并转成文本，避免整个 app 挂掉。
    try {
        out = evalOne(f);
    } catch (const std::exception& e) {
        out = std::string("异常: ") + e.what();
    } catch (...) {
        out = "异常: 未知错误";
    }

    return env->NewStringUTF(out.c_str());
}
