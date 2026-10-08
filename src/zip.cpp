#include "zip.hpp"
#include <zlib.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <ctime>
#include <cstdint>

namespace xl {

// ---------------------------------------------------------------------------
// CRC32
// ---------------------------------------------------------------------------
static uint32_t crcTab[256];
static bool crcInit() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTab[n] = c;
    }
    return true;
}
static bool g_crcReady = crcInit();

uint32_t zipCrc32(const uint8_t* data, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = crcTab[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)(x >> 8));
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)((x >> 8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF)); v.push_back((uint8_t)(x >> 24));
}
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ---------------------------------------------------------------------------
// raw deflate / raw inflate
//
// ZIP 的 method 8 要求"裸 deflate"流：不带 zlib 头（2 字节）也不带 Adler-32 尾。
// compress2/uncompress 产出的却是完整 zlib 流，直接写进去会得到"自己能读、
// 标准库拒绝"的文件 —— PNG 要 zlib 流，ZIP 不要，两者不能混用。
// ---------------------------------------------------------------------------
static bool rawDeflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, int level) {
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    uLong bound = deflateBound(&strm, (uLong)in.size());
    out.resize(bound);
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in.data()));
    strm.avail_in = (uInt)in.size();
    strm.next_out = reinterpret_cast<Bytef*>(out.data());
    strm.avail_out = (uInt)out.size();
    int r = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
    if (r != Z_STREAM_END) return false;
    out.resize(strm.total_out);
    return true;
}

// 单个条目的解压上限。
//
// 解压是"按需增长"的循环：只要 inflate 一直返回 Z_OK，就一直扩容。
// 这本身正确，但没有任何上限 —— 于是压缩包炸弹（几百 KB 的压缩包解压出
// 几 GB 数据）能把内存吃干。实测一个 204KB 的文件解压出 200MB 且"成功"返回，
// 用户只是打开了一个文件，进程就占用了 200MB。
//
// 真正的 xlsx 部件远小于此（十万格的工作表 XML 也就几十 MB），
// 256MB 留了极大余量；超限则判定为异常输入并失败，而不是让进程被 OOM 杀掉。
static const size_t kMaxInflateBytes = 256u * 1024u * 1024u;

static bool rawInflate(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;
    out.clear();
    std::vector<uint8_t> buf(4096);
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    strm.avail_in = (uInt)srcLen;
    int r;
    do {
        if (strm.avail_out == 0) {
            size_t off = out.size();
            if (off >= kMaxInflateBytes) {
                inflateEnd(&strm);
                out.clear();
                return false;                  // 超过上限，按异常输入处理
            }
            out.resize(off + buf.size());
            strm.next_out = reinterpret_cast<Bytef*>(out.data() + off);
            strm.avail_out = (uInt)buf.size();
        }
        r = inflate(&strm, Z_NO_FLUSH);
        if (r == Z_BUF_ERROR && strm.avail_out > 0) break;
    } while (r == Z_OK || (r == Z_BUF_ERROR && strm.avail_out == 0));
    size_t produced = strm.total_out;
    inflateEnd(&strm);
    if (produced > kMaxInflateBytes) { out.clear(); return false; }
    out.resize(produced);
    return r == Z_STREAM_END;
}

// DOS 日期时间
static uint16_t dosTime() {
    time_t t = time(nullptr);
    // 必须初始化：localtime/localtime_r 失败时不会写 tmBuf，
    // 直接用未初始化的字段是 UB（GCC 会报 -Wmaybe-uninitialized）。
    struct tm tmBuf{};
#ifdef _WIN32
    // 不用 localtime_s：它在 MSVC 与 C11 标准里参数顺序相反，
    // MinGW 在严格模式下还可能隐藏它。C89 的 localtime 两边都有。
    if (struct tm* p = localtime(&t)) tmBuf = *p;
#else
    localtime_r(&t, &tmBuf);
#endif
    int year = tmBuf.tm_year + 1900;
    if (year < 1980) year = 1980;
    return (uint16_t)(((tmBuf.tm_hour) << 11) | ((tmBuf.tm_min) << 5) | ((tmBuf.tm_sec / 2)));
}
static uint16_t dosDate() {
    time_t t = time(nullptr);
    struct tm tmBuf{};
#ifdef _WIN32
    // 不用 localtime_s：它在 MSVC 与 C11 标准里参数顺序相反，
    // MinGW 在严格模式下还可能隐藏它。C89 的 localtime 两边都有。
    if (struct tm* p = localtime(&t)) tmBuf = *p;
#else
    localtime_r(&t, &tmBuf);
#endif
    int year = tmBuf.tm_year + 1900;
    if (year < 1980) year = 1980;
    return (uint16_t)(((year - 1980) << 9) | ((tmBuf.tm_mon + 1) << 5) | tmBuf.tm_mday);
}

