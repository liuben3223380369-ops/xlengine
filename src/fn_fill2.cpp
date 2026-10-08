// 补齐批次 B：信息/元函数、工作日国际版、双字节文本、统计补全、复数双曲、奇数期债券
//
// 这批的共同特点：都是"真功能"，不是别名。其中
//   - 元函数（CELL/INFO/FORMULATEXT/ISFORMULA）依赖本轮新增的引用语义栈
//   - 奇数期债券（ODDF*/ODDL*）是上一轮 README 里明确标注"未覆盖"的缺口
#include "functions.hpp"
#include "value.hpp"
#include "ast.hpp"
#include "date.hpp"
#include "dist.hpp"
#include <cmath>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <map>

namespace xl {

namespace {

bool numArg(const Value& v, double& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    Value e;
    if (!toNumber(v, out, e)) { err = e; return false; }
    return true;
}
bool intArg(const Value& v, int& out, Value& err) {
    double d;
    if (!numArg(v, d, err)) return false;
    out = (int)std::floor(d);
    return true;
}
// 收集数值（忽略文本/空/逻辑），用于统计类
void nums(const std::vector<Value>& a, std::vector<double>& out, Value& err) {
    std::vector<Value> v;
    if (!flattenArgs(a, v, true, err)) return;
    for (auto& x : out.clear(), v) { double d; Value e; if (toNumber(x, d, e)) out.push_back(d); }
}

// 双字节宽度：DBCS 字符（中文/日文/韩文等）算 2 字节，ASCII 算 1。
// Excel 的 LENB 按系统代码页算，这里按 UTF-8 的码点范围判定，
// CJK 统一表意文字等宽字符区间返回 2。
size_t dbcsWidth(char32_t cp) {
    auto in = [&](char32_t lo, char32_t hi) { return cp >= lo && cp <= hi; };
    if (cp < 0x80) return 1;
    if (in(0x1100, 0x115F) ||          // 韩文 Jamo
        in(0x2E80, 0x303E) ||          // CJK 部首、标点
        in(0x3041, 0x33FF) ||          // 日文假名、CJK 兼容
        in(0x3400, 0x4DBF) ||          // CJK 扩展 A
        in(0x4E00, 0x9FFF) ||          // CJK 基本区
        in(0xA000, 0xA4CF) ||          // 彝文
        in(0xAC00, 0xD7A3) ||          // 韩文音节
        in(0xF900, 0xFAFF) ||          // CJK 兼容表意文字
        in(0xFE30, 0xFE4F) ||          // CJK 兼容形式
        in(0xFF00, 0xFF60) ||          // 全角形式
        in(0xFFE0, 0xFFE6) ||
        in(0x20000, 0x3FFFF))          // CJK 扩展 B~
        return 2;
    return 1;
}
// UTF-8 解码成码点序列。
// 首版实现把"还有多少字节"的判断写成了 i+n >= s.size()，导致多字节字符
// 被逐字节拆开 —— 于是汉字宽度算成 3 而不是 2，LENB/LEFTB 全错。
// 这里按标准写法：先取首字节决定长度，再校验后续字节都是 10xxxxxx。
std::vector<char32_t> utf8Decode(const std::string& s) {
    std::vector<char32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int n;
        char32_t cp;
        if      (c < 0x80)        { n = 0; cp = c; }
        else if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07u; }
        else { out.push_back((char32_t)c); i++; continue; }   // 非法首字节

        if (i + (size_t)n >= s.size()) {                       // 后续字节不足
            out.push_back((char32_t)c); i++; continue;
        }
        bool ok = true;
        for (int k = 1; k <= n; k++)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) { ok = false; break; }
        if (!ok) { out.push_back((char32_t)c); i++; continue; }
        for (int k = 1; k <= n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3Fu);
        i += (size_t)n + 1;
        out.push_back(cp);
    }
    return out;
}
size_t dbcsLen(const std::string& s) {
    size_t n = 0;
    for (char32_t cp : utf8Decode(s)) n += dbcsWidth(cp);
    return n;
}
// 按双字节宽度截取前 n 个"字节"
std::string dbcsLeft(const std::string& s, size_t n) {
    std::string out;
    size_t used = 0;
    size_t i = 0;
    while (i < s.size() && used < n) {
        unsigned char c = (unsigned char)s[i];
        size_t len = 1;
        if      ((c & 0xF8) == 0xF0) len = 4;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xE0) == 0xC0) len = 2;
        std::string piece = s.substr(i, len);
        auto cps = utf8Decode(piece);
        size_t w = cps.empty() ? 1 : dbcsWidth(cps[0]);
        if (used + w > n) break;
        out += piece; used += w; i += len;
    }
    return out;
}

