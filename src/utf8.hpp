#pragma once
// ---------------------------------------------------------------------------
// 文本函数的**字符级**（码点级）操作
// ---------------------------------------------------------------------------
// LEN / LEFT / RIGHT / MID / REPLACE 原先直接对 std::string 按**字节**截取，
// 用在中文上会出两个问题：
//   LEN("中文abc")        = 9        ← 3+3+3 字节，应为 5（字符数）
//   LEFT("中文abc",2)     = "\xe4\xb8"  ← 把汉字切成半个，输出乱码
//   REPLACE("中文abc",1,2,"X") = "X\xb8文abc" ← 同样乱码，正确是 "Xabc"
// Excel 的这些函数一律按**字符**计数，所以必须先定位每个字符的字节边界再操作。
//
// 用法与 std::string 的 substr/replace 一一对应，只是下标单位从字节换成字符。
#include <string>
#include <vector>

namespace xl {
namespace utf {

// 每个字符在原始串中的字节起始偏移，末尾附一个哨兵 = size()
inline std::vector<size_t> offsets(const std::string& s) {
    std::vector<size_t> off;
    size_t i = 0;
    while (i < s.size()) {
        off.push_back(i);
        unsigned char c = (unsigned char)s[i];
        int n;
        if      (c < 0x80)           n = 0;
        else if ((c & 0xE0) == 0xC0) n = 1;
        else if ((c & 0xF0) == 0xE0) n = 2;
        else if ((c & 0xF8) == 0xF0) n = 3;
        else                         n = 0;   // 非法首字节：按单字节推进
        if (i + (size_t)n >= s.size()) { i++; continue; }
        bool ok = true;
        for (int k = 1; k <= n; k++)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) { ok = false; break; }
        i += ok ? (size_t)n + 1 : 1;
    }
    off.push_back(s.size());
    return off;
}

inline size_t len(const std::string& s) {
    std::vector<size_t> o = offsets(s);
    return o.empty() ? 0 : o.size() - 1;
}

// 取第 start 个字符起 count 个字符（start 从 0 计）
inline std::string slice(const std::string& s, size_t start, size_t count) {
    std::vector<size_t> o = offsets(s);
    size_t N = o.empty() ? 0 : o.size() - 1;
    if (start >= N) return "";
    size_t eIdx = start + count;
    if (eIdx >= N) eIdx = N;
    return s.substr(o[start], o[eIdx] - o[start]);
}

// 从第 st 个字符起删除 count 个，并在该处插入 ins（st 从 0 计）
inline std::string splice(const std::string& s, size_t st, size_t count, const std::string& ins) {
    std::vector<size_t> o = offsets(s);
    size_t N = o.empty() ? 0 : o.size() - 1;
    if (st > N) st = N;
    size_t eIdx = st + count;
    if (eIdx >= N) eIdx = N;
    return s.substr(0, o[st]) + ins + s.substr(o[eIdx]);
}

} // namespace utf
} // namespace xl
