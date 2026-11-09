#include <algorithm>
#include <cstring>
#include <fstream>

#include "splash.h"

const char* formatName(Format f) {
    switch (f) {
        case Format::Qualcomm: return "Qualcomm (SPLASH!!)";
        case Format::QualcommPadded: return "Qualcomm padded (SPLASH!! records after 0x1000 padding, Lenovo etc.)";
        default: return "OPlus (SPLASH LOGO!)";
    }
}

bool detectFormat(const fs::path& file, Format& fmt) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    Bytes head(0x10000, 0);
    f.read(reinterpret_cast<char*>(head.data()), (std::streamsize)head.size());
    size_t got = (size_t)f.gcount();
    if (got >= 8 && std::memcmp(head.data(), "SPLASH!!", 8) == 0) {
        fmt = Format::Qualcomm;
        return true;
    }
    if (got >= 0x4000 + 12 && std::memcmp(head.data() + 0x4000, "SPLASH LOGO!", 12) == 0) {
        fmt = Format::OPlus;
        return true;
    }
    // zero padding followed by a 512-aligned "SPLASH!!" header
    for (size_t p = 512; p + 8 <= got; p += 512) {
        if (std::memcmp(head.data() + p, "SPLASH!!", 8) == 0) {
            fmt = Format::QualcommPadded;
            return true;
        }
        bool zero = true;
        for (size_t i = p - 512; i < p && zero; i++) zero = head[i] == 0;
        if (!zero) break;
    }
    return false;
}

std::vector<fs::path> listImageFiles(const fs::path& dir) {
    static const char* exts[] = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff"};
    std::vector<fs::path> r;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        std::string ext = toLower(pathToUtf8(it->path().extension()));
        for (const char* x : exts)
            if (ext == x) {
                r.push_back(it->path());
                break;
            }
    }
    std::sort(r.begin(), r.end(), [](const fs::path& a, const fs::path& b) {
        return naturalLess(pathToUtf8(a.filename()), pathToUtf8(b.filename()));
    });
    return r;
}

bool unpackSplash(Format fmt, const fs::path& img, const fs::path& outDir, const Callbacks& cb,
                  std::vector<fs::path>& extracted) {
    extracted.clear();
    cb.info(std::string("Operation: unpack - ") + formatName(fmt));
    switch (fmt) {
        case Format::Qualcomm: return unpackQualcomm(img, outDir, cb, extracted);
        case Format::QualcommPadded: return unpackQualcommPadded(img, outDir, cb, extracted);
        default: return unpackOPlus(img, outDir, cb, extracted);
    }
}

bool repackSplash(Format fmt, const fs::path& dir, const fs::path& outImg, const RepackOptions& opt,
                  const Callbacks& cb) {
    cb.info(std::string("Operation: repack - ") + formatName(fmt));
    switch (fmt) {
        case Format::Qualcomm: return repackQualcomm(dir, outImg, opt.qualcommRle, cb);
        case Format::QualcommPadded: return repackQualcommPadded(dir, outImg, opt.qualcommRle, cb);
        default: return repackOPlus(dir, outImg, cb);
    }
}
