// Magic Splash Wand - C++ GUI port
// Shared helpers: little-endian IO, file IO, gzip, base64, path/name helpers.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

// ---- little endian -------------------------------------------------------

// Out-of-range reads return 0 (the original PHP tool zero-pads in the same way).
inline uint32_t rdLE32(const Bytes& b, size_t off) {
    if (off + 4 > b.size()) return 0;
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
           ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

inline void wrLE32(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF));
    b.push_back((uint8_t)((v >> 24) & 0xFF));
}

// ---- files / paths -------------------------------------------------------

bool readFile(const fs::path& p, Bytes& out);
// Creates the parent directory when needed.
bool writeFile(const fs::path& p, const Bytes& data);

std::string pathToUtf8(const fs::path& p);
fs::path utf8ToPath(const std::string& s);

std::string toLower(std::string s);
// Case-insensitive "natural" order: 2.png < 10.png
bool naturalLess(const std::string& a, const std::string& b);
// Makes a name that is safe to use as a file name (no path separators, no
// reserved characters). Protects against malicious splash images.
std::string sanitizeName(const std::string& name);
// Fixed-size char field -> string (cut at first NUL, trimmed).
std::string trimField(const uint8_t* p, size_t n);

// ---- gzip / base64 -------------------------------------------------------

bool gzipCompress(const uint8_t* data, size_t n, Bytes& out, int level = 9);
bool gunzip(const uint8_t* data, size_t n, Bytes& out, size_t maxOut = (size_t)256 << 20);

std::string base64Encode(const uint8_t* data, size_t n);
bool base64Decode(const std::string& s, Bytes& out);
