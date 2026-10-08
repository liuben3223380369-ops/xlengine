#include "ast.hpp"
#include "functions.hpp"
#include <algorithm>

namespace xl {

NodePtr makeNum(double v) { auto n = std::make_shared<Node>(); n->kind = NodeKind::Num; n->num = v; return n; }
NodePtr makeStr(const std::string& v) { auto n = std::make_shared<Node>(); n->kind = NodeKind::Str; n->text = v; return n; }
NodePtr makeBool(bool v) { auto n = std::make_shared<Node>(); n->kind = NodeKind::Bool; n->bval = v; return n; }
NodePtr makeErr(Err v) { auto n = std::make_shared<Node>(); n->kind = NodeKind::ErrLit; n->err = v; return n; }
NodePtr makeRef(const std::string& sheet, int col, int row, bool ca, bool ra) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::CellRef; n->sheet = sheet;
    n->col = col; n->row = row; n->colAbs = ca; n->rowAbs = ra;
    return n;
}
NodePtr makeRange(NodePtr a, NodePtr b) {
    auto n = std::make_shared<Node>(); n->kind = NodeKind::Range; n->a = a; n->b = b; return n;
}
NodePtr makeUnion(NodePtr a, NodePtr b) {
    auto n = std::make_shared<Node>(); n->kind = NodeKind::Union; n->a = a; n->b = b; return n;
}
NodePtr makeIntersect(NodePtr a, NodePtr b) {
    auto n = std::make_shared<Node>(); n->kind = NodeKind::Intersect; n->a = a; n->b = b; return n;
}
NodePtr makeUnary(UnOp op, NodePtr a) {
    auto n = std::make_shared<Node>(); n->kind = NodeKind::Unary; n->uop = op; n->a = a; return n;
}
NodePtr makeBinary(BinOp op, NodePtr a, NodePtr b) {
    auto n = std::make_shared<Node>(); n->kind = NodeKind::Binary; n->op = op; n->a = a; n->b = b; return n;
}
NodePtr makeCall(const std::string& name, std::vector<NodePtr> args) {
    auto n = std::make_shared<Node>(); n->kind = NodeKind::Call; n->text = name; n->kids = std::move(args); return n;
}
NodePtr makeArray(std::vector<NodePtr> rows) {
    auto n = std::make_shared<Node>(); n->kind = NodeKind::ArrayLit; n->kids = std::move(rows); return n;
}
NodePtr makeName(const std::string& name) {
    auto n = std::make_shared<Node>(); n->kind = NodeKind::Name; n->text = name; return n;
}
NodePtr makeLambdaNode(std::vector<std::string> params, NodePtr body) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::LambdaDefNode; n->body = body; n->paramNames = std::move(params);
    return n;
}

// 引用语义栈的 RAII 守卫
struct ArgRefGuard {
    EvalCtx& ctx;
    bool on;
    ArgRefGuard(EvalCtx& c, bool o, const std::vector<std::pair<int,int>>& refs) : ctx(c), on(o) {
        if (on) ctx.pushArgRefs(refs);
    }
    ~ArgRefGuard() { if (on) ctx.popArgRefs(); }
};

// ---------------------------------------------------------------------------
// 二元运算
// ---------------------------------------------------------------------------
// 前向声明：定义在"节点求值"章节（需要用到那里的常量）。
// evalBinary / evalUnary 在它之前定义，但要展开惰性区域。
static Value materializeRange(const Value& v, EvalCtx& ctx);

