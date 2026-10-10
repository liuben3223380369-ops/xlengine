// 可真实调用的 jni.h 桩：jstring 内部就是 std::string，
// 这样桌面测试能真正跑 JNI 函数的逻辑（不只是语法检查）。
//
// 放在仓库外的持久化目录：/tmp 会被沙盒重置清掉。
#pragma once
#include <string>
#include <cstddef>

struct _jobject { std::string s; };
typedef _jobject* jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef int jint;

struct JNIEnv {
    const char* GetStringUTFChars(jstring v, void*) { return v ? v->s.c_str() : ""; }
    void ReleaseStringUTFChars(jstring, const char*) {}
    jstring NewStringUTF(const char* p) { return new _jobject{std::string(p ? p : "")}; }
};
#define JNIEXPORT extern "C"
#define JNICALL