// ---------------------------------------------------------------------------
// ZipWriter
// ---------------------------------------------------------------------------
void ZipWriter::addFile(const std::string& name, const std::vector<uint8_t>& data, int level) {
    Out o;
    o.name = name;
    o.usize = (uint32_t)data.size();
    o.crc = zipCrc32(data.data(), data.size());
    if (level <= 0 || data.empty()) {
        o.method = 0;
        o.raw = data;
    } else {
        std::vector<uint8_t> comp;
        if (rawDeflate(data, comp, level) && comp.size() < data.size()) {
            // 压缩后更大时退回存储（小文件常见），Excel 也这么做
            o.method = 8;
            o.raw = std::move(comp);
        } else {
            o.method = 0;
            o.raw = data;
        }
    }
    items_.push_back(std::move(o));
}

void ZipWriter::addFileStr(const std::string& name, const std::string& text, int level) {
    std::vector<uint8_t> d(text.begin(), text.end());
    addFile(name, d, level);
}

bool ZipWriter::writeToMemory(std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    std::vector<uint8_t> central;
    uint16_t tm = dosTime(), dt = dosDate();

    for (auto& it : items_) {
        it.offset = (uint32_t)out.size();
        // Local file header
        put32(out, 0x04034b50u);
        put16(out, 20);              // version needed
        put16(out, 0);               // flags
        put16(out, it.method);
        put16(out, tm);
        put16(out, dt);
        put32(out, it.crc);
        put32(out, (uint32_t)it.raw.size());
        put32(out, it.usize);
        put16(out, (uint16_t)it.name.size());
        put16(out, 0);               // extra len
        out.insert(out.end(), it.name.begin(), it.name.end());
        out.insert(out.end(), it.raw.begin(), it.raw.end());

        // Central directory entry
        put32(central, 0x02014b50u);
        put16(central, 20);          // version made by
        put16(central, 20);          // version needed
        put16(central, 0);
        put16(central, it.method);
        put16(central, tm);
        put16(central, dt);
        put32(central, it.crc);
        put32(central, (uint32_t)it.raw.size());
        put32(central, it.usize);
        put16(central, (uint16_t)it.name.size());
        put16(central, 0);           // extra
        put16(central, 0);           // comment
        put16(central, 0);           // disk start
        put16(central, 0);           // internal attrs
        put32(central, 0);           // external attrs
        put32(central, it.offset);
        central.insert(central.end(), it.name.begin(), it.name.end());
    }

    uint32_t cdOffset = (uint32_t)out.size();
    out.insert(out.end(), central.begin(), central.end());

    // EOCD
    put32(out, 0x06054b50u);
    put16(out, 0);                   // disk number
    put16(out, 0);                   // disk with cd
    put16(out, (uint16_t)items_.size());
    put16(out, (uint16_t)items_.size());
    put32(out, (uint32_t)central.size());
    put32(out, cdOffset);
    put16(out, 0);                   // comment len

    err.clear();
    return true;
}

