// JNI 桥接的逻辑验证（Android 端界面的正确性基础）
//
// 为什么需要它：
//
//   Android APK 只能在 CI 上编译，本地跑不了真机。而 JNI 层一旦写错
//   （数字被当文本存、JSON 没转义、插入行后引用没跟着走），
//   表现是"界面能开但数据不对"，在手机上很难定位。
//
//   所以用一个可调用的 jni.h 桩把 JNI 函数搬到桌面上真跑一遍。
//   桩里 jstring 就是 std::string，因此这些函数是**真的被执行**，
//   不只是语法检查。
//
// 编译：见 Makefile 的 jnitest 目标（需要 /data/workspace/jnistub/jni.h）

#include "jni.h"

#include <iostream>
#include <string>

extern "C" {
jint Java_com_xlengine_app_Engine_wbNew(JNIEnv*, jclass);
jint Java_com_xlengine_app_Engine_wbLoad(JNIEnv*, jclass, jstring);
jint Java_com_xlengine_app_Engine_wbSave(JNIEnv*, jclass, jstring);
jstring Java_com_xlengine_app_Engine_lastError(JNIEnv*, jclass);
jint Java_com_xlengine_app_Engine_sheetCount(JNIEnv*, jclass);
jstring Java_com_xlengine_app_Engine_sheetName(JNIEnv*, jclass, jint);
jint Java_com_xlengine_app_Engine_setSheet(JNIEnv*, jclass, jint);
jint Java_com_xlengine_app_Engine_addSheet(JNIEnv*, jclass, jstring);
jstring Java_com_xlengine_app_Engine_cellRaw(JNIEnv*, jclass, jint, jint);
jstring Java_com_xlengine_app_Engine_cellText(JNIEnv*, jclass, jint, jint);
jint Java_com_xlengine_app_Engine_setCell(JNIEnv*, jclass, jint, jint, jstring);
jint Java_com_xlengine_app_Engine_recalc(JNIEnv*, jclass);
jstring Java_com_xlengine_app_Engine_grid(JNIEnv*, jclass, jint, jint, jint, jint);
jstring Java_com_xlengine_app_Engine_usedRange(JNIEnv*, jclass);
jint Java_com_xlengine_app_Engine_insertRows(JNIEnv*, jclass, jint, jint);
jint Java_com_xlengine_app_Engine_deleteRows(JNIEnv*, jclass, jint, jint);
jint Java_com_xlengine_app_Engine_insertCols(JNIEnv*, jclass, jint, jint);
jint Java_com_xlengine_app_Engine_deleteCols(JNIEnv*, jclass, jint, jint);
jstring Java_com_xlengine_app_Engine_eval(JNIEnv*, jclass, jstring);
}

namespace {

JNIEnv env;
int P = 0, N = 0;

void chk(bool ok, const std::string& what, const std::string& got = "") {
    if (ok) { P++; }
    else { N++; std::cout << "  [FAIL] " << what << (got.empty() ? "" : "  得到: " + got) << "\n"; }
}

std::string S(jstring s) { return s ? s->s : ""; }
jstring J(const std::string& s) { return new _jobject{s}; }

#define C(fn) Java_com_xlengine_app_Engine_##fn

}  // namespace

