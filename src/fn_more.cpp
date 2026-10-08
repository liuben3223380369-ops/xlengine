#include "functions.hpp"
#include "date.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <iomanip>

namespace xl {

static bool n1(const Value& v, double& out) {
    Value e; if (!toNumber(v, out, e)) return false; return true;
}
static bool i1(const Value& v, int& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    double d; Value e; if (!toNumber(v, d, e)) { err = e; return false; }
    out = (int)std::floor(d); return true;
}
static bool s1(const Value& v, std::string& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    if (!toText(v, out, err)) return false;
    return true;
}

// ===========================================================================
// 正则（Excel 2024 / 365 新增）
// ===========================================================================
struct RegexRegistrar {
    RegexRegistrar() {
        registerFunction("REGEXEXTRACT", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string text, pat; Value e;
            if (!s1(a[0], text, e)) return e;
            if (!s1(a[1], pat, e)) return e;
            int mode = 0;   // 0=首个匹配 1=全部 2=捕获组
            if (a.size() >= 3 && !i1(a[2], mode, e)) return e;
            try {
                std::regex re(pat);
                if (mode == 1) {
                    auto arr = std::make_shared<Array2D>();
                    std::vector<Value> row;
                    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
                        row.push_back(Value::str(it->str()));
                    if (row.empty()) return Value::error(Err::NA);
                    arr->push_back(row);
                    return Value::array(arr);
                }
                std::smatch m;
                if (!std::regex_search(text, m, re)) return Value::error(Err::NA);
                if (mode == 2 && m.size() > 1) {
                    auto arr = std::make_shared<Array2D>();
                    std::vector<Value> row;
                    for (size_t i = 1; i < m.size(); i++) row.push_back(Value::str(m[i].str()));
                    arr->push_back(row);
                    return Value::array(arr);
                }
                return Value::str(m.str());
            } catch (...) { return Value::error(Err::Value); }
        });
        registerFunction("REGEXREPLACE", 3, 4, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string text, pat, rep; Value e;
            if (!s1(a[0], text, e)) return e;
            if (!s1(a[1], pat, e)) return e;
            if (!s1(a[2], rep, e)) return e;
            int occ = 0;   // 0 = 全部替换
            if (a.size() >= 4 && !i1(a[3], occ, e)) return e;
            try {
                std::regex re(pat);
                if (occ == 0) return Value::str(std::regex_replace(text, re, rep));
                std::string out = text;
                std::smatch m;
                auto begin = out.cbegin();
                int cnt = 0;
                std::string result;
                while (std::regex_search(begin, out.cend(), m, re)) {
                    result.append(begin, m[0].first);
                    cnt++;
                    if (cnt == occ) { result.append(rep); begin = m[0].second; break; }
                    result.append(m[0].str());
                    begin = m[0].second;
                    if (m[0].first == m[0].second) {
                        if (begin == out.cend()) break;
                        result.push_back(*begin++);
                    }
                }
                result.append(begin, out.cend());
                return Value::str(result);
            } catch (...) { return Value::error(Err::Value); }
        });
        registerFunction("REGEXTEST", 2, 3, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string text, pat; Value e;
            if (!s1(a[0], text, e)) return e;
            if (!s1(a[1], pat, e)) return e;
            bool cs = false;
            if (a.size() >= 3) { int f; if (!i1(a[2], f, e)) return e; cs = (f != 0); }
            try {
                auto flags = std::regex::ECMAScript;
                if (!cs) flags |= std::regex::icase;
                std::regex re(pat, flags);
                return Value::boolean(std::regex_search(text, re));
            } catch (...) { return Value::error(Err::Value); }
        });
    }
};

