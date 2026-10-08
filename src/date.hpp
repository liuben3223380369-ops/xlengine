#pragma once
// ---------------------------------------------------------------------------
// 日期系统。
// Excel 有两个必须复刻的历史包袱：
//   1) 1900 日期系统把 1900 年错误地当作闰年 —— 序列号 60 = 1900-02-29，
//      而这一天在真实日历里并不存在。微软明确说明这是兼容性承诺，
//      自研时"修正"它会导致历史工作簿整体差一天。
//   2) 存在 1904 日期系统（Mac 遗产），0 = 1904-01-01。
// ---------------------------------------------------------------------------
#include <cmath>
#include <string>
#include <tuple>
#include <ctime>
#include <algorithm>
#include "value.hpp"

namespace xl {

// Howard Hinnant 的 civil<->days 算法：返回自 1970-01-01 起的天数
inline long long daysFromCivil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<int>(doe) - 719468;
}

inline void civilFromDays(long long z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long yy = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = static_cast<int>(yy + (m <= 2));
}

struct YMD { int y; int m; int d; };

// 1900 系统的关键锚点
inline long long anchor1900_01_01() { return daysFromCivil(1900, 1, 1); }
inline long long anchor1900_03_01() { return daysFromCivil(1900, 3, 1); }
inline long long anchor1904_01_01() { return daysFromCivil(1904, 1, 1); }

// 序列号 -> 年月日（1900 系统）
inline YMD serialToYMD(double serial) {
    long long s = (long long)std::floor(serial);
    long long days;
    if (s >= 61) {
        days = anchor1900_03_01() + (s - 61);
    } else if (s >= 1) {
        days = anchor1900_01_01() + (s - 1);
        if (s == 60) return YMD{1900, 2, 29};      // Excel 的假闰日
    } else {
        days = anchor1900_01_01() + (s - 1);       // s=0 -> 1899-12-31
    }
    int y; unsigned m, d;
    civilFromDays(days, y, m, d);
    return YMD{y, (int)m, (int)d};
}

// 年月日 -> 序列号（1900 系统）
inline double ymdToSerial(int y, int m, int d) {
    if (y == 1900 && m == 2 && d == 29) return 60.0;    // 假闰日
    long long days = daysFromCivil(y, (unsigned)m, (unsigned)d);
    if (days < anchor1900_03_01()) {
        return (double)(days - anchor1900_01_01() + 1);
    }
    return (double)(61 + (days - anchor1900_03_01()));
}

inline bool isLeapY(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}
inline int daysInMonth(int y, int m) {
    static const int tbl[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && isLeapY(y)) return 29;
    if (m < 1 || m > 12) return 0;
    return tbl[m - 1];
}

// 星期几：0=周日 1=周一 ... 6=周六
// Excel 序列号 -> epoch days（1970-01-01 = 0）。
// 必须显式处理 1900 假闰日：序列号 61 才是真实的 1900-03-01，
// 直接减常数会让所有日期在 1900 年附近整体错位一天。
// 漏掉这步的表现很隐蔽：weekdayOf 算出来的星期对所有日期都偏 5 天
// （25569 % 7 = 5），于是工作日函数会把周二当周末。
inline long long serialToEpochDays(double serial) {
    long long s = (long long)std::floor(serial);
    if (s >= 61) return anchor1900_03_01() + (s - 61);
    return anchor1900_01_01() + (s - 1);
}

inline int weekdayOf(long long days) {
    // 1970-01-01 是周四
    long long w = (days + 4) % 7;
    if (w < 0) w += 7;
    return (int)w;
}

// ---------------------------------------------------------------------------
// 日计数基准（财务函数的 basis 参数）
//   0 / 省略 = 30/360 (US NASD)
//   1        = actual/actual
//   2        = actual/360
//   3        = actual/365
//   4        = 30/360 欧洲
// ---------------------------------------------------------------------------
inline int basisOf(const std::vector<Value>& args, size_t idx) {
    if (args.size() <= idx) return 0;
    double b; Value e;
    if (!toNumber(args[idx], b, e)) return 0;
    int bi = (int)std::floor(b);
    return (bi < 0 || bi > 4) ? -1 : bi;     // -1 表示非法
}

