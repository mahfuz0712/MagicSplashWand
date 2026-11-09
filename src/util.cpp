#include "util.h"

#include <zlib.h>

#include <cctype>
#include <fstream>
#include <set>

bool readFile(const fs::path& p, Bytes& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0, std::ios::beg);
    out.assign((size_t)n, 0);
    if (n > 0) {
        f.read(reinterpret_cast<char*>(out.data()), n);
        if (f.gcount() != n) return false;
    }
    return true;
}

bool writeFile(const fs::path& p, const Bytes& data) {
    std::error_code ec;
    fs::path parent = p.parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
    f.flush();
    return (bool)f;
}

std::string pathToUtf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path utf8ToPath(const std::string& s) { return fs::u8path(s); }

std::string toLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[j];
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t si = i, sj = j;
            while (si < a.size() && a[si] == '0') si++;
            while (sj < b.size() && b[sj] == '0') sj++;
            size_t ei = si, ej = sj;
            while (ei < a.size() && std::isdigit((unsigned char)a[ei])) ei++;
            while (ej < b.size() && std::isdigit((unsigned char)b[ej])) ej++;
            size_t li = ei - si, lj = ej - sj;
            if (li != lj) return li < lj;
            int c = a.compare(si, li, b, sj, lj);
            if (c != 0) return c < 0;
            i = ei;
            j = ej;
        } else {
            int la = std::tolower(ca), lb = std::tolower(cb);
            if (la != lb) return la < lb;
            i++;
            j++;
        }
    }
    return (a.size() - i) < (b.size() - j);
}

std::string sanitizeName(const std::string& name) {
    std::string r;
    for (unsigned char c : name) {
        if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
            c == '\\' || c == '|' || c == '?' || c == '*')
            r.push_back('_');
        else
            r.push_back((char)c);
    }
    while (!r.empty() && (r.back() == ' ' || r.back() == '.')) r.pop_back();
    size_t st = 0;
    while (st < r.size() && r[st] == ' ') st++;
    r.erase(0, st);
    if (r.empty()) r = "image";
    static const std::set<std::string> reserved = {
        "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
        "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    std::string stem = toLower(r.substr(0, r.find('.')));
    if (reserved.count(stem)) r.insert(r.begin(), '_');
    return r;
}

std::string trimField(const uint8_t* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len] != 0) len++;
    std::string s((const char*)p, len);
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// ---- gzip -----------------------------------------------------------------

bool gzipCompress(const uint8_t* data, size_t n, Bytes& out, int level) {
    z_stream s{};
    if (deflateInit2(&s, level, Z_DEFLATED, 15 + 16, 9, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    out.assign((size_t)deflateBound(&s, (uLong)n) + 64, 0);
    s.next_in = const_cast<Bytef*>(data);
    s.avail_in = (uInt)n;
    s.next_out = out.data();
    s.avail_out = (uInt)out.size();
    int r = deflate(&s, Z_FINISH);
    out.resize(s.total_out);
    deflateEnd(&s);
    return r == Z_STREAM_END;
}

bool gunzip(const uint8_t* data, size_t n, Bytes& out, size_t maxOut) {
    out.clear();
    z_stream s{};
    if (inflateInit2(&s, 15 + 32) != Z_OK) return false;  // gzip or zlib, auto-detected
    s.next_in = const_cast<Bytef*>(data);
    s.avail_in = (uInt)n;
    std::vector<uint8_t> buf(1 << 16);
    int r;
    do {
        s.next_out = buf.data();
        s.avail_out = (uInt)buf.size();
        r = inflate(&s, Z_NO_FLUSH);
        if (r != Z_OK && r != Z_STREAM_END) {
            inflateEnd(&s);
            return false;
        }
        out.insert(out.end(), buf.begin(), buf.end() - s.avail_out);
        if (out.size() > maxOut) {
            inflateEnd(&s);
            return false;
        }
        if (r == Z_OK && s.avail_in == 0 && s.avail_out != 0) {  // truncated input
            inflateEnd(&s);
            return false;
        }
    } while (r != Z_STREAM_END);
    inflateEnd(&s);
    return true;
}

// ---- base64 ---------------------------------------------------------------

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* d, size_t n) {
    std::string r;
    r.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        r += kB64[v >> 18];
        r += kB64[(v >> 12) & 63];
        r += kB64[(v >> 6) & 63];
        r += kB64[v & 63];
    }
    if (i + 1 == n) {
        uint32_t v = d[i] << 16;
        r += kB64[v >> 18];
        r += kB64[(v >> 12) & 63];
        r += "==";
    } else if (i + 2 == n) {
        uint32_t v = (d[i] << 16) | (d[i + 1] << 8);
        r += kB64[v >> 18];
        r += kB64[(v >> 12) & 63];
        r += kB64[(v >> 6) & 63];
        r += '=';
    }
    return r;
}

bool base64Decode(const std::string& s, Bytes& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || std::isspace(c)) continue;
        else return false;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((acc >> bits) & 0xFF));
        }
    }
    return true;
}