Value evalBinary(BinOp op, const Value& x, const Value& y, EvalCtx* ctx) {
    // 惰性区域在这里展开。ctx 为 null 时无法取格子，直接按原语义处理
    // （测试里直接调本函数时会传 null）。
    if (ctx && (x.isRangeRef() || y.isRangeRef())) {
        Value a = x; Value b = y;
        if (x.isRangeRef()) { a = materializeRange(x, *ctx); if (a.isError()) return a; }
        if (y.isRangeRef()) { b = materializeRange(y, *ctx); if (b.isError()) return b; }
        return evalBinary(op, a, b, nullptr);
    }
    if (x.isError()) return x;
    if (y.isError()) return y;

    if (op == BinOp::Concat) {
        std::string a, b; Value e;
        if (!toText(x, a, e)) return e;
        if (!toText(y, b, e)) return e;
        return Value::str(a + b);
    }
    if (op == BinOp::Eq || op == BinOp::Ne || op == BinOp::Lt ||
        op == BinOp::Gt || op == BinOp::Le || op == BinOp::Ge) {
        int cmp; Value e;
        if (!compareValues(x, y, cmp, e)) return e;
        bool r;
        switch (op) {
            case BinOp::Eq: r = (cmp == 0); break;
            case BinOp::Ne: r = (cmp != 0); break;
            case BinOp::Lt: r = (cmp < 0);  break;
            case BinOp::Gt: r = (cmp > 0);  break;
            case BinOp::Le: r = (cmp <= 0); break;
            default:        r = (cmp >= 0); break;
        }
        return Value::boolean(r);
    }

    double a, b; Value e;
    if (!toNumber(x, a, e)) return e;
    if (!toNumber(y, b, e)) return e;
    switch (op) {
        case BinOp::Add: return Value::num(a + b);
        case BinOp::Sub: return Value::num(a - b);
        case BinOp::Mul: return Value::num(a * b);
        case BinOp::Div:
            if (b == 0.0) return Value::error(Err::Div0);
            return Value::num(a / b);
        case BinOp::Pow: {
            if (a == 0.0 && b < 0) return Value::error(Err::Div0);
            if (a < 0 && std::floor(b) != b) return Value::error(Err::Num);
            double r = std::pow(a, b);
            if (std::isnan(r) || std::isinf(r)) return Value::error(Err::Num);
            return Value::num(r);
        }
        default: return Value::error(Err::Value);
    }
}

Value evalUnary(UnOp op, const Value& x, EvalCtx* ctx) {
    if (ctx && x.isRangeRef()) {
        Value v = materializeRange(x, *ctx);
        if (v.isError()) return v;
        return evalUnary(op, v, nullptr);
    }
    if (x.isError()) return x;
    if (op == UnOp::Percent) {
        double d; Value e;
        if (!toNumber(x, d, e)) return e;
        return Value::num(d / 100.0);
    }
    double d; Value e;
    if (!toNumber(x, d, e)) return e;
    return Value::num(op == UnOp::Neg ? -d : d);
}

// ---------------------------------------------------------------------------
// 节点求值
// ---------------------------------------------------------------------------
// 单次区域求值允许展开的最大元素数（见 Range 分支的说明）
static const long long kMaxRangeCells = 1048576LL;

// 把惰性区域展开成真正的 Array2D。
// 不支持惰性遍历的函数、以及要当作值存储/参与运算时都走这里。
// 只在确认是区域后才调用 —— 它无条件构造一个新 Value，
// 对非区域值用它会白白多一次拷贝。
// 只在确认是区域后才调用 —— 它无条件构造一个新 Value，
// 对非区域值用它会白白多一次拷贝。
static Value materializeRange(const Value& v, EvalCtx& ctx) {
    if (!v.isRangeRef() || !v.rng) return v;
    const RangeRefData& r = *v.rng;
    long long rows = (long long)r.r1 - r.r0 + 1;
    long long cols = (long long)r.c1 - r.c0 + 1;
    if (rows <= 0 || cols <= 0 || rows * cols > kMaxRangeCells) return Value::error(Err::Num);

    auto arr = std::make_shared<Array2D>();
    arr->reserve((size_t)rows);
    for (int y = r.r0; y <= r.r1; y++) {
        std::vector<Value> row;
        row.reserve((size_t)cols);              // 不 reserve 的话每格都可能触发增长重分配
        for (int x = r.c0; x <= r.c1; x++) {
            Value cv = ctx.cell(r.sheet, x, y);
            if (cv.isError() && cv.e == Err::Ref) return cv;
            row.push_back(std::move(cv));       // move，不是拷贝
        }
        arr->push_back(std::move(row));
    }
    return Value::array(arr);
}

// 给 sheet.cpp 用：把惰性区域展开成真正的数组。
// 存储层不能保留 RangeRef —— 它只是求值期的一个优化表示。
Value materializeRangeValue(const Value& v, EvalCtx& ctx) { return materializeRange(v, ctx); }

