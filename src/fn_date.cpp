#include "functions.hpp"
#include "date.hpp"
#include <sstream>
#include <iomanip>

namespace xl {

// 取数字参数，失败返回错误值
static Value numArg(const Value& v) {
    double d; Value e;
    if (!toNumber(v, d, e)) return e;
    return Value::num(d);
}
static bool argNum(const Value& v, double& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    if (!toNumber(v, out, err)) return false;
    return true;
}
static bool argInt(const Value& v, int& out, Value& err) {
    double d;
    if (!argNum(v, d, err)) return false;
    out = (int)std::floor(d);
    return true;
}

// ===========================================================================
// 日期与时间
// ===========================================================================
struct DateRegistrar {
    DateRegistrar() {
        registerFunction("DATE", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int y, m, d; Value e;
            if (!argInt(a[0], y, e)) return e;
            if (!argInt(a[1], m, e)) return e;
            if (!argInt(a[2], d, e)) return e;
            if (y < 0 || y > 9999) return Value::error(Err::Num);
            // Excel 允许月份/日期溢出：DATE(2024,13,1) = 2025-01-01
            if (y < 1900) y += 1900;            // Excel 把 0-1899 解释为 1900+year
            int mm = m - 1;
            int yy = y + mm / 12;
            mm = mm % 12; if (mm < 0) { mm += 12; yy--; }
            long long base = daysFromCivil(yy, mm + 1, 1);
            long long days = base + (d - 1);
            int ry; unsigned rm, rd;
            civilFromDays(days, ry, rm, rd);
            if (ry < 1900) return Value::error(Err::Num);
            return Value::num(ymdToSerial(ry, (int)rm, (int)rd));
        });