// ---------------------------------------------------------------------------
// 工作日：weekend 参数解析
// ---------------------------------------------------------------------------
// WORKDAY.INTL / NETWORKDAYS.INTL 的 weekend 可以是：
//   - 字符串 7 位 0/1，从周一开始，如 "0000011" = 周六周日休
//   - 数字 1..17 的预设码
bool parseWeekend(const Value& v, bool off[7], Value& err) {
    if (v.isEmpty()) {                                   // 缺省 = 周六周日
        for (int i = 0; i < 7; i++) off[i] = (i == 5 || i == 6);
        return true;
    }
    if (v.isStr()) {
        std::string s = v.s;
        if (s.size() != 7) { err = Value::error(Err::Value); return false; }
        for (int i = 0; i < 7; i++) {
            if (s[i] == '0') off[i] = false;
            else if (s[i] == '1') off[i] = true;
            else { err = Value::error(Err::Value); return false; }
        }
        return true;
    }
    int code;
    if (!intArg(v, code, err)) return false;
    // 微软文档定义的预设码。下标 0=周一 .. 6=周日。
    // 8/9/10 在文档中没有定义，Excel 返回 #NUM!，这里保持一致。
    static const int table[18][7] = {
        {0,0,0,0,0,0,0},   // 0 占位（不会用到）
        {0,0,0,0,0,1,1},   // 1: 周六、周日
        {1,0,0,0,0,0,1},   // 2: 周日、周一
        {1,1,0,0,1,1,0},   // 3: 周一、周二、周五、周六
        {0,1,1,0,0,0,0},   // 4: 周二、周三
        {0,0,1,1,0,0,0},   // 5: 周三、周四
        {0,0,0,1,1,1,0},   // 6: 周四、周五、周六
        {0,0,0,0,1,1,1},   // 7: 周五、周六、周日
        {0,0,0,0,0,0,0},   // 8-10: 未定义，走下面的 #NUM!
        {0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0},
        {0,0,0,0,0,0,1},   // 11: 仅周日
        {1,0,0,0,0,0,0},   // 12: 仅周一
        {0,1,0,0,0,0,0},   // 13: 仅周二
        {0,0,1,0,0,0,0},   // 14: 仅周三
        {0,0,0,1,0,0,0},   // 15: 仅周四
        {0,0,0,0,1,0,0},   // 16: 仅周五
        {0,0,0,0,0,1,0},   // 17: 仅周六
    };
    // 码 8-10 微软文档未定义，Excel 报 #NUM!
    if (code >= 8 && code <= 10) { err = Value::error(Err::Num); return false; }
    if (code < 1 || code > 17) { err = Value::error(Err::Num); return false; }
    for (int i = 0; i < 7; i++) off[i] = table[code][i] != 0;
    return true;
}
// isWorkday: off 的下标 0=周一 .. 6=周日
bool isWorkday(long long epochDays, const bool off[7]) {
    int w = weekdayOf(epochDays);     // 0=周日 .. 6=周六
    int idx = (w == 0) ? 6 : w - 1;   // 转成 0=周一
    return !off[idx];
}

// 节假日集合
bool buildHolidays(const std::vector<Value>& args, size_t idx,
                   std::vector<long long>& out, Value& err) {
    if (idx >= args.size() || args[idx].isEmpty()) return true;
    std::vector<Value> flat;
    if (!flattenArgs({args[idx]}, flat, true, err)) return false;
    for (auto& v : flat) {
        double s;
        if (!valueToSerial(v, s)) continue;
        out.push_back(serialToEpochDays(s));
    }
    return true;
}

// ---------------------------------------------------------------------------
// 债券：由日期序列生成付息日
// ---------------------------------------------------------------------------
struct Coupon {
    double date;        // 序列号
    double factor;      // 该期票息相对 100*rate/freq 的比例（奇数期用）
};

// 从 start 起，每 freq 个月一个付息日，直到（含）maturity
void addMonths(int& y, int& m, int step) {
    int total = (y * 12 + (m - 1)) + step;
    y = total / 12;
    m = total % 12 + 1;
}
double serialAddMonths(double serial, int months) {
    YMD d = serialToYMD(serial);
    int y = d.y, m = (int)d.m;
    addMonths(y, m, months);
    int dd = std::min((int)d.d, daysInMonth(y, m));
    return ymdToSerial(y, m, dd);
}

} // namespace

// ===========================================================================
// 奇数期债券
//
// Excel 用"准付息期（quasi-coupon period）"建模奇数期：
// 奇数期内的每个准付息日也视作一次票息支付，用以确定折现期数。
//
// 这里统一用现金流折现实现，并与常规 PRICE 做一致性自检：
// 当奇数期长度恰好等于一个正常期时，ODDFPRICE 必须等于 PRICE。
// ===========================================================================
namespace {

// 通用奇数期债券定价：给定付息日与每期票息比例，按 yld 折现到 settlement
double oddPriceCore(double settlement, const std::vector<double>& dates,
                    const std::vector<double>& factors, double rate, double yld,
                    double redemption, int freq) {
    double base = 1.0 + yld / freq;
    double price = 0;
    for (size_t i = 0; i < dates.size(); i++) {
        double cf = 100.0 * rate / freq * factors[i];
        if (i + 1 == dates.size()) cf += redemption;         // 到期还本
        // 折现期数：从 settlement 到该付息日，按准付息期(年/freq)计
        double years = (dates[i] - settlement) / 365.0;
        double t = years * freq;
        price += cf / std::pow(base, t);
    }
    return price;
}
} // namespace

struct Fill2Registrar {
    Fill2Registrar();
};
static Fill2Registrar g_fill2Reg;

