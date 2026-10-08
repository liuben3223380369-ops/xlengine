// 补齐批次 D：FORECAST.ETS 系列（指数平滑预测）
//
// Excel 2016 引入，用 Holt-Winters 三重指数平滑做时序预测。
// 这里实现的是可验证的核心版本：
//   - 数据补全：缺失点线性插值（Excel 的 data_completion=1）
//   - 季节周期：由参数指定或自动检测（自相关法）
//   - 模型：加法 Holt-Winters（level + trend + seasonal）
// 没有覆盖 Excel 的全部选项（乘法季节性、聚合方式等），README 已标注。
#include "functions.hpp"
#include "value.hpp"
#include "ast.hpp"
#include "date.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

namespace xl {

namespace {

bool numArg(const Value& v, double& out, Value& err) {
    if (v.isError()) { err = v; return false; }
    Value e;
    if (!toNumber(v, out, e)) { err = e; return false; }
    return true;
}
void nums(const std::vector<Value>& a, std::vector<double>& out, Value& err) {
    std::vector<Value> v;
    if (!flattenArgs(a, v, true, err)) return;
    for (auto& x : v) { double d; Value e; if (toNumber(x, d, e)) out.push_back(d); }
}

// 用自相关粗估季节周期：找使自相关最大的 lag（2..n/2）
int detectSeason(const std::vector<double>& y) {
    int n = (int)y.size();
    if (n < 4) return 0;
    double mean = 0; for (double v : y) mean += v; mean /= n;
    double var = 0; for (double v : y) var += (v - mean) * (v - mean);
    if (var < 1e-12) return 0;
    int best = 0; double bestR = 0;
    for (int lag = 2; lag <= n / 2; lag++) {
        double s = 0;
        for (int i = 0; i + lag < n; i++) s += (y[i] - mean) * (y[i + lag] - mean);
        double r = s / var;
        if (r > bestR) { bestR = r; best = lag; }
    }
    // 阈值过低说明没有明显季节性
    return bestR > 0.3 ? best : 0;
}

// 加法 Holt-Winters
struct HWModel {
    double level = 0, trend = 0;
    std::vector<double> seas;
    int period = 0;
    double alpha = 0.3, beta = 0.1, gamma = 0.2;
    double residVar = 0;

    void fit(const std::vector<double>& y, int p) {
        int n = (int)y.size();
        period = p;
        seas.assign(period > 0 ? period : 0, 0.0);
        if (n < 2) { level = y.empty() ? 0 : y[0]; return; }

        if (period > 0 && n >= 2 * period) {
            // 初始季节因子：各周期内均值减去总均值
            double mean = 0; for (double v : y) mean += v; mean /= n;
            for (int s = 0; s < period; s++) {
                double sum = 0; int cnt = 0;
                for (int i = s; i < n; i += period) { sum += y[i]; cnt++; }
                seas[s] = cnt ? (sum / cnt - mean) : 0;
            }
            // 初始趋势：跨周期均值差
            double first = 0, last = 0; int c = 0;
            for (int i = 0; i < period; i++) { first += y[i]; last += y[n - period + i]; c++; }
            trend = c ? ((last - first) / c) / ((double)(n - period) / period) : 0;
            level = mean;
        } else {
            period = 0; seas.clear();
            level = y[0];
            trend = (y.back() - y.front()) / (n - 1);
        }

        double sse = 0; int cnt = 0;
        for (int t = 0; t < n; t++) {
            double obs = y[t];
            double s = period > 0 ? seas[t % period] : 0;
            double pred = level + trend + s;
            sse += (obs - pred) * (obs - pred); cnt++;
            double newLevel = alpha * (obs - s) + (1 - alpha) * (level + trend);
            double newTrend = beta * (newLevel - level) + (1 - beta) * trend;
            if (period > 0) seas[t % period] = gamma * (obs - newLevel) + (1 - gamma) * s;
            level = newLevel; trend = newTrend;
        }
        residVar = cnt > 1 ? sse / (cnt - 1) : 0;
    }