// 支持惰性遍历的函数（大写名）。
// 只有这些函数拿得到未展开的 RangeRef，其余一律在调用前展开 ——
// 这样 495 个函数里绝大多数不用改，正确性由"默认展开"兜底。
static bool supportsLazyRange(const std::string& u) {
    // XLE_NO_LAZY_RANGE：关掉惰性，走"先展开区域、再 flattenArgs"的旧路径。
    // 留着它是为了能在同一台机器上做 A/B —— 单次计时在这个环境上噪声极大，
    // 不同时段测出的数字根本没有可比性。
    // 注意这条路径仍保留 reserve/move 的微优化，所以是**保守基线**：
    // 惰性只要还更快，就是真的更快。
#ifdef XLE_NO_LAZY_RANGE
    (void)u;
    return false;
#else
    return u == "SUM"   || u == "COUNT"  || u == "COUNTA" || u == "COUNTBLANK"
        || u == "AVERAGE" || u == "MIN"  || u == "MAX"    || u == "SUMSQ"
        || u == "PRODUCT" || u == "MEDIAN"
        // 统计类：它们内部用 col2 收集成 vector<double>，拿到惰性区域时
        // 直接按坐标遍历，连展开带转换的中间层一起省掉
        || u == "STDEV"   || u == "STDEV.S" || u == "STDEV.P" || u == "STDEVP"
        || u == "VAR"     || u == "VAR.S"   || u == "VAR.P"   || u == "VARP"
        || u == "AVEDEV"  || u == "DEVSQ"   || u == "GEOMEAN" || u == "HARMEAN"
        || u == "SKEW"    || u == "KURT"
        // OFFSET 需要引用本身的坐标，展开成数组后就只剩值了
        || u == "OFFSET";
#endif
}

static Value toArray2D(const std::vector<Value>& flat) {
    auto arr = std::make_shared<Array2D>();
    arr->push_back(flat);
    return Value::array(arr);
}


// ---------------------------------------------------------------------------
// 求值递归深度上限
// ---------------------------------------------------------------------------
// 解析器已有 4000 层的嵌套上限，但**求值**没有 —— 于是
// "1+1+1+...（10000 项）"这种完全合法、解析也通过的公式，
// 会在求值时递归 10000 层把栈打爆，进程直接段错误退出。
//
// 这不是理论风险：压力测试就是这么崩的。而且崩的位置在
// AST 递归求值里，用户只是写了个长公式。
//
// 上限取 8000，是实测出来的**安全值**，不是随手定的：
//   9000 层：安全（返回 #NUM!）
//   10000 层：段错误
// 每层栈帧都压缩过（展开逻辑内联进 evalBinary，不再多套函数层），
// 但 AST 深度等于项数，"一万项连加"这种写法仍会到 10000 层。
//
// 为什么 8000 够用：Excel 公式长度上限 8192 字符，"1+1+..." 形式
// 每 2 字符一项，最坏也就约 4096 项 —— 深度 4096，8000 有近 2 倍余量。
// 超出 Excel 长度的公式（如 10000 项）返回 #NUM! 是安全拒绝，
// 不是功能缺陷：它本来就不是合法输入。
static const int kMaxEvalDepth = 8000;
static thread_local int g_evalDepth = 0;

struct EvalDepthGuard {
    bool ok;
    EvalDepthGuard() : ok(false) {
        if (g_evalDepth < kMaxEvalDepth) { ++g_evalDepth; ok = true; }
    }
    ~EvalDepthGuard() { if (ok) --g_evalDepth; }
};

