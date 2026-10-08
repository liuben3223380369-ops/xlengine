#pragma once
// ---------------------------------------------------------------------------
// 最小 ZIP 读写（PKZIP / APPNOTE 6.3.x）。
//
// 为什么自己写：沙盒里没有 libzip / minizip，而 xlsx 本质就是一个 ZIP 包。
// 只实现需要的能力：
//   读：定位 EOCD -> 中央目录 -> local header -> raw inflate（method 8）或原样（method 0）
//   写：deflate 压缩 + 生成 local header / 中央目录 / EOCD
// 不支持：ZIP64、加密、分卷。写侧不使用数据描述符，总是回填 crc 与大小。
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace xl {

struct ZipEntry {
    std::string name;
    std::vector<uint8_t> data;      // 解压后的内容
    uint16_t method = 0;            // 0=store 8=deflate
    uint32_t crc = 0;
};

// --------------------------- 写 ---------------------------
class ZipWriter {
public:
    // level: 0 = 仅存储，9 = 最大压缩
    void addFile(const std::string& name, const std::vector<uint8_t>& data, int level = 6);
    void addFileStr(const std::string& name, const std::string& text, int level = 6);
    bool writeTo(const std::string& path, std::string& err);
    bool writeToMemory(std::vector<uint8_t>& out, std::string& err);

private:
    struct Out {
        std::string name;
        std::vector<uint8_t> raw;      // 已压缩（或原样）
        uint16_t method;
        uint32_t crc;
        uint32_t usize;
        uint32_t offset = 0;
    };
    std::vector<Out> items_;
};

// --------------------------- 读 ---------------------------
class ZipReader {
public:
    bool open(const std::string& path, std::string& err);
    bool openMemory(const std::vector<uint8_t>& buf, std::string& err);
    const std::vector<ZipEntry>& entries() const { return entries_; }

    // 按名字取内容；找不到返回 false。文本会被按 UTF-8 原样返回。
    bool readText(const std::string& name, std::string& out) const;
    bool has(const std::string& name) const;
    const ZipEntry* find(const std::string& name) const;

private:
    std::vector<ZipEntry> entries_;
    bool parse(const std::vector<uint8_t>& buf, std::string& err);
};

uint32_t zipCrc32(const uint8_t* data, size_t len);

} // namespace xl