// ===========================================================================
// 文本新函数
// ===========================================================================
struct Text2Registrar {
    Text2Registrar() {
        registerFunction("TEXTBEFORE", 2, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string text, delim; Value e;
            if (!s1(a[0], text, e)) return e;
            if (!s1(a[1], delim, e)) return e;
            int nth = 1;
            if (a.size() >= 3 && !i1(a[2], nth, e)) return e;
            if (delim.empty()) return Value::error(Err::Value);
            size_t pos = std::string::npos;
            size_t from = 0;
            for (int k = 0; k < nth; k++) {
                pos = text.find(delim, from);
                if (pos == std::string::npos) break;
                if (k + 1 < nth) from = pos + delim.size();
            }
            if (pos == std::string::npos) {
                if (a.size() >= 5) return a[4];     // if_not_found（第 5 参）
                return Value::error(Err::NA);
            }
            return Value::str(text.substr(0, pos));
        });
        registerFunction("TEXTAFTER", 2, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string text, delim; Value e;
            if (!s1(a[0], text, e)) return e;
            if (!s1(a[1], delim, e)) return e;
            int nth = 1;
            if (a.size() >= 3 && !i1(a[2], nth, e)) return e;
            if (delim.empty()) return Value::error(Err::Value);
            size_t from = 0, pos = std::string::npos;
            for (int k = 0; k < nth; k++) {
                pos = text.find(delim, from);
                if (pos == std::string::npos) break;
                from = pos + delim.size();
            }
            if (pos == std::string::npos) {
                if (a.size() >= 5) return a[4];
                return Value::error(Err::NA);
            }
            return Value::str(text.substr(from));
        });
        registerFunction("TEXTSPLIT", 2, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string text, cd; Value e;
            if (!s1(a[0], text, e)) return e;
            if (!s1(a[1], cd, e)) return e;
            std::string rd;
            if (a.size() >= 3) {
                // 行分隔符可能缺失，此时是第 4 个参数位置的 skip_empty
                std::string maybe;
                if (s1(a[2], maybe, e)) rd = maybe;
            }
            bool skipEmpty = false;
            if (a.size() >= 4) { int f; if (!i1(a[3], f, e)) return e; skipEmpty = (f != 0); }
            auto splitBy = [&](const std::string& s, const std::string& d) {
                std::vector<std::string> out;
                if (d.empty()) { out.push_back(s); return out; }
                size_t pos = 0;
                while (true) {
                    size_t p = s.find(d, pos);
                    if (p == std::string::npos) { out.push_back(s.substr(pos)); break; }
                    out.push_back(s.substr(pos, p - pos));
                    pos = p + d.size();
                }
                return out;
            };
            std::vector<std::string> rows = rd.empty() ? std::vector<std::string>{text} : splitBy(text, rd);
            auto arr = std::make_shared<Array2D>();
            for (auto& r : rows) {
                std::vector<std::string> cells = splitBy(r, cd);
                std::vector<Value> vrow;
                for (auto& c : cells) {
                    if (skipEmpty && c.empty()) continue;
                    vrow.push_back(Value::str(c));
                }
                arr->push_back(vrow);
            }
            return Value::array(arr);
        });
        registerFunction("PHONETIC", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            std::string s; Value e;
            if (!s1(a[0], s, e)) return e;
            return Value::str(s);
        });
        // 占位实现：真正的 BAHTTEXT 要输出泰文金额读法
        // （หนึ่งบาทถ้วน 那一套，含 สิบ/ร้อย/พัน/หมื่น/แสน/ล้าน 进位、
        //  และ "เอ็ด"/"ยี่" 等特殊读法）。这里不编造泰文，只给出可读的占位文本，
        // 并在 README 的已知边界里明确标注。
        registerFunction("BAHTTEXT", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double n; if (!n1(a[0], n)) return Value::error(Err::Value);
            std::ostringstream o;
            o << std::fixed << std::setprecision(2) << n << " Baht";
            return Value::str(o.str());
        });
    }
};