// 30/360 (US NASD)：差值天数
// 入参是 **Excel 序列号**，不是 civilFromDays 的天数。
//
// 原实现直接把序列号喂给 civilFromDays —— 两者相差一个常数（25569 天，
// 即 1970-01-01 的序列号），于是解出来的年月日整体平移约 70 年。
// 因为平移不是整年数，日/月分量会错位，30/360 的结果随之偏移：
//   实测 YEARFRAC(2020-1-1, 2021-1-1, 0) = 361/360 = 1.00278（应为 1）
//   而平年跨度 2021→2022 恰好得 360，掩盖了这个 bug —— 只在闰年暴露。
inline double days360(long long d1, long long d2, bool european) {
    YMD p1 = serialToYMD((double)d1), p2 = serialToYMD((double)d2);
    int y1 = p1.y, y2 = p2.y; unsigned m1 = p1.m, m2 = p2.m;
    int a1 = (int)p1.d, a2 = (int)p2.d;
    if (european) {
        a1 = std::min(a1, 30);
        a2 = std::min(a2, 30);
    } else {
        if (a1 == 31) a1 = 30;
        if (a2 == 31 && a1 == 30) a2 = 30;
    }
    return (y2 - y1) * 360.0 + ((int)m2 - (int)m1) * 30.0 + (a2 - a1);
}

// 年分数：两个序列号之间的年数，按 basis
inline double yearFrac(double s1, double s2, int basis) {
    if (s1 > s2) std::swap(s1, s2);
    long long a = (long long)std::floor(s1);
    long long b = (long long)std::floor(s2);
    switch (basis) {
        case 0: case 4: return days360(a, b, basis == 4) / 360.0;
        case 1: {
            // actual/actual：按闰年天数加权
            YMD ya = serialToYMD(s1), yb = serialToYMD(s2);
            if (ya.y == yb.y) return (b - a) / (isLeapY(ya.y) ? 366.0 : 365.0);
            double total = 0;
            // 首年剩余
            long long yearEnd = daysFromCivil(ya.y, 12, 31);
            long long dayStart = daysFromCivil(ya.y, (unsigned)ya.m, (unsigned)ya.d);
            total += (yearEnd - dayStart + 1) / (isLeapY(ya.y) ? 366.0 : 365.0);
            // 中间整年
            for (int y = ya.y + 1; y < yb.y; y++) total += 1.0;
            // 末年已过
            long long yearStart = daysFromCivil(yb.y, 1, 1);
            long long dayEnd = daysFromCivil(yb.y, (unsigned)yb.m, (unsigned)yb.d);
            total += (dayEnd - yearStart) / (isLeapY(yb.y) ? 366.0 : 365.0);
            return total;
        }
        case 2: return (b - a) / 360.0;
        case 3: return (b - a) / 365.0;
    }
    return 0;
}

// 日期序列号解析：接受数字序列号或文本日期 "2024-01-15" / "2024/1/15"
inline bool valueToSerial(const Value& v, double& out) {
    if (v.isError()) return false;
    if (v.isNum()) { out = v.n; return true; }
    if (v.isEmpty()) { out = 0; return true; }
    std::string s; Value e;
    if (!toText(v, s, e)) return false;
    // 先试：纯数字文本
    double d;
    if (textToNumber(s, d)) { out = d; return true; }
    // 解析 Y-M-D
    int y = 0, m = 0, dd = 0;
    std::string num;
    std::vector<int> parts;
    for (size_t i = 0; i <= s.size(); i++) {
        char c = (i < s.size()) ? s[i] : '\0';
        if (std::isdigit((unsigned char)c)) { num.push_back(c); continue; }
        if (c == '-' || c == '/' || c == '.' || c == '\0') {
            if (!num.empty()) { parts.push_back(std::stoi(num)); num.clear(); }
            if (c == '\0') break;
            continue;
        }
        return false;
    }
    if (parts.size() != 3) return false;
    y = parts[0]; m = parts[1]; dd = parts[2];
    if (y < 100) y += (y < 30) ? 2000 : 1900;
    if (m < 1 || m > 12 || dd < 1 || dd > daysInMonth(y, m)) return false;
    out = ymdToSerial(y, m, dd);
    return true;
}

// 固定"今天"，保证测试可复现；可通过 setTodaySerial 注入
inline double& todaySerialRef() {
    static double s = 0;      // 0 = 使用系统时间
    return s;
}
inline double nowSerial() {
    double fixed = todaySerialRef();
    if (fixed > 0) return fixed;
    std::time_t t = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    // 同上：不用 gmtime_s，避免 MSVC / C11 签名分歧
    if (std::tm* p = gmtime(&t)) utc = *p;
#else
    gmtime_r(&t, &utc);
#endif
    long long days = daysFromCivil(utc.tm_year + 1900, (unsigned)(utc.tm_mon + 1), (unsigned)utc.tm_mday);
    double serial;
    if (days < anchor1900_03_01()) serial = (double)(days - anchor1900_01_01() + 1);
    else serial = (double)(61 + (days - anchor1900_03_01()));
    return serial + (utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec) / 86400.0;
}

} // namespace xl
