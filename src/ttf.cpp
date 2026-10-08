#include "ttf.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace xl {

namespace {

inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline int16_t  be16s(const uint8_t* p) { return (int16_t)((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline int32_t  be32s(const uint8_t* p) { return (int32_t)be32(p); }

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)(x & 0xFF));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)((x >> 16) & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF)); v.push_back((uint8_t)(x & 0xFF));
}
uint32_t tableChecksum(const uint8_t* p, uint32_t len) {
    uint32_t sum = 0;
    uint32_t n = (len + 3) / 4;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = 0;
        for (uint32_t k = 0; k < 4; k++) {
            uint32_t byte = (i * 4 + k < len) ? p[i * 4 + k] : 0;
            v = (v << 8) | byte;
        }
        sum += v;
    }
    return sum;
}

} // namespace

std::vector<uint32_t> TtfFont::decodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int n; uint32_t cp;
        if      (c < 0x80)             { n = 0; cp = c; }
        else if ((c & 0xE0) == 0xC0)   { n = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0)   { n = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0)   { n = 3; cp = c & 0x07u; }
        else { out.push_back(c); i++; continue; }
        if (i + (size_t)n >= s.size()) { out.push_back(c); i++; continue; }
        bool ok = true;
        for (int k = 1; k <= n; k++)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) { ok = false; break; }
        if (!ok) { out.push_back(c); i++; continue; }
        for (int k = 1; k <= n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3Fu);
        i += (size_t)n + 1;
        out.push_back(cp);
    }
    return out;
}

const uint8_t* TtfFont::table(const std::string& tag, uint32_t& len) const {
    auto it = tables_.find(tag);
    if (it == tables_.end()) { len = 0; return nullptr; }
    if (it->second.first + it->second.second > raw_.size()) { len = 0; return nullptr; }
    len = it->second.second;
    return raw_.data() + it->second.first;
}