// ===========================================================================
// 财务补充：摊销、久期、贴现债券收益率
// ===========================================================================
struct Fin2Registrar {
    Fin2Registrar() {
        registerFunction("AMORLCOST", 6, 7, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double cost, datePurch, firstPeriod, salvage; Value e;
            if (!n1(a[0], cost) || !n1(a[3], salvage)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], datePurch) && !n1(a[1], datePurch)) return Value::error(Err::Value);
            if (!valueToSerial(a[2], firstPeriod) && !n1(a[2], firstPeriod)) return Value::error(Err::Value);
            int period; if (!i1(a[4], period, e)) return e;
            double rate; if (!n1(a[5], rate)) return Value::error(Err::Value);
            int basis = (a.size() >= 7) ? basisOf(a, 6) : 0;
            if (basis < 0 || rate <= 0) return Value::error(Err::Num);
            if (period < 0) return Value::error(Err::Num);
            double yf = yearFrac(datePurch, firstPeriod, basis);
            if (period == 0) return Value::num(cost * rate * yf);
            double total = cost * rate * yf;
            if (period >= 1) {
                // 之后每期按直线法（法国会计体系）
                double life = 1.0 / rate;
                double per = (cost - salvage) / life;
                return Value::num(per);
            }
            return Value::num(total);
        });
        registerFunction("AMORDEGRC", 6, 7, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double cost, dp, fp, salvage; Value e;
            if (!n1(a[0], cost) || !n1(a[3], salvage)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], dp) && !n1(a[1], dp)) return Value::error(Err::Value);
            if (!valueToSerial(a[2], fp) && !n1(a[2], fp)) return Value::error(Err::Value);
            int period; if (!i1(a[4], period, e)) return e;
            double rate; if (!n1(a[5], rate)) return Value::error(Err::Value);
            int basis = (a.size() >= 7) ? basisOf(a, 6) : 0;
            if (basis < 0 || period < 0) return Value::error(Err::Num);
            // 法国递减折旧：系数随年限递减
            double life = 1.0 / rate;
            double yf = yearFrac(dp, fp, basis);
            if (period == 0) return Value::num(cost * rate * yf);
            double n = life;
            double coeff = (n - period + 1.0) / (n * (n + 1.0) / 2.0);
            double depr = (cost - salvage) * coeff;
            return Value::num(depr);
        });
        registerFunction("DURATION", 5, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, coup, yld; Value e;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], coup) || !n1(a[3], yld)) return Value::error(Err::Value);
            int freq; if (!i1(a[4], freq, e)) return e;
            int basis = (a.size() >= 7) ? basisOf(a, 6) : 0;
            if (freq < 1 || freq > 4 || basis < 0) return Value::error(Err::Num);
            if (coup < 0 || yld < 0 || st >= mt) return Value::error(Err::Num);
            // Macaulay 久期
            double n = std::max(1.0, std::ceil((mt - st) / (365.25 / freq)));
            double r = yld / freq;
            double c = 100.0 * coup / freq;
            double pvSum = 0, wSum = 0;
            for (int k = 1; k <= (int)n; k++) {
                double cf = (k == (int)n) ? (c + 100.0) : c;
                double pv = cf / std::pow(1.0 + r, k);
                pvSum += pv;
                wSum += k * pv;
            }
            if (pvSum == 0) return Value::error(Err::Num);
            return Value::num((wSum / pvSum) / freq);
        });
        registerFunction("MDURATION", 5, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            Value d = findFunction("DURATION")->impl(a, *(EvalCtx*)nullptr);
            if (d.isError()) return d;
            double yld; if (!n1(a[3], yld)) return Value::error(Err::Value);
            int freq; Value e; if (!i1(a[4], freq, e)) return e;
            if (freq < 1) return Value::error(Err::Num);
            return Value::num(d.n / (1.0 + yld / freq));
        });
        registerFunction("YIELDDISC", 4, 5, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, pr, red;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!n1(a[2], pr) || !n1(a[3], red)) return Value::error(Err::Value);
            int basis = (a.size() >= 5) ? basisOf(a, 4) : 0;
            if (pr <= 0 || red <= 0 || st >= mt || basis < 0) return Value::error(Err::Num);
            double frac = yearFrac(st, mt, basis);
            if (frac == 0) return Value::error(Err::Div0);
            return Value::num((red - pr) / (pr * frac) * (basis == 2 || basis == 0 ? 1.0 : 1.0));
        });
        registerFunction("YIELDMAT", 5, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            double st, mt, issue, rate, pr;
            if (!valueToSerial(a[0], st) && !n1(a[0], st)) return Value::error(Err::Value);
            if (!valueToSerial(a[1], mt) && !n1(a[1], mt)) return Value::error(Err::Value);
            if (!valueToSerial(a[2], issue) && !n1(a[2], issue)) return Value::error(Err::Value);
            if (!n1(a[3], rate) || !n1(a[4], pr)) return Value::error(Err::Value);
            int basis = (a.size() >= 6) ? basisOf(a, 5) : 0;
            if (rate < 0 || pr <= 0 || st >= mt || basis < 0) return Value::error(Err::Num);
            double dim = yearFrac(st, mt, basis);
            double a2 = yearFrac(issue, st, basis);
            double b2 = yearFrac(issue, mt, basis);
            double num = 100.0 + rate * 100.0 * b2 - pr - rate * 100.0 * a2 * pr / 100.0;
            double den = pr + rate * 100.0 * a2;
            if (den == 0 || dim == 0) return Value::error(Err::Div0);
            return Value::num(num / den / dim);
        });
    }
};

// ===========================================================================
// 工程补充：双曲函数
// ===========================================================================
struct Eng2Registrar {
    Eng2Registrar() {
        registerFunction("IMTAN", 1, 1, [](const std::vector<Value>& a, EvalCtx&) -> Value {
            // tan(a+bi) = (sin 2a + i·sinh 2b) / (cos 2a + cosh 2b)
            //
            // 原先这里内联了一套自己的解析+格式化，于是虚部为 0 时输出
            // "0+0i"，而其余复数函数（IMSIN/IMCOS/…）输出 "0" —— 同一个
            // 库里两种格式。现在改用共享的 parseCx / cxStr，顺带也接上了
            // 浮点噪声清理（如 1.2e-16 被归零）。
            Cx c; if (!parseCx(a[0], c)) return Value::error(Err::Num);
            double den = std::cos(2 * c.re) + std::cosh(2 * c.im);
            if (std::fabs(den) < 1e-15) return Value::error(Err::Num);
            return Value::str(cxStr(Cx{std::sin(2 * c.re) / den,
                                       std::sinh(2 * c.im) / den}));
        });
    }
};

static RegexRegistrar g_reReg;
static Text2Registrar g_text2Reg;
static Fin2Registrar  g_fin2Reg;
static Eng2Registrar  g_eng2Reg;

} // namespace xl
