// Magic Splash Wand - C++ GUI port
//
// Port of "Magic Splash Wand" (PHP) by NekoYuzu (MlgmXyysd) / MeowCat Studio /
// Meow Mobile - https://github.com/MlgmXyysd/Magic-Splash-Wand
//
// Tool for unpacking and packaging the splash image of Qualcomm devices.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "util.h"

enum class LogLevel { Info, Warn, Error, Success };

struct Callbacks {
    std::function<void(LogLevel, const std::string&)> log;  // UTF-8 message
    std::function<void(int, int)> progress;                 // (done, total)

    void info(const std::string& m) const { if (log) log(LogLevel::Info, m); }
    void warn(const std::string& m) const { if (log) log(LogLevel::Warn, m); }
    void error(const std::string& m) const { if (log) log(LogLevel::Error, m); }
    void ok(const std::string& m) const { if (log) log(LogLevel::Success, m); }
    void step(int done, int total) const { if (progress) progress(done, total); }
};

enum class Format {
    Qualcomm,        // "SPLASH!!"      - one 512 byte header listing all images, raw / RLE24
    QualcommPadded,  // records of [0x1000 zeros][512 byte "SPLASH!!" header][data] (Lenovo etc.)
    OPlus            // "SPLASH LOGO!"  - OPPO / OnePlus / Realme, BMP + optional GZIP
};

struct RepackOptions {
    bool qualcommRle = true;  // Qualcomm: RLE24 compression (false = raw BGR)
};

const char* formatName(Format f);

// Looks at the magic bytes of a splash image.
bool detectFormat(const fs::path& file, Format& fmt);

// Image files (png/jpg/bmp/gif/tif) in a folder, natural sort order.
std::vector<fs::path> listImageFiles(const fs::path& dir);

bool unpackSplash(Format fmt, const fs::path& img, const fs::path& outDir, const Callbacks& cb,
                  std::vector<fs::path>& extracted);
bool repackSplash(Format fmt, const fs::path& dir, const fs::path& outImg, const RepackOptions& opt,
                  const Callbacks& cb);

// Per-format implementations.
bool unpackQualcomm(const fs::path& img, const fs::path& outDir, const Callbacks& cb,
                    std::vector<fs::path>& extracted);
bool repackQualcomm(const fs::path& dir, const fs::path& outImg, bool rle, const Callbacks& cb);
bool unpackQualcommPadded(const fs::path& img, const fs::path& outDir, const Callbacks& cb,
                          std::vector<fs::path>& extracted);
bool repackQualcommPadded(const fs::path& dir, const fs::path& outImg, bool rle, const Callbacks& cb);
bool unpackOPlus(const fs::path& img, const fs::path& outDir, const Callbacks& cb,
                 std::vector<fs::path>& extracted);
bool repackOPlus(const fs::path& dir, const fs::path& outImg, const Callbacks& cb);