bool ZipWriter::writeTo(const std::string& path, std::string& err) {
    std::vector<uint8_t> buf;
    if (!writeToMemory(buf, err)) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) { err = "无法打开文件写入: " + path; return false; }
    f.write((const char*)buf.data(), (std::streamsize)buf.size());
    if (!f) { err = "写入失败: " + path; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// ZipReader
// ---------------------------------------------------------------------------
bool ZipReader::open(const std::string& path, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "无法打开文件: " + path; return false; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    return parse(buf, err);
}

bool ZipReader::openMemory(const std::vector<uint8_t>& buf, std::string& err) {
    return parse(buf, err);
}

// 从尾部往前找 EOCD 签名（注释长度可变，不能只查文件末尾 22 字节）
static const uint8_t* findEOCD(const std::vector<uint8_t>& b, size_t& pos) {
    if (b.size() < 22) return nullptr;
    size_t limit = b.size() >= 65557 + 22 ? b.size() - (65557 + 22) : 0;
    for (size_t i = b.size() - 22;; --i) {
        if (i + 3 < b.size() && b[i] == 0x50 && b[i+1] == 0x4B && b[i+2] == 0x05 && b[i+3] == 0x06) {
            pos = i;
            return b.data() + i;
        }
        if (i == limit) break;
    }
    return nullptr;
}

bool ZipReader::parse(const std::vector<uint8_t>& b, std::string& err) {
    entries_.clear();
    size_t eocd = 0;
    const uint8_t* e = findEOCD(b, eocd);
    if (!e) { err = "不是有效的 ZIP 文件（未找到 EOCD）"; return false; }

    uint16_t total = get16(e + 10);
    uint32_t cdSize = get32(e + 12);
    uint32_t cdOff = get32(e + 16);

    if (cdOff + cdSize > b.size()) {
        // 某些工具给出的偏移含前导数据，退化为流式解析中央目录
        cdOff = 0;
    }

    size_t p = cdOff;
    for (uint16_t i = 0; i < total; i++) {
        if (p + 46 > b.size()) { err = "中央目录条目越界"; return false; }
        if (!(b[p] == 0x50 && b[p+1] == 0x4B && b[p+2] == 0x01 && b[p+3] == 0x02)) {
            // 流式扫描下一个中央目录签名
            bool found = false;
            for (size_t q = p + 4; q + 4 <= b.size(); q++) {
                if (b[q] == 0x50 && b[q+1] == 0x4B && b[q+2] == 0x01 && b[q+3] == 0x02) {
                    p = q; found = true; break;
                }
            }
            if (!found) break;
        }
        uint16_t method = get16(b.data() + p + 10);
        uint32_t crc = get32(b.data() + p + 16);
        uint32_t csize = get32(b.data() + p + 20);
        uint32_t usize = get32(b.data() + p + 24);
        (void)usize;
        uint16_t fnlen = get16(b.data() + p + 28);
        uint16_t extralen = get16(b.data() + p + 30);
        uint16_t cmtlen = get16(b.data() + p + 32);
        uint32_t lho = get32(b.data() + p + 42);
        std::string name((const char*)b.data() + p + 46, fnlen);
        p += 46 + fnlen + extralen + cmtlen;

        // 定位 local header
        if (lho + 30 > b.size()) { err = "local header 偏移越界: " + name; return false; }
        if (!(b[lho] == 0x50 && b[lho+1] == 0x4B && b[lho+2] == 0x03 && b[lho+3] == 0x04)) {
            err = "local header 签名错误: " + name;
            return false;
        }
        uint16_t lfnlen = get16(b.data() + lho + 26);
        uint16_t lextra = get16(b.data() + lho + 28);
        size_t dataOff = lho + 30 + lfnlen + lextra;
        if (dataOff + csize > b.size()) { err = "数据区越界: " + name; return false; }

        ZipEntry ent;
        ent.name = name;
        ent.method = method;
        ent.crc = crc;
        if (method == 0) {
            ent.data.assign(b.begin() + dataOff, b.begin() + dataOff + csize);
        } else if (method == 8) {
            if (!rawInflate(b.data() + dataOff, csize, ent.data)) {
                err = "解压失败（不是有效的 raw deflate 流）: " + name;
                return false;
            }
        } else {
            err = "不支持的压缩方法: " + name;
            return false;
        }
        entries_.push_back(std::move(ent));
    }
    err.clear();
    return true;
}

const ZipEntry* ZipReader::find(const std::string& name) const {
    for (auto& e : entries_) if (e.name == name) return &e;
    return nullptr;
}

bool ZipReader::has(const std::string& name) const { return find(name) != nullptr; }

bool ZipReader::readText(const std::string& name, std::string& out) const {
    const ZipEntry* e = find(name);
    if (!e) return false;
    out.assign(e->data.begin(), e->data.end());
    return true;
}

} // namespace xl