int main() {
    std::cout << "== JNI 桥接逻辑验证 ==\n";

    // ---- 工作簿 ----
    chk(C(wbNew)(&env, nullptr) == 0, "wbNew");
    int sc = C(sheetCount)(&env, nullptr);
    chk(sc == 1, "新建后有 1 张表", std::to_string(sc));

    // ---- 类型判断：数字必须当数字 ----
    chk(C(setCell)(&env, nullptr, 0, 0, J("123")) == 0, "setCell 数字");
    chk(S(C(cellText)(&env, nullptr, 0, 0)) == "123", "数字显示",
        S(C(cellText)(&env, nullptr, 0, 0)));

    // ---- 公式 ----
    chk(C(setCell)(&env, nullptr, 1, 0, J("=A1*2")) == 0, "setCell 公式");
    chk(C(recalc)(&env, nullptr) == 0, "recalc");
    chk(S(C(cellText)(&env, nullptr, 1, 0)) == "246", "公式结果 246",
        S(C(cellText)(&env, nullptr, 1, 0)));
    // 原文带 '='，回填编辑框时才对
    chk(S(C(cellRaw)(&env, nullptr, 1, 0)) == "=A1*2", "原文带等号",
        S(C(cellRaw)(&env, nullptr, 1, 0)));

    // 前导空格的公式也要认
    chk(C(setCell)(&env, nullptr, 2, 0, J(" =A1+1")) == 0, "带前导空格的公式");
    chk(C(recalc)(&env, nullptr) == 0, "recalc2");
    chk(S(C(cellText)(&env, nullptr, 2, 0)) == "124", "前导空格公式结果",
        S(C(cellText)(&env, nullptr, 2, 0)));

    // ---- 文本 ----
    C(setCell)(&env, nullptr, 0, 1, J("中文abc"));
    chk(S(C(cellText)(&env, nullptr, 0, 1)) == "中文abc", "中文文本");

    // nan/inf 不该当数字存（strtod 会吃掉它们）
    C(setCell)(&env, nullptr, 0, 2, J("nan"));
    chk(S(C(cellText)(&env, nullptr, 0, 2)) == "nan", "nan 当文本",
        S(C(cellText)(&env, nullptr, 0, 2)));

    // ---- 网格 JSON ----
    std::string g = S(C(grid)(&env, nullptr, 0, 0, 1, 1));
    chk(g == "[[\"123\",\"246\"],[\"中文abc\",\"\"]]", "grid JSON", g);

    // 特殊字符必须转义。不转义的话 JSON 非法，JS 侧 JSON.parse 抛错，
    // 表现是"整个网格空白"，很难往 C++ 这边查。
    C(setCell)(&env, nullptr, 0, 3, J("a\"b\\c\n新行"));
    g = S(C(grid)(&env, nullptr, 0, 3, 0, 3));
    chk(g.find("\\\"") != std::string::npos, "引号被转义", g);
    chk(g.find("\\n") != std::string::npos, "换行被转义", g);
    chk(g.find("\\\\") != std::string::npos, "反斜杠被转义", g);

    std::string ur = S(C(usedRange)(&env, nullptr));
    chk(ur == "0,0,2,3", "usedRange", ur);

    // ---- 结构性编辑：引用要跟着数据走 ----
    // 在第 0 行插一行，原来的 A1 被挤到 A2，B1 的 =A1*2 必须变成 =A2*2。
    // 按"引用位置固定"实现的话，插个空行就会改变计算结果。
    chk(C(insertRows)(&env, nullptr, 0, 1) == 0, "insertRows");
    chk(C(recalc)(&env, nullptr) == 0, "recalc after insert");
    chk(S(C(cellText)(&env, nullptr, 1, 1)) == "246",
        "插入行后公式跟着数据走",
        S(C(cellText)(&env, nullptr, 1, 1)));

    // ---- 多表 ----
    int si = C(addSheet)(&env, nullptr, J("表2"));
    chk(si == 1, "addSheet 返回索引", std::to_string(si));
    chk(S(C(sheetName)(&env, nullptr, 1)) == "表2", "sheetName",
        S(C(sheetName)(&env, nullptr, 1)));
    chk(C(setSheet)(&env, nullptr, 1) == 0, "setSheet");
    chk(S(C(cellText)(&env, nullptr, 0, 0)) == "", "新表是空的");

    // ---- 存盘往返 ----
    chk(C(setSheet)(&env, nullptr, 0) == 0, "切回表1");
    chk(C(wbSave)(&env, nullptr, J("/tmp/jni_rt.xlsx")) == 0, "wbSave",
        S(C(lastError)(&env, nullptr)));
    chk(C(wbLoad)(&env, nullptr, J("/tmp/jni_rt.xlsx")) == 0, "wbLoad",
        S(C(lastError)(&env, nullptr)));
    chk(S(C(cellText)(&env, nullptr, 1, 1)) == "246", "往返后公式结果还在",
        S(C(cellText)(&env, nullptr, 1, 1)));

    // 加载失败不能留一个空工作簿继续跑，
    // 否则用户以为"打开成功但内容没了"
    chk(C(wbLoad)(&env, nullptr, J("/tmp/肯定不存在的文件.xlsx")) != 0,
        "加载不存在的文件应失败");
    chk(C(sheetCount)(&env, nullptr) >= 1, "失败后仍有可用表");

    // ---- eval（保留的快速求值入口） ----
    chk(S(C(eval)(&env, nullptr, J("SUM({1,2,3})"))) == "6", "eval SUM",
        S(C(eval)(&env, nullptr, J("SUM({1,2,3})"))));
    chk(S(C(eval)(&env, nullptr, J("=1/0"))) == "#DIV/0!", "eval 除零",
        S(C(eval)(&env, nullptr, J("=1/0"))));

    std::cout << "\n-------------------------------------\n";
    std::cout << "通过 " << P << " 项，失败 " << N << " 项\n";
    return N ? 1 : 0;
}