bool TtfFont::load(const std::string& path, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "无法打开字体文件: " + path; return false; }
    raw_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (raw_.size() < 12) { err = "文件太小，不是有效字体"; return false; }

    uint32_t sfnt = be32(raw_.data());
    // 0x00010000 = TrueType；'OTTO' = CFF（这里不支持，PDF 需要 Type2 的 glyf）
    if (sfnt == 0x4F54544F) { err = "OTF/CFF 字体不支持（需要含 glyf 表的 TrueType）"; return false; }
    if (sfnt != 0x00010000 && sfnt != 0x74727565) {
        err = "不是 TrueType 字体（sfnt 版本 0x" + std::to_string(sfnt) + "）";
        return false;
    }

    uint16_t numTables = be16(raw_.data() + 4);
    if (12 + (size_t)numTables * 16 > raw_.size()) { err = "表目录越界"; return false; }
    for (uint16_t i = 0; i < numTables; i++) {
        const uint8_t* p = raw_.data() + 12 + i * 16;
        std::string tag((char*)p, 4);
        uint32_t off = be32(p + 8), len = be32(p + 12);
        if (off + len > raw_.size()) len = (uint32_t)(raw_.size() - std::min<size_t>(off, raw_.size()));
        tables_[tag] = {off, len};
    }

    // ---- head ----
    uint32_t hl; const uint8_t* head = table("head", hl);
    if (!head || hl < 54) { err = "缺少 head 表"; return false; }
    m_.unitsPerEm = be16(head + 18);
    m_.indexToLocFormat = be16s(head + 50);
    m_.bboxXMin = be16s(head + 36); m_.bboxYMin = be16s(head + 38);
    m_.bboxXMax = be16s(head + 40); m_.bboxYMax = be16s(head + 42);
    if (m_.unitsPerEm <= 0) m_.unitsPerEm = 1000;

    // ---- maxp ----
    uint32_t ml; const uint8_t* maxp = table("maxp", ml);
    if (!maxp || ml < 6) { err = "缺少 maxp 表"; return false; }
    m_.numGlyphs = be16(maxp + 4);

    // ---- hhea / hmtx ----
    uint32_t hhl; const uint8_t* hhea = table("hhea", hhl);
    if (!hhea || hhl < 36) { err = "缺少 hhea 表"; return false; }
    m_.numberOfHMetrics = be16(hhea + 34);
    m_.ascent = be16s(hhea + 4);
    m_.descent = be16s(hhea + 6);
    if (m_.numberOfHMetrics <= 0 || m_.numberOfHMetrics > m_.numGlyphs)
        m_.numberOfHMetrics = m_.numGlyphs;

    uint32_t hml; const uint8_t* hmtx = table("hmtx", hml);
    advW_.assign((size_t)m_.numGlyphs, 0);
    if (hmtx) {
        for (int i = 0; i < m_.numGlyphs; i++) {
            size_t p = (size_t)std::min(i, m_.numberOfHMetrics - 1) * 4;
            if (p + 2 <= hml) advW_[i] = be16(hmtx + p);
            else if (i > 0) advW_[i] = advW_[i - 1];
        }
    }

    // ---- loca ----
    uint32_t ll; const uint8_t* loca = table("loca", ll);
    if (!loca) { err = "缺少 loca 表"; return false; }
    loca_.assign((size_t)m_.numGlyphs + 1, 0);
    if (m_.indexToLocFormat == 0) {
        for (int i = 0; i <= m_.numGlyphs && (size_t)i * 2 + 1 < ll; i++)
            loca_[i] = (uint32_t)be16(loca + i * 2) * 2;
    } else {
        for (int i = 0; i <= m_.numGlyphs && (size_t)i * 4 + 3 < ll; i++)
            loca_[i] = be32(loca + i * 4);
    }

    glyfOff_ = 0; glyfLen_ = 0;
    auto gt = tables_.find("glyf");
    if (gt != tables_.end()) { glyfOff_ = gt->second.first; glyfLen_ = gt->second.second; }
    if (!glyfLen_) { err = "缺少 glyf 表"; return false; }

    // ---- cmap：优先 Unicode 全量（format 12），退化到 BMP（format 4）----
    uint32_t cl; const uint8_t* cmap = table("cmap", cl);
    if (!cmap || cl < 4) { err = "缺少 cmap 表"; return false; }
    uint16_t nSub = be16(cmap + 2);
    uint32_t bestOff = 0; int bestFmt = -1;
    for (uint16_t i = 0; i < nSub && (size_t)4 + i * 8 + 7 < cl; i++) {
        const uint8_t* p = cmap + 4 + i * 8;
        uint16_t pid = be16(p), eid = be16(p + 2);
        uint32_t off = be32(p + 4);
        if (off + 4 > cl) continue;
        uint16_t fmt = be16(cmap + off);
        bool unicode = (pid == 3 && (eid == 1 || eid == 10)) || (pid == 0);
        if (!unicode) continue;
        if (fmt == 12) { bestOff = off; bestFmt = 12; break; }   // format 12 覆盖全 Unicode
        if (fmt == 4 && bestFmt != 12) { bestOff = off; bestFmt = 4; }
    }
    if (bestFmt < 0) { err = "cmap 中没有可用的 Unicode 子表"; return false; }

    const uint8_t* st = cmap + bestOff;
    if (bestFmt == 4) {
        uint16_t segX2 = be16(st + 6);
        const uint8_t* endC = st + 14;
        const uint8_t* startC = endC + segX2 + 2;
        const uint8_t* delta = startC + segX2;
        const uint8_t* rangeOff = delta + segX2;
        for (uint16_t s = 0; s < segX2 / 2; s++) {
            uint16_t end = be16(endC + s * 2);
            uint16_t start = be16(startC + s * 2);
            int16_t d = be16s(delta + s * 2);
            uint16_t ro = be16(rangeOff + s * 2);
            if (start == 0xFFFF) continue;
            for (uint32_t c = start; c <= end; c++) {
                uint16_t g = 0;
                if (ro == 0) g = (uint16_t)((c + d) & 0xFFFF);
                else {
                    size_t gi = (size_t)s * 2 + ro + (c - start) * 2;
                    if (gi + 1 < cl) { g = be16(rangeOff + gi); if (g) g = (uint16_t)((g + d) & 0xFFFF); }
                }
                if (g && g < m_.numGlyphs) cmap_[c] = g;
            }
        }
    } else {
        uint32_t nGroups = be32(st + 12);
        for (uint32_t i = 0; i < nGroups; i++) {
            size_t p = 16 + i * 12;
            if (p + 11 >= cl) break;
            uint32_t sc = be32(st + p), ec = be32(st + p + 4), sg = be32(st + p + 8);
            if (ec < sc || ec - sc > 0x10FFFF) continue;
            // 超大的组（如整段 BMP）逐个存会爆内存，这里按需惰性存：
            // 记录区间，glyphFor 时再算。
            for (uint32_t c = sc; c <= ec && c <= sc + 65535; c++) {
                uint32_t g = sg + (c - sc);
                if (g && g < (uint32_t)m_.numGlyphs) cmap_[c] = g;
            }
            // 超出 65535 的部分按惰性处理（实际中文用到的都在 BMP/CJK Ext 内）
            if (ec > sc + 65535) {
                for (uint32_t c = sc + 65536; c <= ec; c++) {
                    uint32_t g = sg + (c - sc);
                    if (g && g < (uint32_t)m_.numGlyphs) cmap_[c] = g;
                }
            }
        }
    }
    if (cmap_.empty()) { err = "cmap 解析结果为空"; return false; }
    return true;
}