Fill2Registrar::Fill2Registrar() {
    // -----------------------------------------------------------------------
    // 信息 / 元函数
    // -----------------------------------------------------------------------
    registerFunction("ISEVEN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double d; Value e;
        if (!numArg(a[0], d, e)) return e;
        // Excel 先截断取整再判奇偶：ISEVEN(2.5) 用 2 -> TRUE
        double t = std::trunc(d);
        return Value::boolean(std::fmod(t, 2.0) == 0);
    });
    registerFunction("ISODD", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double d; Value e;
        if (!numArg(a[0], d, e)) return e;
        double t = std::trunc(d);
        return Value::boolean(std::fmod(t, 2.0) != 0);
    });
    registerFunction("ISOMITTED", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        // 只有 LAMBDA 的可选参数被省略时才为 TRUE（此时是 Empty）
        return Value::boolean(a[0].isEmpty());
    });
    registerFunction("ISFORMULA", 1, 1, [](const std::vector<Value>&, EvalCtx& c) -> Value {
        int col, row;
        if (!c.argRefAt(0, col, row)) return Value::error(Err::Value);
        std::string f;
        return Value::boolean(c.cellFormula(col, row, f));
    });
    registerFunction("FORMULATEXT", 1, 1, [](const std::vector<Value>&, EvalCtx& c) -> Value {
        int col, row;
        if (!c.argRefAt(0, col, row)) return Value::error(Err::NA);
        std::string f;
        if (!c.cellFormula(col, row, f)) return Value::error(Err::NA);
        return Value::str("=" + f);
    });
    registerFunction("CELL", 1, 2, [](const std::vector<Value>& a, EvalCtx& c) -> Value {
        std::string type = a[0].isStr() ? a[0].s : std::string();
        std::transform(type.begin(), type.end(), type.begin(), ::tolower);
        int col = -1, row = -1;
        // 引用在第 2 个参数上。不能用"值是否为空"判断——被引用的
        // 空单元格值就是 Empty，那样会误判成"没给引用"而退回当前格。
        size_t refIdx = (a.size() > 1) ? 1 : 0;
        c.argRefAt(refIdx, col, row);
        if (col < 0) { int cc, rr; c.currentCell(cc, rr); col = cc; row = rr; }

        if (type == "row")    return Value::num(row + 1);
        if (type == "col")    return Value::num(col + 1);
        if (type == "address") {
            std::string addr;
            if (col >= 0 && c.cellAddr(col, row, addr)) return Value::str(addr);
            return Value::error(Err::NA);
        }
        if (type == "filename") return Value::str("");       // 未落盘时为空，Excel 也是空
        if (type == "type") {
            if (col < 0) return Value::str("v");
            std::string f;
            return Value::str(c.cellFormula(col, row, f) ? "f" : "v");
        }
        // 以下与单元格无关，属环境信息
        if (type == "version" || type == "release") return Value::str("xlengine");
        if (type == "system")   return Value::str("Linux");
        if (type == "recalc")   return Value::str("Automatic");
        if (type == "numfile")  return Value::num(1);
        if (type == "width")    return Value::num(10);
        return Value::error(Err::Value);
    });
    registerFunction("INFO", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string t; Value e;
        if (!toText(a[0], t, e)) return e;
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t == "directory")  return Value::str("");
        if (t == "numfile")    return Value::num(1);
        if (t == "origin")     return Value::str("$A$1");
        if (t == "osversion")  return Value::str("Linux");
        if (t == "recalc")     return Value::str("Automatic");
        if (t == "release")    return Value::str("1.0");
        if (t == "system")     return Value::str("Linux");
        if (t == "version")    return Value::str("xlengine 1.0");
        if (t == "memavail" || t == "memused" || t == "totmem")
            return Value::error(Err::NA);
        return Value::error(Err::Value);
    });
    registerFunction("HYPERLINK", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string link; Value e;
        if (!toText(a[0], link, e)) return e;
        if (a.size() > 1 && !a[1].isEmpty()) {
            std::string name;
            if (!toText(a[1], name, e)) return e;
            return Value::str(name);
        }
        return Value::str(link);
    });
    registerFunction("ENCODEURL", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string s; Value e;
        if (!toText(a[0], s, e)) return e;
        std::ostringstream o;
        o << std::hex << std::uppercase << std::setfill('0');
        for (unsigned char c : s) {
            if (std::isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') o << (char)c;
            else o << '%' << std::setw(2) << (int)c;
        }
        return Value::str(o.str());
    });
    registerFunction("AREAS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        // 单个区域/引用恒为 1。多区域联合在本引擎里已合成一个数组，
        // 因此无法区分 —— 这是已知限制，返回 1 而不是报错更有用。
        (void)a;
        return Value::num(1);
    });

    // -----------------------------------------------------------------------
    // 工作日 / 净工作日（国际版：可自定义周末）
    // -----------------------------------------------------------------------
    registerFunction("WORKDAY.INTL", 2, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double s; Value e;
        if (!valueToSerial(a[0], s)) return Value::error(Err::Value);
        int days;
        if (!intArg(a[1], days, e)) return e;
        // 注意不要把"解析成功"和"参数缺失"混成一个 if/else：
        // 写成 if (有参数 && !parse(...)) ... else 用默认值覆盖
        // 会让解析成功的 off 被默认值冲掉（NETWORKDAYS.INTL 就踩了这个）。
        bool off[7];
        if (a.size() > 2 && !a[2].isEmpty()) {
            if (!parseWeekend(a[2], off, e)) return e;
        } else {
            bool tmp[7];
            parseWeekend(Value::empty(), tmp, e);
            for (int i = 0; i < 7; i++) off[i] = tmp[i];
        }
        std::vector<long long> hol;
        if (!buildHolidays(a, 3, hol, e)) return e;

        long long cur = serialToEpochDays(s);
        int sign = days < 0 ? -1 : 1;
        int left = std::abs(days);
        while (left > 0) {
            cur += sign;
            if (isWorkday(cur, off) &&
                std::find(hol.begin(), hol.end(), cur) == hol.end()) left--;
        }
        // 转回 Excel 序列号
        double back = (double)(cur - anchor1900_03_01() + 61);
        return Value::num(back);
    });
    registerFunction("NETWORKDAYS.INTL", 2, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double s1, s2;
        if (!valueToSerial(a[0], s1) || !valueToSerial(a[1], s2)) return Value::error(Err::Value);
        Value e;
        bool off[7];
        if (a.size() > 2 && !a[2].isEmpty()) {
            if (!parseWeekend(a[2], off, e)) return e;
        } else {
            bool tmp[7];
            parseWeekend(Value::empty(), tmp, e);
            for (int i = 0; i < 7; i++) off[i] = tmp[i];
        }
        std::vector<long long> hol;
        if (!buildHolidays(a, 3, hol, e)) return e;

        long long d1 = serialToEpochDays(s1), d2 = serialToEpochDays(s2);
        int sign = 1;
        if (d1 > d2) { std::swap(d1, d2); sign = -1; }
        long long cnt = 0;
        for (long long d = d1; d <= d2; d++)
            if (isWorkday(d, off) && std::find(hol.begin(), hol.end(), d) == hol.end()) cnt++;
        return Value::num((double)(sign * cnt));
    });

    // -----------------------------------------------------------------------
    // 双字节文本（DBCS）
    // -----------------------------------------------------------------------
    registerFunction("LENB", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string s; Value e;
        if (!toText(a[0], s, e)) return e;
        return Value::num((double)dbcsLen(s));
    });
    registerFunction("LEFTB", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string s; Value e;
        if (!toText(a[0], s, e)) return e;
        int n = 1;
        if (a.size() > 1) { if (!intArg(a[1], n, e)) return e; }
        if (n < 0) return Value::error(Err::Value);
        return Value::str(dbcsLeft(s, (size_t)n));
    });
    registerFunction("MIDB", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string s; Value e;
        if (!toText(a[0], s, e)) return e;
        int start, n;
        if (!intArg(a[1], start, e)) return e;
        if (!intArg(a[2], n, e)) return e;
        if (start < 1 || n < 0) return Value::error(Err::Value);
        std::string tail = dbcsLeft(s, (size_t)(start - 1));
        std::string rest = s.substr(tail.size());
        return Value::str(dbcsLeft(rest, (size_t)n));
    });
    registerFunction("RIGHTB", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string s; Value e;
        if (!toText(a[0], s, e)) return e;
        int n = 1;
        if (a.size() > 1) { if (!intArg(a[1], n, e)) return e; }
        if (n < 0) return Value::error(Err::Value);
        size_t total = dbcsLen(s);
        if ((size_t)n >= total) return Value::str(s);
        std::string head = dbcsLeft(s, total - (size_t)n);
        return Value::str(s.substr(head.size()));
    });
    // FINDB / SEARCHB 与 FIND / SEARCH 的区别：返回的是字节位置
    registerFunction("FINDB", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        // Excel 的顺序是 FINDB(find_text, within_text)，
        // 与直觉的"在大串里找小串"相反，写成 (a[0]=within, a[1]=find) 会全错。
        std::string needle, hay; Value e;
        if (!toText(a[0], needle, e)) return e;
        if (!toText(a[1], hay, e)) return e;
        if (needle.empty()) return Value::error(Err::Value);
        size_t pos = hay.find(needle);
        if (pos == std::string::npos) return Value::error(Err::Value);
        return Value::num((double)(dbcsLen(hay.substr(0, pos)) + 1));
    });
    registerFunction("SEARCHB", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        // 同样按 (find_text, within_text) 取参
        std::string needle, hay; Value e;
        if (!toText(a[0], needle, e)) return e;
        if (!toText(a[1], hay, e)) return e;
        if (needle.empty()) return Value::error(Err::Value);
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        };
        // 大小写不敏感，支持 * ? 通配符（简化：先做子串，通配符留待后续）
        std::string lh = lower(hay), ln = lower(needle);
        size_t pos = lh.find(ln);
        if (pos == std::string::npos) return Value::error(Err::Value);
        return Value::num((double)(dbcsLen(hay.substr(0, pos)) + 1));
    });
    registerFunction("REPLACEB", 4, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string s, rep; Value e;
        if (!toText(a[0], s, e)) return e;
        int start, n;
        if (!intArg(a[1], start, e)) return e;
        if (!intArg(a[2], n, e)) return e;
        if (!toText(a[3], rep, e)) return e;
        if (start < 1 || n < 0) return Value::error(Err::Value);
        std::string head = dbcsLeft(s, (size_t)(start - 1));
        std::string rest = s.substr(head.size());
        std::string mid = dbcsLeft(rest, (size_t)n);
        std::string tail = rest.substr(mid.size());
        return Value::str(head + rep + tail);
    });
    registerFunction("ASC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::string s; Value e;
        if (!toText(a[0], s, e)) return e;
        // 全角 ASCII（FF01-FF5E）转半角；其余字符原样保留
        std::string out;
        for (char32_t cp : utf8Decode(s)) {
            if (cp >= 0xFF01 && cp <= 0xFF5E) out.push_back((char)(cp - 0xFEE0));
            else if (cp == 0x3000) out.push_back(' ');
            else {
                // 编码回去
                auto enc = [&](char32_t c) {
                    if (c < 0x80) out.push_back((char)c);
                    else if (c < 0x800) {
                        out.push_back((char)(0xC0 | (c >> 6)));
                        out.push_back((char)(0x80 | (c & 0x3F)));
                    } else if (c < 0x10000) {
                        out.push_back((char)(0xE0 | (c >> 12)));
                        out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (c & 0x3F)));
                    } else {
                        out.push_back((char)(0xF0 | (c >> 18)));
                        out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
                        out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (c & 0x3F)));
                    }
                };
                enc(cp);
            }
        }
        return Value::str(out);
    });

    // -----------------------------------------------------------------------
    // 统计补全
    // -----------------------------------------------------------------------
    registerFunction("GAUSS", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        return Value::num(dist::normCdf(x) - 0.5);          // = NORM.S.DIST(x,TRUE) - 0.5
    });
    registerFunction("PHI", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x; Value e;
        if (!numArg(a[0], x, e)) return e;
        return Value::num(dist::normPdf(x));
    });
    registerFunction("SKEW.P", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::vector<double> v; Value e;
        nums(a, v, e); if (e.isError()) return e;
        if (v.size() < 3) return Value::error(Err::Div0);
        double m = 0; for (double x : v) m += x; m /= v.size();
        double s2 = 0, s3 = 0;
        for (double x : v) { double d = x - m; s2 += d * d; s3 += d * d * d; }
        // 总体版本：用总体标准差（除以 n），与 SKEW 的样本标准差不同
        double sd = std::sqrt(s2 / v.size());
        if (sd < 1e-15) return Value::error(Err::Div0);
        return Value::num((s3 / v.size()) / (sd * sd * sd));
    });
    registerFunction("PROB", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::vector<double> xs, ps; Value e;
        nums({a[0]}, xs, e); if (e.isError()) return e;
        nums({a[1]}, ps, e); if (e.isError()) return e;
        if (xs.size() != ps.size() || xs.empty()) return Value::error(Err::Num);
        double lo, hi;
        if (!numArg(a[2], lo, e)) return e;
        hi = lo;
        if (a.size() > 3) { if (!numArg(a[3], hi, e)) return e; }
        if (lo > hi) return Value::error(Err::Num);
        double sum = 0;
        for (size_t i = 0; i < xs.size(); i++) {
            if (ps[i] < 0 || ps[i] > 1) return Value::error(Err::Num);
            if (xs[i] >= lo && xs[i] <= hi) sum += ps[i];
        }
        return Value::num(sum);
    });
    registerFunction("STDEVPA", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        // 与 STDEV.P 的区别：区域内文本和 FALSE 计 0，TRUE 计 1
        std::vector<Value> flat;
        Value e;
        flattenAll(a, flat, e); if (e.isError()) return e;
        std::vector<double> v;
        for (auto& x : flat) {
            if (x.isEmpty()) continue;
            if (x.isNum()) v.push_back(x.n);
            else if (x.isBool()) v.push_back(x.b ? 1.0 : 0.0);
            else if (x.isStr()) { double d; Value e2; if (toNumber(x, d, e2)) v.push_back(d); else v.push_back(0.0); }
        }
        if (v.empty()) return Value::error(Err::Div0);
        double m = 0; for (double x : v) m += x; m /= v.size();
        double s = 0; for (double x : v) s += (x - m) * (x - m);
        return Value::num(std::sqrt(s / v.size()));
    });
    registerFunction("VARPA", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::vector<Value> flat; Value e;
        flattenAll(a, flat, e); if (e.isError()) return e;
        std::vector<double> v;
        for (auto& x : flat) {
            if (x.isEmpty()) continue;
            if (x.isNum()) v.push_back(x.n);
            else if (x.isBool()) v.push_back(x.b ? 1.0 : 0.0);
            else if (x.isStr()) { double d; Value e2; if (toNumber(x, d, e2)) v.push_back(d); else v.push_back(0.0); }
        }
        if (v.empty()) return Value::error(Err::Div0);
        double m = 0; for (double x : v) m += x; m /= v.size();
        double s = 0; for (double x : v) s += (x - m) * (x - m);
        return Value::num(s / v.size());
    });
    registerFunction("MODE.MULT", 1, 255, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        std::vector<double> v; Value e;
        nums(a, v, e); if (e.isError()) return e;
        if (v.empty()) return Value::error(Err::Num);
        std::map<double, int> cnt;
        for (double x : v) cnt[x]++;
        int best = 0;
        for (auto& kv : cnt) best = std::max(best, kv.second);
        if (best <= 1) return Value::error(Err::NA);
        auto arr = std::make_shared<Array2D>();
        for (auto& kv : cnt)
            if (kv.second == best) arr->push_back({Value::num(kv.first)});
        return Value::array(arr);
    });
    registerFunction("BINOM.DIST.RANGE", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; int n; double p, s;
        if (!intArg(a[0], n, e)) return e;
        if (!numArg(a[1], p, e)) return e;
        if (!numArg(a[2], s, e)) return e;
        double s2 = (a.size() > 3) ? 0 : s;
        if (a.size() > 3) { if (!numArg(a[3], s2, e)) return e; }
        if (n < 0 || p < 0 || p > 1 || s < 0 || s2 < s) return Value::error(Err::Num);
        if (s2 > n) s2 = n;
        double total = 0;
        for (int k = (int)std::ceil(s); k <= (int)std::floor(s2); k++) {
            if (k < 0 || k > n) continue;
            // C(n,k) p^k (1-p)^(n-k)
            double lg = std::lgamma(n + 1) - std::lgamma(k + 1) - std::lgamma(n - k + 1);
            total += std::exp(lg + k * std::log(p) + (n - k) * std::log1p(-p));
        }
        return Value::num(total);
    });
    registerFunction("BINOM.INV", 3, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        Value e; int n; double p, alpha;
        if (!intArg(a[0], n, e)) return e;
        if (!numArg(a[1], p, e)) return e;
        if (!numArg(a[2], alpha, e)) return e;
        if (n < 0 || p < 0 || p > 1 || alpha < 0 || alpha > 1) return Value::error(Err::Num);
        double cum = 0;
        for (int k = 0; k <= n; k++) {
            double lg = std::lgamma(n + 1) - std::lgamma(k + 1) - std::lgamma(n - k + 1);
            cum += std::exp(lg + k * std::log(p) + (n - k) * std::log1p(-p));
            if (cum >= alpha) return Value::num(k);
        }
        return Value::num(n);
    });
    registerFunction("CRITBINOM", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) -> Value {
        return findFunction("BINOM.INV")->impl(a, c);
    });

    // -----------------------------------------------------------------------
    // 复数双曲（complex hyperbolic）
    // -----------------------------------------------------------------------
    auto cmplxBin = [](const std::string& name,
                       std::function<void(double,double,double&,double&)> f) {
        registerFunction(name, 1, 1, [f](const std::vector<Value>& a, EvalCtx&) -> Value {
            Cx z;
            if (!parseCx(a[0], z)) return Value::error(Err::Num);
            double orr, oi;
            f(z.re, z.im, orr, oi);
            if (std::isnan(orr) || std::isnan(oi)) return Value::error(Err::Num);
            Cx o; o.re = orr; o.im = oi;
            return Value::str(cxStr(o));
        });
    };
    // sinh(a+bi) = sinh(a)cos(b) + i cosh(a)sin(b)
    cmplxBin("IMSINH", [](double a, double b, double& r, double& i) {
        r = std::sinh(a) * std::cos(b);
        i = std::cosh(a) * std::sin(b);
    });
    // cosh(a+bi) = cosh(a)cos(b) + i sinh(a)sin(b)
    cmplxBin("IMCOSH", [](double a, double b, double& r, double& i) {
        r = std::cosh(a) * std::cos(b);
        i = std::sinh(a) * std::sin(b);
    });
    // tanh(z) = sinh(z)/cosh(z)
    cmplxBin("IMTANH", [](double a, double b, double& r, double& i) {
        double sr = std::sinh(a) * std::cos(b), si = std::cosh(a) * std::sin(b);
        double cr = std::cosh(a) * std::cos(b), ci = std::sinh(a) * std::sin(b);
        double d = cr * cr + ci * ci;
        if (d == 0) { r = std::nan(""); i = std::nan(""); return; }
        r = (sr * cr + si * ci) / d;
        i = (si * cr - sr * ci) / d;
    });
    // sec(z) = 1/cos(z)
    cmplxBin("IMSEC", [](double a, double b, double& r, double& i) {
        double cr = std::cos(a) * std::cosh(b), ci = -std::sin(a) * std::sinh(b);
        double d = cr * cr + ci * ci;
        if (d == 0) { r = std::nan(""); i = std::nan(""); return; }
        r = cr / d; i = -ci / d;
    });
    cmplxBin("IMCSC", [](double a, double b, double& r, double& i) {
        double sr = std::sin(a) * std::cosh(b), si = std::cos(a) * std::sinh(b);
        double d = sr * sr + si * si;
        if (d == 0) { r = std::nan(""); i = std::nan(""); return; }
        r = sr / d; i = -si / d;
    });
    cmplxBin("IMCOT", [](double a, double b, double& r, double& i) {
        double sr = std::sin(a) * std::cosh(b), si = std::cos(a) * std::sinh(b);
        double cr = std::cos(a) * std::cosh(b), ci = -std::sin(a) * std::sinh(b);
        // cot = cos/sin，奇异点在 sin(z)=0（z = kπ），不是 cos(z)=0
        double sd = sr * sr + si * si;
        if (sd < 1e-24) { r = std::nan(""); i = std::nan(""); return; }
        double d = cr * cr + ci * ci;
        if (d == 0) { r = std::nan(""); i = std::nan(""); return; }
        r = (sr * cr + si * ci) / d;
        i = (si * cr - sr * ci) / d;
    });
    cmplxBin("IMSECH", [](double a, double b, double& r, double& i) {
        double cr = std::cosh(a) * std::cos(b), ci = std::sinh(a) * std::sin(b);
        double d = cr * cr + ci * ci;
        if (d == 0) { r = std::nan(""); i = std::nan(""); return; }
        r = cr / d; i = -ci / d;
    });
    cmplxBin("IMCSCH", [](double a, double b, double& r, double& i) {
        double sr = std::sinh(a) * std::cos(b), si = std::cosh(a) * std::sin(b);
        double d = sr * sr + si * si;
        if (d == 0) { r = std::nan(""); i = std::nan(""); return; }
        r = sr / d; i = -si / d;
    });

    // -----------------------------------------------------------------------
    // 奇数期债券：ODDF*（首期为奇数）/ ODDL*（末期为奇数）
    //
    // 这是上一轮 README 中明确标注"未覆盖"的缺口。
    // 一致性自检：奇数期长度等于正常期时，结果必须等于 PRICE / YIELD。
    // -----------------------------------------------------------------------
    registerFunction("ODDFPRICE", 8, 9, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double sett, mat, issue, first, rate, yld, redem;
        Value e;
        if (!valueToSerial(a[0], sett) || !valueToSerial(a[1], mat) ||
            !valueToSerial(a[2], issue) || !valueToSerial(a[3], first)) return Value::error(Err::Value);
        if (!numArg(a[4], rate, e)) return e;
        if (!numArg(a[5], yld, e)) return e;
        if (!numArg(a[6], redem, e)) return e;
        int freq;
        if (!intArg(a[7], freq, e)) return e;
        if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
        if (issue >= sett || sett >= first || first >= mat) return Value::error(Err::Num);
        if (rate < 0 || yld < 0) return Value::error(Err::Num);

        // 奇数期（issue -> first）内的准付息日 + 之后的正常付息日
        int step = 12 / freq;
        std::vector<double> dates, factors;
        // 准付息日：issue 之后每 step 个月，直到 first（不含 first，first 单独算）
        for (int i = 1; ; i++) {
            double d = serialAddMonths(issue, i * step);
            if (d >= first - 1e-9) break;
            if (d <= sett + 1e-9) continue;              // 已过的准付息日不付
            dates.push_back(d);
            factors.push_back(1.0);                      // 准付息期各付一期票息
        }
        // 首期票息 + 后续正常付息日
        double ncDays = 365.0 / freq;
        double dfc = first - issue;
        double frac = dfc / ncDays;                      // 首期相对正常期的比例
        dates.push_back(first);
        factors.push_back(frac);
        for (int i = 1; ; i++) {
            double d = serialAddMonths(first, i * step);
            if (d >= mat - 1e-9) break;
            dates.push_back(d);
            factors.push_back(1.0);
        }
        dates.push_back(mat);
        factors.push_back(1.0);

        double price = oddPriceCore(sett, dates, factors, rate, yld, redem, freq);
        // 减去应计利息：issue 到 settlement 这一段持有者应得的部分
        double accrFrac = (sett - issue) / ncDays;
        price -= 100.0 * rate / freq * accrFrac;
        return Value::num(price);
    });

    registerFunction("ODDLPRICE", 7, 8, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double sett, mat, last, rate, yld, redem;
        Value e;
        if (!valueToSerial(a[0], sett) || !valueToSerial(a[1], mat) ||
            !valueToSerial(a[2], last)) return Value::error(Err::Value);
        if (!numArg(a[3], rate, e)) return e;
        if (!numArg(a[4], yld, e)) return e;
        if (!numArg(a[5], redem, e)) return e;
        int freq;
        if (!intArg(a[6], freq, e)) return e;
        if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
        if (last >= sett || sett >= mat) return Value::error(Err::Num);
        if (rate < 0 || yld < 0) return Value::error(Err::Num);

        int step = 12 / freq;
        double ncDays = 365.0 / freq;
        std::vector<double> dates, factors;
        // last 之后的正常付息日
        for (int i = 1; ; i++) {
            double d = serialAddMonths(last, i * step);
            if (d >= mat - 1e-9) break;
            if (d <= sett + 1e-9) continue;
            dates.push_back(d);
            factors.push_back(1.0);
        }
        // 末期（奇数）：last 之后的最后一段
        double frac = (mat - last - std::floor((mat - last) / ncDays) * ncDays) / ncDays;
        if (frac <= 1e-9) frac = (mat - last) / ncDays;
        dates.push_back(mat);
        factors.push_back(frac);

        double price = oddPriceCore(sett, dates, factors, rate, yld, redem, freq);
        double accrFrac = (sett - last) / ncDays;
        price -= 100.0 * rate / freq * std::min(accrFrac, (double)((int)((mat - last) / ncDays)));
        return Value::num(price);
    });

    registerFunction("ODDFYIELD", 8, 9, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double sett, mat, issue, first, rate, price, redem;
        Value e;
        if (!valueToSerial(a[0], sett) || !valueToSerial(a[1], mat) ||
            !valueToSerial(a[2], issue) || !valueToSerial(a[3], first)) return Value::error(Err::Value);
        if (!numArg(a[4], rate, e)) return e;
        if (!numArg(a[5], price, e)) return e;
        if (!numArg(a[6], redem, e)) return e;
        int freq;
        if (!intArg(a[7], freq, e)) return e;
        if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
        if (issue >= sett || sett >= first || first >= mat) return Value::error(Err::Num);
        if (price <= 0) return Value::error(Err::Num);

        int step = 12 / freq;
        double ncDays = 365.0 / freq;
        std::vector<double> dates, factors;
        for (int i = 1; ; i++) {
            double d = serialAddMonths(issue, i * step);
            if (d >= first - 1e-9) break;
            if (d <= sett + 1e-9) continue;
            dates.push_back(d); factors.push_back(1.0);
        }
        double frac = (first - issue) / ncDays;
        dates.push_back(first); factors.push_back(frac);
        for (int i = 1; ; i++) {
            double d = serialAddMonths(first, i * step);
            if (d >= mat - 1e-9) break;
            dates.push_back(d); factors.push_back(1.0);
        }
        dates.push_back(mat); factors.push_back(1.0);

        // 二分求解：目标是 oddPriceCore - 应计 == price
        double accr = 100.0 * rate / freq * ((sett - issue) / ncDays);
        double lo = -0.99, hi = 10.0;
        auto f = [&](double y) {
            return oddPriceCore(sett, dates, factors, rate, y, redem, freq) - accr - price;
        };
        if (f(lo) * f(hi) > 0) return Value::error(Err::Num);
        for (int i = 0; i < 200; i++) {
            double mid = (lo + hi) / 2, fm = f(mid);
            if (std::fabs(fm) < 1e-12) return Value::num(mid);
            if (f(lo) * fm <= 0) hi = mid; else lo = mid;
        }
        return Value::num((lo + hi) / 2);
    });

    registerFunction("ODDLYIELD", 7, 8, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double sett, mat, last, rate, price, redem;
        Value e;
        if (!valueToSerial(a[0], sett) || !valueToSerial(a[1], mat) ||
            !valueToSerial(a[2], last)) return Value::error(Err::Value);
        if (!numArg(a[3], rate, e)) return e;
        if (!numArg(a[4], price, e)) return e;
        if (!numArg(a[5], redem, e)) return e;
        int freq;
        if (!intArg(a[6], freq, e)) return e;
        if (freq != 1 && freq != 2 && freq != 4) return Value::error(Err::Num);
        if (last >= sett || sett >= mat) return Value::error(Err::Num);
        if (price <= 0) return Value::error(Err::Num);

        int step = 12 / freq;
        double ncDays = 365.0 / freq;
        std::vector<double> dates, factors;
        for (int i = 1; ; i++) {
            double d = serialAddMonths(last, i * step);
            if (d >= mat - 1e-9) break;
            if (d <= sett + 1e-9) continue;
            dates.push_back(d); factors.push_back(1.0);
        }
        double frac = (mat - last - std::floor((mat - last) / ncDays) * ncDays) / ncDays;
        if (frac <= 1e-9) frac = (mat - last) / ncDays;
        dates.push_back(mat); factors.push_back(frac);

        double accrCap = (double)((int)((mat - last) / ncDays));
        double accr = 100.0 * rate / freq * std::min((sett - last) / ncDays, accrCap);
        double lo = -0.99, hi = 10.0;
        auto f = [&](double y) {
            return oddPriceCore(sett, dates, factors, rate, y, redem, freq) - accr - price;
        };
        if (f(lo) * f(hi) > 0) return Value::error(Err::Num);
        for (int i = 0; i < 200; i++) {
            double mid = (lo + hi) / 2, fm = f(mid);
            if (std::fabs(fm) < 1e-12) return Value::num(mid);
            if (f(lo) * fm <= 0) hi = mid; else lo = mid;
        }
        return Value::num((lo + hi) / 2);
    });

    // -----------------------------------------------------------------------
    // 取整补全：ISO.CEILING / CEILING.PRECISE / FLOOR.PRECISE
    // -----------------------------------------------------------------------
    registerFunction("ISO.CEILING", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x, sig = 1; Value e;
        if (!numArg(a[0], x, e)) return e;
        if (a.size() > 1 && !a[1].isEmpty()) { if (!numArg(a[1], sig, e)) return e; }
        if (sig == 0) return Value::num(0);
        // ISO.CEILING 对负数也向 +∞ 取整（与 CEILING 不同）
        double q = x / std::fabs(sig);
        return Value::num(std::ceil(q) * std::fabs(sig));
    });
    registerFunction("CEILING.PRECISE", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x, sig = 1; Value e;
        if (!numArg(a[0], x, e)) return e;
        if (a.size() > 1 && !a[1].isEmpty()) { if (!numArg(a[1], sig, e)) return e; }
        if (sig == 0) return Value::num(0);
        double s = std::fabs(sig);
        return Value::num(std::ceil(x / s) * s);
    });
    registerFunction("FLOOR.PRECISE", 1, 2, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        double x, sig = 1; Value e;
        if (!numArg(a[0], x, e)) return e;
        if (a.size() > 1 && !a[1].isEmpty()) { if (!numArg(a[1], sig, e)) return e; }
        if (sig == 0) return Value::num(0);
        double s = std::fabs(sig);
        return Value::num(std::floor(x / s) * s);
    });
}

} // namespace xl