        registerFunction("YEAR", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; Value e;
            if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            if (s < 0) return Value::error(Err::Num);
            return Value::num((double)serialToYMD(s).y);
        });
        registerFunction("MONTH", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; Value e;
            if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            if (s < 0) return Value::error(Err::Num);
            return Value::num((double)serialToYMD(s).m);
        });
        registerFunction("DAY", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; Value e;
            if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            if (s < 0) return Value::error(Err::Num);
            return Value::num((double)serialToYMD(s).d);
        });

        registerFunction("TODAY", 0, 0, [](const std::vector<Value>&, EvalCtx&) {
            return Value::num(std::floor(nowSerial()));
        }, true);
        registerFunction("NOW", 0, 0, [](const std::vector<Value>&, EvalCtx&) {
            return Value::num(nowSerial());
        }, true);

        registerFunction("TIME", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            int h, m, s; Value e;
            if (!argInt(a[0], h, e)) return e;
            if (!argInt(a[1], m, e)) return e;
            if (!argInt(a[2], s, e)) return e;
            if (h < 0 || h > 32767 || m < 0 || m > 32767 || s < 0 || s > 32767)
                return Value::error(Err::Num);
            double frac = (h * 3600 + m * 60 + s) / 86400.0;
            // Excel 允许溢出跨天
            double whole = std::floor(frac);
            return Value::num(frac - whole > 0 ? frac : (whole > 0 ? frac : frac));
        });
        registerFunction("HOUR", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            double frac = s - std::floor(s);
            return Value::num(std::floor(frac * 24.0 + 1e-9));
        });
        registerFunction("MINUTE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            double frac = s - std::floor(s);
            return Value::num(std::floor(frac * 1440.0 + 1e-9) - std::floor(frac * 24.0) * 60.0);
        });
        registerFunction("SECOND", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            double frac = s - std::floor(s);
            return Value::num(std::floor(frac * 86400.0 + 1e-9) - std::floor(frac * 1440.0) * 60.0);
        });

        registerFunction("WEEKDAY", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            int type = 1;
            if (a.size() >= 2) { Value e; if (!argInt(a[1], type, e)) return e; }
            long long days;
            if (s >= 61) days = anchor1900_03_01() + ((long long)std::floor(s) - 61);
            else days = anchor1900_01_01() + ((long long)std::floor(s) - 1);
            int w = weekdayOf(days);          // 0=周日
            switch (type) {
                case 1:  return Value::num(w + 1);                    // 1=日 .. 7=六
                case 2:  return Value::num(w == 0 ? 7 : w);           // 1=一 .. 7=日
                case 3:  return Value::num(w == 0 ? 0 : w - 1);       // 0=一 .. 6=日
                case 11: return Value::num(w == 1 ? 1 : (w == 0 ? 7 : w));
                case 12: return Value::num(w == 2 ? 1 : (w == 0 || w == 1 ? w + 5 : w - 1));
                case 13: return Value::num(w == 3 ? 1 : (w < 3 ? w + 4 : w - 2));
                case 14: return Value::num(w >= 4 ? w - 3 : w + 3);
                case 15: return Value::num(w >= 5 ? w - 4 : w + 2);
                case 16: return Value::num(w >= 6 ? w - 5 : w + 1);
                case 17: return Value::num(w == 0 ? 1 : w + 1);
                default: return Value::error(Err::Num);
            }
        });

        registerFunction("DATEVALUE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string s; Value e;
            if (!toText(a[0], s, e)) return e;
            double out;
            if (!valueToSerial(Value::str(s), out)) return Value::error(Err::Value);
            return Value::num(std::floor(out));
        });

        registerFunction("DAYS", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s1, s2;
            if (!valueToSerial(a[0], s1) || !valueToSerial(a[1], s2)) return Value::error(Err::Value);
            return Value::num(std::floor(s1) - std::floor(s2));
        });

        registerFunction("DAYS360", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s1, s2;
            if (!valueToSerial(a[0], s1) || !valueToSerial(a[1], s2)) return Value::error(Err::Value);
            bool eu = false;
            if (a.size() >= 3) {
                Value b = numArg(a[2]); if (b.isError()) return b;
                eu = (b.n != 0);
            }
            long long d1 = (long long)std::floor(s1), d2 = (long long)std::floor(s2);
            return Value::num(days360(d1, d2, eu));
        });

        registerFunction("EDATE", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            int n; Value e;
            if (!argInt(a[1], n, e)) return e;
            YMD y = serialToYMD(std::floor(s));
            int mm = y.m - 1 + n;
            int yy = y.y + mm / 12;
            mm = mm % 12; if (mm < 0) { mm += 12; yy--; }
            int dd = std::min(y.d, daysInMonth(yy, mm + 1));
            if (yy < 1900 || yy > 9999) return Value::error(Err::Num);
            return Value::num(ymdToSerial(yy, mm + 1, dd));
        });

        registerFunction("EOMONTH", 2, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            int n; Value e;
            if (!argInt(a[1], n, e)) return e;
            YMD y = serialToYMD(std::floor(s));
            int mm = y.m - 1 + n;
            int yy = y.y + mm / 12;
            mm = mm % 12; if (mm < 0) { mm += 12; yy--; }
            if (yy < 1900 || yy > 9999) return Value::error(Err::Num);
            return Value::num(ymdToSerial(yy, mm + 1, daysInMonth(yy, mm + 1)));
        });

        registerFunction("YEARFRAC", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s1, s2;
            if (!valueToSerial(a[0], s1) || !valueToSerial(a[1], s2)) return Value::error(Err::Value);
            int b = basisOf(a, 2);
            if (b < 0) return Value::error(Err::Num);
            return Value::num(yearFrac(s1, s2, b));
        });

        registerFunction("DATEDIF", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s1, s2;
            if (!valueToSerial(a[0], s1) || !valueToSerial(a[1], s2)) return Value::error(Err::Value);
            if (s1 > s2) return Value::error(Err::Num);
            std::string unit; Value e;
            if (!toText(a[2], unit, e)) return e;
            std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);
            if (unit == "Y" || unit == "YD" || unit == "YM" || unit == "MD" ||
                unit == "M" || unit == "D") {
                // 合法
            } else return Value::error(Err::Num);
            YMD d1 = serialToYMD(std::floor(s1)), d2 = serialToYMD(std::floor(s2));
            if (unit == "D") return Value::num(std::floor(s2) - std::floor(s1));
            if (unit == "M") return Value::num((d2.y - d1.y) * 12.0 + (d2.m - d1.m) - (d2.d < d1.d ? 1.0 : 0.0));
            if (unit == "Y") {
                int y = d2.y - d1.y;
                if (d2.m < d1.m || (d2.m == d1.m && d2.d < d1.d)) y--;
                return Value::num((double)y);
            }
            if (unit == "YM") {
                int y = d2.y - d1.y;
                if (d2.m < d1.m || (d2.m == d1.m && d2.d < d1.d)) y--;
                int m = (d2.y - (d1.y + y)) * 12 + (d2.m - d1.m);   // y 用于对齐周年
                if (d2.d < d1.d) m--;
                if (m < 0) m += 12;
                return Value::num((double)m);
            }
            if (unit == "YD") {
                int y = d2.y - d1.y;
                if (d2.m < d1.m || (d2.m == d1.m && d2.d < d1.d)) y--;
                YMD anniv = d1; anniv.y += y;
                double a1 = ymdToSerial(anniv.y, anniv.m, anniv.d);
                return Value::num(std::floor(s2) - std::floor(a1));
            }
            // "MD"
            int pm = d2.m - 1; int py = d2.y;
            if (pm == 0) { pm = 12; py--; }
            YMD prev = {py, pm, d1.d};
            if (d1.d > daysInMonth(py, pm)) prev.d = daysInMonth(py, pm);
            double a1 = ymdToSerial(prev.y, prev.m, prev.d);
            return Value::num(std::floor(s2) - std::floor(a1));
        });

        registerFunction("NETWORKDAYS", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s1, s2;
            if (!valueToSerial(a[0], s1) || !valueToSerial(a[1], s2)) return Value::error(Err::Value);
            long long d1 = (long long)std::floor(s1), d2 = (long long)std::floor(s2);
            if (d1 > d2) std::swap(d1, d2);
            std::vector<long long> hol;
            if (a.size() >= 3) {
                Value e;
                std::vector<Value> v;
                flattenAll({a[2]}, v, e);
                if (e.isError()) return e;
                for (auto& x : v) { double hs; if (valueToSerial(x, hs)) hol.push_back((long long)std::floor(hs)); }
            }
            long long cnt = 0;
            for (long long d = d1; d <= d2; d++) {
                long long days;
                if (d >= 61) days = anchor1900_03_01() + (d - 61);
                else days = anchor1900_01_01() + (d - 1);
                int w = weekdayOf(days);
                if (w == 0 || w == 6) continue;
                if (std::find(hol.begin(), hol.end(), d) != hol.end()) continue;
                cnt++;
            }
            return Value::num((double)cnt);
        });

        registerFunction("WORKDAY", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            int n; Value e;
            if (!argInt(a[1], n, e)) return e;
            std::vector<long long> hol;
            if (a.size() >= 3) {
                std::vector<Value> v; Value ee;
                flattenAll({a[2]}, v, ee);
                if (ee.isError()) return ee;
                for (auto& x : v) { double hs; if (valueToSerial(x, hs)) hol.push_back((long long)std::floor(hs)); }
            }
            long long d = (long long)std::floor(s);
            int step = n >= 0 ? 1 : -1;
            long long left = std::abs((long long)n);
            while (left > 0) {
                d += step;
                long long days;
                if (d >= 61) days = anchor1900_03_01() + (d - 61);
                else days = anchor1900_01_01() + (d - 1);
                int w = weekdayOf(days);
                if (w == 0 || w == 6) continue;
                if (std::find(hol.begin(), hol.end(), d) != hol.end()) continue;
                left--;
            }
            return Value::num((double)d);
        });

        registerFunction("WEEKNUM", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            int type = 1;
            if (a.size() >= 2) { Value e; if (!argInt(a[1], type, e)) return e; }
            YMD y = serialToYMD(std::floor(s));
            // 1 月 1 日所在的周为第 1 周
            long long jan1 = daysFromCivil(y.y, 1, 1);
            long long cur;
            if ((long long)std::floor(s) >= 61) cur = anchor1900_03_01() + ((long long)std::floor(s) - 61);
            else cur = anchor1900_01_01() + ((long long)std::floor(s) - 1);
            int firstW = weekdayOf(jan1);
            int offset;
            if (type == 1 || type == 11 || type == 12 || type == 13 || type == 14 ||
                type == 15 || type == 16 || type == 17) {
                // 周起始日依 type 而定，简化：type 1 -> 周日
                offset = (type == 1) ? firstW : (type == 2 ? (firstW + 6) % 7 : firstW);
            } else {
                offset = (firstW + 6) % 7;      // type 2：周一
            }
            return Value::num((double)((cur - jan1 + offset) / 7 + 1));
        });

        registerFunction("ISOWEEKNUM", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double s; if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
            long long cur;
            if ((long long)std::floor(s) >= 61) cur = anchor1900_03_01() + ((long long)std::floor(s) - 61);
            else cur = anchor1900_01_01() + ((long long)std::floor(s) - 1);
            int w = weekdayOf(cur);                    // 0=周日
            int iso = (w == 0) ? 7 : w;                // ISO：周一=1..周日=7
            long long thursday = cur + (4 - iso);
            int ty; unsigned tm, td;
            civilFromDays(thursday, ty, tm, td);
            // ISO 8601：第 1 周是"包含当年 1 月 4 日的那一周"。
            // 基准必须取 1 月 4 日所在周的周四。
            // 原实现用 1 月 1 日再加一个 "-=7" 补丁，结果整体差一周 ——
            // 实测 ISOWEEKNUM(2024-1-1) 得 2（应为 1，2024-01-01 是周一），
            // ISOWEEKNUM(2023-1-1) 得 53（应为 52）。
            long long jan4 = daysFromCivil(ty, 1, 4);
            int w4 = weekdayOf(jan4);
            int iso4 = (w4 == 0) ? 7 : w4;
            long long tThu = jan4 + (4 - iso4);      // 第 1 周的周四
            return Value::num((double)((thursday - tThu) / 7 + 1));
        });

        registerFunction("TIMEVALUE", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string s; Value e;
            if (!toText(a[0], s, e)) return e;
            // 支持 "18:30" / "18:30:15" / "6:30 PM"
            std::string u = s;
            std::transform(u.begin(), u.end(), u.begin(), ::toupper);
            bool pm = u.find("PM") != std::string::npos;
            bool am = u.find("AM") != std::string::npos;
            std::string digits;
            for (char c : u) if (std::isdigit((unsigned char)c) || c == ':') digits.push_back(c);
            std::vector<int> p; std::string cur;
            for (char c : digits) {
                if (c == ':') { p.push_back(cur.empty() ? 0 : std::stoi(cur)); cur.clear(); }
                else cur.push_back(c);
            }
            p.push_back(cur.empty() ? 0 : std::stoi(cur));
            if (p.empty() || p.size() > 3) return Value::error(Err::Value);
            int h = p[0], m = p.size() > 1 ? p[1] : 0, sec = p.size() > 2 ? p[2] : 0;
            if (pm && h < 12) h += 12;
            if (am && h == 12) h = 0;
            if (h > 23 || m > 59 || sec > 59) return Value::error(Err::Value);
            return Value::num((h * 3600 + m * 60 + sec) / 86400.0);
        });
    }
};
static DateRegistrar g_dateReg;

} // namespace xl