    // 预测第 h 步（h >= 1）
    double forecast(int h) const {
        double s = (period > 0) ? seas[(h - 1) % period] : 0;
        return level + trend * h + s;
    }
};

// 统一的 ETS 拟合入口
// seasonalityIdx：季节周期参数在参数列表里的位置。
// 各函数并不一致 —— ETS / SEASONALITY 放在第 4 位，
// 而 CONFINT / STAT 的第 4 位分别是置信水平和统计量类型，季节参数被挤到第 5 位。
// 统一用 3 会让 STAT 把 statistic_type=99 当成周期 99，直接报 #NUM!。
bool etsFit(const std::vector<Value>& a, HWModel& m, double& targetX,
            std::vector<double>& xs, std::vector<double>& ys, Value& err,
            size_t seasonalityIdx = 3) {
    if (!numArg(a[0], targetX, err)) return false;
    nums({a[1]}, ys, err); if (err.isError()) return false;
    nums({a[2]}, xs, err); if (err.isError()) return false;
    if (ys.size() != xs.size() || ys.size() < 2) { err = Value::error(Err::Num); return false; }

    // 时间轴必须递增
    for (size_t i = 1; i < xs.size(); i++)
        if (xs[i] <= xs[i - 1]) { err = Value::error(Err::Value); return false; }

    // 缺失值线性插值
    for (size_t i = 0; i < ys.size(); i++)
        if (!std::isfinite(ys[i])) {
            size_t j = i; while (j < ys.size() && !std::isfinite(ys[j])) j++;
            if (i == 0 || j >= ys.size()) { err = Value::error(Err::Num); return false; }
            double y0 = ys[i - 1], y1 = ys[j];
            for (size_t k = i; k < j; k++)
                ys[k] = y0 + (y1 - y0) * (xs[k] - xs[i - 1]) / (xs[j] - xs[i - 1]);
            i = j - 1;
        }

    int period = 0;
    if (a.size() > seasonalityIdx && !a[seasonalityIdx].isEmpty()) {
        double sp;
        if (!numArg(a[seasonalityIdx], sp, err)) return false;
        period = (int)std::floor(sp);
        if (period == 1) period = 0;                     // 1 表示无季节性
        if (period < 0 || period > (int)ys.size() / 2) { err = Value::error(Err::Num); return false; }
    } else {
        period = detectSeason(ys);
    }
    m.fit(ys, period);
    return true;
}

// 时间轴的平均步长
double avgStep(const std::vector<double>& xs) {
    if (xs.size() < 2) return 1;
    return (xs.back() - xs.front()) / (xs.size() - 1);
}

} // namespace

struct Fill4Registrar {
    Fill4Registrar();
};
static Fill4Registrar g_fill4Reg;

Fill4Registrar::Fill4Registrar() {
    registerFunction("FORECAST.ETS", 3, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        HWModel m; double target; std::vector<double> xs, ys; Value e;
        if (!etsFit(a, m, target, xs, ys, e)) return e;
        double step = avgStep(xs);
        if (step <= 0) return Value::error(Err::Num);
        // 步数：从最后一个观测点到目标点的步长倍数
        double h = (target - xs.back()) / step;
        if (h <= 0) {
            // 目标在历史区间内：直接取插值/拟合值
            int idx = 0;
            for (size_t i = 0; i + 1 < xs.size(); i++)
                if (target >= xs[i] && target <= xs[i + 1]) { idx = (int)i; break; }
            double t = (target - xs[idx]) / step;
            return Value::num(m.level + m.trend * (idx + 1 + t));
        }
        int hi = (int)std::round(h);
        if (hi < 1) hi = 1;
        double whole = m.forecast(hi);
        // 非整数步：在相邻两步之间线性插值
        double frac = h - std::floor(h);
        if (std::fabs(frac) > 1e-9 && std::floor(h) >= 1) {
            double prev = m.forecast((int)std::floor(h));
            whole = prev + (whole - prev) * frac;
        }
        return Value::num(whole);
    });

    registerFunction("FORECAST.ETS.CONFINT", 3, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        HWModel m; double target; std::vector<double> xs, ys; Value e;
        // CONFINT 的第 4 位是置信水平，季节周期挪到第 5 位
        if (!etsFit(a, m, target, xs, ys, e, 4)) return e;
        double conf = 0.95;
        if (a.size() > 3 && !a[3].isEmpty()) {
            if (!numArg(a[3], conf, e)) return e;
            // 允许 0.95 或 95 两种写法
            if (conf > 1) conf /= 100.0;
            if (conf <= 0 || conf >= 1) return Value::error(Err::Num);
        }
        double step = avgStep(xs);
        double h = std::max(1.0, (target - xs.back()) / step);
        if (step <= 0) return Value::error(Err::Num);
        // 用正态近似的 z 值；Excel 用 t 分布，这里简化为正态
        double z = 1.959963984540054;                    // 95%
        if (std::fabs(conf - 0.99) < 1e-9) z = 2.5758293035489004;
        else if (std::fabs(conf - 0.9) < 1e-9) z = 1.6448536269514722;
        double sd = std::sqrt(std::max(0.0, m.residVar));
        return Value::num(z * sd * std::sqrt(h));
    });

    registerFunction("FORECAST.ETS.SEASONALITY", 3, 6, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        HWModel m; double target; std::vector<double> xs, ys; Value e;
        if (!etsFit(a, m, target, xs, ys, e, 3)) return e;   // 季节周期在第 4 位
        // 返回检测到的季节周期长度，无季节性时为 0
        return Value::num((double)m.period);
    });

    registerFunction("FORECAST.ETS.STAT", 4, 7, [](const std::vector<Value>& a, EvalCtx&) -> Value {
        HWModel m; double target; std::vector<double> xs, ys; Value e;
        // STAT 的第 4 位是统计量类型，季节周期挪到第 5 位
        if (!etsFit(a, m, target, xs, ys, e, 4)) return e;
        int stat;
        double d;
        if (!numArg(a[3], d, e)) return e;
        stat = (int)std::floor(d);
        // 只实现有明确定义的统计量，其余返回 #N/A 而不是编造数值
        switch (stat) {
            case 1:  return Value::num(m.alpha);          // Alpha
            case 2:  return Value::num(m.beta);           // Beta
            case 3:  return Value::num(m.gamma);          // Gamma
            // MASE 需要"朴素季节性预测"的残差序列，这里没有保存该序列。
            // 上方注释写明"返回 #N/A 而不是编造数值"，原先却返回硬编码 1.0 ——
            // 那正是编造，而且会让调用方误以为模型质量很好（MASE<1 才算可用）。
            case 4:  return Value::error(Err::NA);
            case 5:  return Value::num(std::sqrt(std::max(0.0, m.residVar)));  // 残差标准差
            case 6:  return Value::num(m.residVar);       // 残差方差
            default: return Value::error(Err::NA);
        }
    });

    // FORECAST.LINEAR 与 FORECAST 同义（Excel 保留旧名 FORECAST）
    registerFunction("FORECAST.LINEAR", 3, 3, [](const std::vector<Value>& a, EvalCtx& c) -> Value {
        return findFunction("FORECAST")->impl(a, c);
    });
}

} // namespace xl