uint16_t TtfFont::glyphFor(uint32_t cp) const {
    auto it = cmap_.find(cp);
    if (it == cmap_.end()) return 0;
    return (uint16_t)it->second;
}

double TtfFont::advanceOf(uint16_t gid) const {
    if ((size_t)gid >= advW_.size()) return 0;
    return (double)advW_[gid] / (double)m_.unitsPerEm;
}
double TtfFont::textWidthEm(const std::string& utf8) const {
    double w = 0;
    for (uint32_t cp : decodeUtf8(utf8)) w += advanceOf(glyphFor(cp));
    return w;
}

// ---------------------------------------------------------------------------
// 复合字形的依赖收集
// ---------------------------------------------------------------------------
void TtfFont::collectDeps(uint16_t gid, std::vector<bool>& keep, int depth) const {
    if (gid >= m_.numGlyphs || depth > 8) return;
    if (gid >= (int)keep.size()) return;
    if (keep[gid]) return;
    keep[gid] = true;
    if ((size_t)(gid + 1) >= loca_.size()) return;
    uint32_t start = loca_[gid], end = loca_[gid + 1];
    if (end <= start || start >= glyfLen_) return;      // 空字形（如空格）
    const uint8_t* g = raw_.data() + glyfOff_ + start;
    int16_t nc = be16s(g);
    if (nc >= 0) return;                                 // 简单字形，无依赖
    size_t p = 10;
    uint16_t flags = 0;
    do {
        if (p + 4 > (size_t)(end - start)) break;
        flags = be16(g + p);
        uint16_t comp = be16(g + p + 2);
        p += 4;
        if (flags & 0x0001) p += 4; else p += 2;         // ARG_1_AND_2_ARE_WORDS
        if (flags & 0x0008) p += 2;                      // WE_HAVE_A_SCALE
        else if (flags & 0x0040) p += 4;                 // WE_HAVE_AN_X_AND_Y_SCALE
        else if (flags & 0x0080) p += 8;                 // WE_HAVE_A_TWO_BY_TWO
        collectDeps(comp, keep, depth + 1);
    } while (flags & 0x0020);                            // MORE_COMPONENTS
}