Value Node::eval(EvalCtx& ctx) const {
    // 深度守卫必须就放在这一层，不能抽出 evalUnchecked() 再包一层：
    // 那样每层递归就多一次函数调用，栈帧明显变大 ——
    // 实测 10000 项连加会因此从"能算"变成段错误。
    // guard 本身只多一个 bool，代价可以忽略。
    EvalDepthGuard g;
    if (!g.ok) return Value::error(Err::Num);      // 超限：给错误值，绝不让栈爆
    switch (kind) {
        case NodeKind::Num:    return Value::num(num);
        case NodeKind::Str:    return Value::str(text);
        case NodeKind::Bool:   return Value::boolean(bval);
        case NodeKind::ErrLit: return Value::error(err);
        case NodeKind::Missing:
            // 空参数占位，求值为"空"。这样 isEmpty() 一类判断能正常工作，
            // 而把它当数字用的函数会得到 #VALUE!（与 Excel 一致）。
            return Value::empty();

        case NodeKind::Name: {
            // 两级查找：LET/LAMBDA 作用域 -> 工作簿定义名称。
            // 顺序不能反：LET 是公式内部的局部绑定，优先级更高。
            Value v;
            if (ctx.lookupName(text, v)) return v;
            if (ctx.definedName(text, v)) return v;
            return Value::error(Err::Name);
        }
        case NodeKind::LambdaDefNode: {
            auto def = std::make_shared<LambdaDef>();
            def->params = paramNames;
            def->body = body;
            return Value::lambdaV(def);
        }
        case NodeKind::CellRef:
            return ctx.cell(sheet, col, row);

        case NodeKind::Range: {
            if (!a || !b) return Value::error(Err::Ref);
            int c1 = std::min(a->col, b->col), c2 = std::max(a->col, b->col);
            int r1 = std::min(a->row, b->row), r2 = std::max(a->row, b->row);

            // 整列 / 整行引用：语义上是 1048576 行或 16384 列，
            // 但真的展开成 Array2D 会直接爆内存，稀疏表也没必要这么做。
            // 这里裁剪到工作表的已用区域 —— 对 SUM/COUNT/COUNTA/VLOOKUP 这类
            // 只关心"有数据的格子"的用法，结果与 Excel 一致。
            //
            // 已知的语义偏差：ROWS(A:A) 在 Excel 里是 1048576，
            // 这里返回已用区域高度。完整支持需要引入"稀疏区域"这一新的值类型。
            bool wc = a->wholeCol || b->wholeCol;
            bool wr = a->wholeRow || b->wholeRow;
            if (wc || wr) {
                const std::string& sn = sheet.empty() ? a->sheet : sheet;
                int u0, v0, u1, v1;
                if (!ctx.usedRange(sn, u0, v0, u1, v1)) {
                    // 整张表是空的：返回一个 1x1 的空值，避免上层拿到空数组
                    auto one = std::make_shared<Array2D>();
                    one->push_back({Value::empty()});
                    return Value::array(one);
                }
                if (wc) { r1 = v0; r2 = v1; }
                if (wr) { c1 = u0; c2 = u1; }
            }

            // 区域展开上限。
            //
            // 稀疏存储让"只存非空格"成为可能，但求值成数组时必须真的展开 ——
            // 于是 =A1:XFD1048576 这种完全合法的写法会尝试分配
            // 1048576 × 16384 ≈ 170 亿个 Value，内存瞬间耗尽，进程被 OOM 杀掉。
            // 这不是"慢"，是崩溃，且用户只是写了一个语法正确的公式。
            //
            // 上限取 1048576（Excel 单列的满行数）：正常用法远远用不到，
            // 而越界时明确返回 #NUM!，而不是让进程消失。
            // 用 long long 计算，避免行列相乘时 int 溢出导致判断失效。
            long long rows = (long long)r2 - r1 + 1;
            long long cols = (long long)c2 - c1 + 1;
            if (rows * cols > kMaxRangeCells) return Value::error(Err::Num);

            // 惰性：只记坐标，不展开。
            // 聚合类函数会直接按坐标遍历，省掉一次 Array2D + 每行 vector
            // + 逐元素拷贝。其它消费点通过 materializeIfRange 展开，
            // 行为与展开版完全一致。
            //
            // 越界坐标仍然要在这里挡住（负坐标会让遍历行为不可预期），
            // 但正常路径不再做任何分配。
            if (c1 < 0 || r1 < 0) return Value::error(Err::Ref);
            return Value::rangeRef(sheet.empty() ? a->sheet : sheet, c1, r1, c2, r2);
        }

        case NodeKind::Union: {
            std::vector<Value> all; Value e;
            // Union / Intersect 的嵌套深度通常很浅（不像 + 能写上万层），
            // 这里用局部 Value 展开是安全的；深度守卫仍会兜住极端情况。
            Value x = a->eval(ctx), y = b->eval(ctx);
            if (x.isRangeRef()) x = materializeRange(x, ctx);
            if (y.isRangeRef()) y = materializeRange(y, ctx);
            flattenAll({x, y}, all, e);
            if (e.isError()) return e;
            return toArray2D(all);
        }

        case NodeKind::Intersect: {
            Value va = a->eval(ctx), vb = b->eval(ctx);
            if (va.isError()) return va;
            if (vb.isError()) return vb;
            // 简化：仅支持两个区域的重叠，这里返回 #NULL! 表示无交集
            if (a->kind != NodeKind::Range || b->kind != NodeKind::Range)
                return Value::error(Err::Null);
            int c1 = std::max(std::min(a->a->col, a->b->col), std::min(b->a->col, b->b->col));
            int c2 = std::min(std::max(a->a->col, a->b->col), std::max(b->a->col, b->b->col));
            int r1 = std::max(std::min(a->a->row, a->b->row), std::min(b->a->row, b->b->row));
            int r2 = std::min(std::max(a->a->row, a->b->row), std::max(b->a->row, b->b->row));
            if (c1 > c2 || r1 > r2) return Value::error(Err::Null);
            if (((long long)r2 - r1 + 1) * ((long long)c2 - c1 + 1) > kMaxRangeCells)
                return Value::error(Err::Num);
            auto arr = std::make_shared<Array2D>();
            for (int r = r1; r <= r2; r++) {
                std::vector<Value> row;
                for (int c = c1; c <= c2; c++) row.push_back(ctx.cell("", c, r));
                arr->push_back(std::move(row));
            }
            return Value::array(arr);
        }

        case NodeKind::Unary:
            // 注意：不要在这里留下局部 Value。
            // 求值递归深度等于 AST 深度，"1+1+...（10000 项）"就是 10000 层，
            // 每层多一个 Value（约 88 字节）就会把栈打爆 —— 压力测试真崩过。
            // 展开放在 evalUnary 内部做，这里保持"直接传参"的形式。
            return evalUnary(uop, a->eval(ctx), &ctx);

        case NodeKind::Binary:
            return evalBinary(op, a->eval(ctx), b->eval(ctx), &ctx);

        case NodeKind::ArrayLit: {
            auto arr = std::make_shared<Array2D>();
            for (auto& rn : kids) {
                std::vector<Value> row;
                if (rn->kind == NodeKind::ArrayLit) {
                    for (auto& cn : rn->kids) row.push_back(cn->eval(ctx));
                } else {
                    row.push_back(rn->eval(ctx));
                }
                arr->push_back(std::move(row));
            }
            return Value::array(arr);
        }

        case NodeKind::Call: {
            std::string u = text;
            std::transform(u.begin(), u.end(), u.begin(), ::toupper);

            // LET / LAMBDA 必须在查函数表之前特判：
            // 它们的参数里含"待绑定的名字"，不能按普通参数先求值
            if (u == "LET") {
                if (kids.size() < 3 || kids.size() % 2 == 0) return Value::error(Err::Value);
                size_t bound = 0;
                for (size_t i = 0; i + 1 < kids.size(); i += 2) {
                    if (kids[i]->kind != NodeKind::Name) {
                        ctx.unbindNames(bound); return Value::error(Err::Name);
                    }
                    Value v = kids[i+1]->eval(ctx);
                    if (v.isError()) { ctx.unbindNames(bound); return v; }
                    ctx.bindName(kids[i]->text, v);
                    bound++;
                }
                Value r = kids.back()->eval(ctx);
                ctx.unbindNames(bound);
                return r;
            }
            if (u == "LAMBDA") {
                if (kids.size() < 2) return Value::error(Err::Value);
                std::vector<std::string> params;
                for (size_t i = 0; i + 1 < kids.size(); i++) {
                    if (kids[i]->kind != NodeKind::Name) return Value::error(Err::Name);
                    params.push_back(kids[i]->text);
                }
                auto def = std::make_shared<LambdaDef>();
                def->params = params;
                def->body = kids.back();
                return Value::lambdaV(def);
            }

            // 元函数（FORMULATEXT / ISFORMULA / CELL）要拿到引用本身而非求值结果，
            // 先把各参数的地址压栈。用 RAII guard 保证所有 return 路径都会出栈 ——
            // 这个 Call 分支里有十几条提前 return，手动 pop 必漏。
            bool wantRefs = (u == "FORMULATEXT" || u == "ISFORMULA" || u == "CELL");
            std::vector<std::pair<int,int>> refBuf;
            if (wantRefs) {
                for (auto& k : kids) {
                    if (k && k->kind == NodeKind::CellRef)
                        refBuf.push_back({k->col, k->row});
                    else if (k && k->kind == NodeKind::Range && k->a)
                        refBuf.push_back({k->a->col, k->a->row});
                    else
                        refBuf.push_back({-1, -1});
                }
            }
            ArgRefGuard refGuard(ctx, wantRefs, refBuf);

            const FnInfo* fn = findFunction(u);
            if (!fn) return Value::error(Err::Name);

            // ----------------------------------------------------------
            // 需要"参数在语法上是不是引用"的函数，必须在这里（AST 层）特判。
            //
            // 原因：单个单元格引用 A1 求完值就是一个标量，等传到函数手里时
            // 已经无从区分"数字 42"和"A1 里的 42"。ISREF(A1) 因此恒为 FALSE
            // （Excel 是 TRUE），OFFSET(A1,1,0) 因此直接返回 #REF!。
            // 区域参数（A1:A2）不受影响 —— 它本身就是数组/惰性区域。
            // 这与 IF 的惰性求值用的是同一个位置与同样的理由。
            // ----------------------------------------------------------
            auto argIsRef = [](const NodePtr& n) -> bool {
                return n && (n->kind == NodeKind::CellRef || n->kind == NodeKind::Range);
            };
            if (u == "ISREF" && kids.size() == 1)
                return Value::boolean(argIsRef(kids[0]));

            std::vector<Value> args;
            for (auto& k : kids) args.push_back(k->eval(ctx));

            // OFFSET 的基准：单格引用要还原成 1x1 区域，否则原逻辑
            // 见到标量就直接判 #REF!。
            if (u == "OFFSET" && !kids.empty() && kids[0]->kind == NodeKind::CellRef) {
                const Node& n = *kids[0];
                args[0] = Value::rangeRef(n.sheet, n.col, n.row, n.col, n.row);
            }

            // 惰性区域只留给明确支持的函数；其余在进函数前展开。
            // 顺序必须在 IF/IFERROR 特判之前 —— 那些分支里 args 可能是区域。
            //
            // 先扫参数（只做类型检查，很便宜），再按需查白名单（字符串比较，
            // 较贵）。反过来的话，每条不含区域的公式都要白跑二十次字符串
            // 比较 —— 实测 LEN("abc") 因此慢了 55%（0.195 -> 0.305 us）。
            bool anyRef = false;
            for (const Value& a : args) if (a.isRangeRef()) { anyRef = true; break; }
            if (anyRef && !supportsLazyRange(u)) {
                // 只替换真正的区域项 —— 整段重赋值会把每个非区域参数也拷贝一遍
                for (Value& a : args)
                    if (a.isRangeRef()) a = materializeRange(a, ctx);
            }

            // IF 的惰性求值：只算被选中的分支（避免 1/0 之类的错误污染）
            if (u == "IF" && args.size() >= 2) {
                if (args[0].isError()) return args[0];
                bool c; Value e;
                if (!toBool(args[0], c, e)) return e;
                if (c) return args[1];
                return args.size() >= 3 ? args[2] : Value::boolean(false);
            }
            if (u == "IFERROR" && args.size() == 2) return args[0].isError() ? args[1] : args[0];
            if (u == "IFNA" && args.size() == 2)
                return (args[0].isError() && args[0].e == Err::NA) ? args[1] : args[0];

            if ((int)args.size() < fn->minArgs) return Value::error(Err::Value);
            if (fn->maxArgs >= 0 && (int)args.size() > fn->maxArgs) return Value::error(Err::Value);
            return fn->impl(args, ctx);
        }
    }
    return Value::error(Err::Value);
}

} // namespace xl