bool TtfFont::subset(const std::vector<uint32_t>& cps,
                     std::vector<uint8_t>& outFont,
                     std::map<uint32_t, uint16_t>& cids,
                     std::string& err) const {
    if (!loaded()) { err = "字体未加载"; return false; }

    // 1) 码点 -> 原字形 ID
    std::vector<uint16_t> roots;
    std::map<uint16_t, std::vector<uint32_t>> gidToCps;
    for (uint32_t cp : cps) {
        uint16_t g = glyphFor(cp);
        if (!g) continue;                                // 字体里没有该字形
        if (!gidToCps.count(g)) roots.push_back(g);
        gidToCps[g].push_back(cp);
    }
    if (roots.empty()) { err = "没有任何可用字形"; return false; }

    // 2) 传递闭包：把复合字形引用的组件一起带上
    std::vector<bool> keep((size_t)m_.numGlyphs, false);
    keep[0] = true;                                      // .notdef 必须在 0 号位
    for (uint16_t g : roots)
        collectDeps(g, keep);

    // 3) 原 GID -> 子集 GID
    std::vector<uint16_t> newId((size_t)m_.numGlyphs, 0);
    std::vector<uint16_t> order;
    for (uint16_t g = 0; g < m_.numGlyphs; g++)
        if (keep[g]) { newId[g] = (uint16_t)order.size(); order.push_back(g); }
    if (order.empty()) { err = "子集为空"; return false; }
    uint16_t nG = (uint16_t)order.size();

    // 4) 重建 glyf：复制原始数据，并重写复合字形里的组件编号
    std::vector<std::vector<uint8_t>> newGlyf(nG);
    for (uint16_t i = 0; i < nG; i++) {
        uint16_t og = order[i];
        if ((size_t)(og + 1) >= loca_.size()) continue;
        uint32_t start = loca_[og], end = loca_[og + 1];
        if (end <= start || start >= glyfLen_) continue;
        const uint8_t* g = raw_.data() + glyfOff_ + start;
        size_t len = std::min<size_t>(end - start, glyfLen_ - start);
        std::vector<uint8_t> d(g, g + len);
        if (d.size() >= 10) {
            int16_t nc = be16s(d.data());
            if (nc < 0) {                                // 复合字形：重写组件 GID
                size_t p = 10;
                uint16_t flags = 0;
                do {
                    if (p + 4 > d.size()) break;
                    flags = be16(d.data() + p);
                    uint16_t comp = be16(d.data() + p + 2);
                    uint16_t nc2 = (comp < m_.numGlyphs && keep[comp]) ? newId[comp] : 0;
                    d[p + 2] = (uint8_t)(nc2 >> 8);
                    d[p + 3] = (uint8_t)(nc2 & 0xFF);
                    p += 4;
                    if (flags & 0x0001) p += 4; else p += 2;
                    if (flags & 0x0008) p += 2;
                    else if (flags & 0x0040) p += 4;
                    else if (flags & 0x0080) p += 8;
                } while (flags & 0x0020);
            }
        }
        // 每个字形按 4 字节对齐（loca 要求）
        while (d.size() % 4) d.push_back(0);
        newGlyf[i] = std::move(d);
    }

    // 5) loca：根据总大小选 short / long
    uint32_t total = 0;
    std::vector<uint32_t> offsets(nG + 1, 0);
    for (uint16_t i = 0; i < nG; i++) { offsets[i] = total; total += (uint32_t)newGlyf[i].size(); }
    offsets[nG] = total;
    bool useLong = (total > 0x1FFFE);
    std::vector<uint8_t> locaData;
    for (uint16_t i = 0; i <= nG; i++) {
        if (useLong) put32(locaData, offsets[i]);
        else put16(locaData, (uint16_t)(offsets[i] / 2));
    }
    std::vector<uint8_t> glyfData;
    for (uint16_t i = 0; i < nG; i++)
        glyfData.insert(glyfData.end(), newGlyf[i].begin(), newGlyf[i].end());

    // 6) hmtx
    std::vector<uint8_t> hmtxData;
    for (uint16_t i = 0; i < nG; i++) {
        uint16_t og = order[i];
        put16(hmtxData, advW_[og]);
        int16_t lsb = 0;
        size_t p = (size_t)std::min((int)og, m_.numberOfHMetrics - 1) * 4 + 2;
        uint32_t hml; const uint8_t* hmtx = table("hmtx", hml);
        if (hmtx && p + 1 < hml) lsb = be16s(hmtx + p);
        put16(hmtxData, (uint16_t)lsb);
    }

    // 7) cmap format 4（子集码点分散，按段分组）
    std::vector<std::pair<uint32_t, uint32_t>> segs;
    {
        std::vector<uint32_t> sorted;
        for (auto& kv : gidToCps)
            for (uint32_t cp : kv.second)
                if (cp <= 0xFFFF) sorted.push_back(cp);
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        for (uint32_t c : sorted) {
            if (!segs.empty() && segs.back().second + 1 == c &&
                segs.back().second - segs.back().first < 200) {
                segs.back().second = c;
            } else segs.push_back({c, c});
        }
        segs.push_back({0xFFFF, 0xFFFF});                // 终止段
    }
    std::vector<uint8_t> cmapData;
    {
        uint16_t segX2 = (uint16_t)(segs.size() * 2);
        std::vector<uint8_t> endC, startC, delta, rangeOff, glyphIds;
        for (auto& s : segs) {
            put16(endC, (uint16_t)s.second);
            put16(startC, (uint16_t)s.first);
            if (s.first == 0xFFFF) { put16(delta, 1); put16(rangeOff, 0); continue; }
            uint32_t n = s.second - s.first + 1;
            // 若段内 GID 连续，用 delta 编码更省；否则走 glyphIdArray
            bool contiguous = true;
            uint16_t g0 = newId[glyphFor(s.first)];
            for (uint32_t k = 0; k < n; k++)
                if (newId[glyphFor(s.first + k)] != g0 + k) { contiguous = false; break; }
            if (contiguous) {
                int16_t d = (int16_t)(g0 - (uint16_t)s.first);
                put16(delta, (uint16_t)d);
                put16(rangeOff, 0);
            } else {
                put16(delta, 0);
                put16(rangeOff, (uint16_t)(segX2 + glyphIds.size()));
                for (uint32_t k = 0; k < n; k++)
                    put16(glyphIds, newId[glyphFor(s.first + k)]);
            }
        }
        uint16_t subLen = (uint16_t)(16 + segX2 * 4 + glyphIds.size());
        put16(cmapData, 0);                              // version
        put16(cmapData, 1);                              // numTables
        put16(cmapData, 3); put16(cmapData, 1);          // platformID=3, encodingID=1
        put32(cmapData, 12);                             // offset
        put16(cmapData, 4);                              // format
        put16(cmapData, subLen);
        put16(cmapData, 0);                              // language
        put16(cmapData, segX2);
        put16(cmapData, 2); put16(cmapData, 0); put16(cmapData, 0);   // searchRange 等
        cmapData.insert(cmapData.end(), endC.begin(), endC.end());
        put16(cmapData, 0);                              // reservedPad
        cmapData.insert(cmapData.end(), startC.begin(), startC.end());
        cmapData.insert(cmapData.end(), delta.begin(), delta.end());
        cmapData.insert(cmapData.end(), rangeOff.begin(), rangeOff.end());
        cmapData.insert(cmapData.end(), glyphIds.begin(), glyphIds.end());
    }

    // 8) head（改 indexToLocFormat、清零 checksumAdjustment）
    uint32_t hl; const uint8_t* h0 = table("head", hl);
    std::vector<uint8_t> headData(h0, h0 + std::min<size_t>(hl, 54));
    while (headData.size() < 54) headData.push_back(0);
    headData[50] = 0; headData[51] = (uint8_t)(useLong ? 1 : 0);
    headData[8] = headData[9] = headData[10] = headData[11] = 0;   // checkSumAdjustment

    // 9) hhea / maxp
    uint32_t hhl; const uint8_t* hh = table("hhea", hhl);
    std::vector<uint8_t> hheaData(hh, hh + std::min<size_t>(hhl, 36));
    while (hheaData.size() < 36) hheaData.push_back(0);
    put16(hheaData, 0);                                  // hhea 尾部补 numberOfHMetrics
    hheaData[hheaData.size() - 2] = (uint8_t)(nG >> 8);
    hheaData[hheaData.size() - 1] = (uint8_t)(nG & 0xFF);

    uint32_t ml; const uint8_t* mp = table("maxp", ml);
    std::vector<uint8_t> maxpData(mp, mp + std::min<size_t>(ml, 32));
    while (maxpData.size() < 6) maxpData.push_back(0);
    maxpData[4] = (uint8_t)(nG >> 8); maxpData[5] = (uint8_t)(nG & 0xFF);

    // 10) 组装 sfnt
    struct Tab { std::string tag; std::vector<uint8_t> data; };
    std::vector<Tab> tabs = {
        {"head", headData}, {"hhea", hheaData}, {"maxp", maxpData},
        {"hmtx", hmtxData}, {"cmap", cmapData}, {"loca", locaData}, {"glyf", glyfData},
    };
    // 可选表：只搬"不依赖字形数量"且体积小的。
    // post 表绝对不能原样搬 —— 它的 format 2.0 里存着原字体每一个字形的名字索引，
    // 一个 29030 字形的中文字体光这张表就有 288 KB，原样复制会让"只嵌一个字"
    // 的子集也变成 291 KB。这里改写成 format 3.0（不含字形名，固定 32 字节）。
    std::vector<uint8_t> postData = {
        0x00,0x03,0x00,0x00,   // version 3.0
        0x00,0x00,0x00,0x00,   // italicAngle
        0x00,0x00,0x00,0x00,   // underlinePosition(16) + underlineThickness(16)
        0x00,0x00,0x00,0x00,   // isFixedPitch(4) + padding
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    };
    tabs.push_back({"post", postData});
    for (const char* t : {"name", "OS/2", "cvt ", "fpgm", "prep", "gasp"}) {
        uint32_t l; const uint8_t* p = table(t, l);
        // 超过 64 KB 的可选表（hinting 程序等）对 PDF 渲染没有价值，反而拖大体积
        if (p && l && l <= 64u * 1024) tabs.push_back({t, std::vector<uint8_t>(p, p + l)});
    }
    std::sort(tabs.begin(), tabs.end(),
              [](const Tab& a, const Tab& b) { return a.tag < b.tag; });

    uint16_t nT = (uint16_t)tabs.size();
    uint32_t searchRange = 16, entrySel = 0;
    while (searchRange * 2 <= (uint32_t)nT * 16) { searchRange *= 2; entrySel++; }
    uint32_t rangeShift = (uint32_t)nT * 16 - searchRange;

    outFont.clear();
    put32(outFont, 0x00010000);
    put16(outFont, nT);
    put16(outFont, (uint16_t)searchRange);
    put16(outFont, (uint16_t)entrySel);
    put16(outFont, (uint16_t)rangeShift);

    uint32_t off = 12 + (uint32_t)nT * 16;
    std::vector<std::pair<std::string, uint32_t>> dir;
    for (auto& t : tabs) { dir.push_back({t.tag, off}); off += (uint32_t)t.data.size(); }
    for (size_t i = 0; i < tabs.size(); i++) {
        const std::string& tag = tabs[i].tag;
        outFont.push_back((uint8_t)tag[0]); outFont.push_back((uint8_t)tag[1]);
        outFont.push_back((uint8_t)tag[2]); outFont.push_back((uint8_t)tag[3]);
        put32(outFont, tableChecksum(tabs[i].data.data(), (uint32_t)tabs[i].data.size()));
        put32(outFont, dir[i].second);
        put32(outFont, (uint32_t)tabs[i].data.size());
    }
    for (auto& t : tabs)
        outFont.insert(outFont.end(), t.data.begin(), t.data.end());

    // 回填 cids：码点 -> 子集字形序号（PDF 的 Identity-H 用的就是这个）
    cids.clear();
    for (auto& kv : gidToCps) {
        uint16_t ng = newId[kv.first];
        for (uint32_t cp : kv.second) cids[cp] = ng;
    }
    return true;
}

} // namespace xl
