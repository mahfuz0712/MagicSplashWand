// Qualcomm splash formats ("SPLASH!!").
//
// (1) Classic multi-logo layout (see below), and
// (2) "padded" layout used e.g. by Lenovo tablets (Tab M10 HD ...): a sequence of records
//       [0x1000 zero bytes][512-byte header: "SPLASH!!", u32 width, height, type, blocks][data]
//     with one image per record (boot logo, fastboot screen, ...).
//
//   0x000  char[8]  magic "SPLASH!!"
//   0x008  u32      number of logos
//   0x00C  logo[n]  { u32 width, height, type(0=raw,1=RLE24), blocks(512B), offset }
//   0x200  payload  (offsets are absolute file offsets, data padded to 512 bytes)
//
// Pixels are stored B,G,R. RLE24: control byte, bit7 = "repeat one pixel",
// low 7 bits = count-1; then 1 pixel (repeat) or `count` pixels (literal).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "image.h"
#include "splash.h"

namespace {

constexpr size_t kBlock = 512;
constexpr size_t kMaxImages = (kBlock - 12) / 20;  // 25 entries fit in the header

// Returns false when the data ended before the image was complete.
bool decodeRle24(const uint8_t* d, size_t n, Image& im) {
    const size_t total = (size_t)im.w * (size_t)im.h;
    size_t px = 0, pos = 0;
    uint8_t* out = im.rgb.data();
    auto put = [&](uint8_t b, uint8_t g, uint8_t r) {
        out[px * 3 + 0] = r;
        out[px * 3 + 1] = g;
        out[px * 3 + 2] = b;
        px++;
    };
    while (px < total) {
        if (pos >= n) return false;
        uint8_t c = d[pos++];
        size_t cnt = (size_t)(c & 0x7F) + 1;
        if (c & 0x80) {
            if (pos + 3 > n) return false;
            uint8_t b = d[pos], g = d[pos + 1], r = d[pos + 2];
            pos += 3;
            for (size_t k = 0; k < cnt && px < total; k++) put(b, g, r);
        } else {
            for (size_t k = 0; k < cnt && px < total; k++) {
                if (pos + 3 > n) return false;
                put(d[pos], d[pos + 1], d[pos + 2]);
                pos += 3;
            }
        }
    }
    return true;
}

bool decodeRaw(const uint8_t* d, size_t n, Image& im) {
    const size_t total = (size_t)im.w * (size_t)im.h;
    size_t avail = std::min(total, n / 3);
    for (size_t i = 0; i < avail; i++) {
        im.rgb[i * 3 + 0] = d[i * 3 + 2];
        im.rgb[i * 3 + 1] = d[i * 3 + 1];
        im.rgb[i * 3 + 2] = d[i * 3 + 0];
    }
    return avail == total;
}

// Runs never cross a row boundary (same as the original encoder).
void encodeRleRow(const uint8_t* rgb, int w, Bytes& out) {
    auto same = [&](int a, int b) { return std::memcmp(rgb + 3 * a, rgb + 3 * b, 3) == 0; };
    auto pushPx = [&](int i) {
        out.push_back(rgb[3 * i + 2]);
        out.push_back(rgb[3 * i + 1]);
        out.push_back(rgb[3 * i + 0]);
    };
    int x = 0;
    while (x < w) {
        int run = 1;
        while (x + run < w && run < 128 && same(x, x + run)) run++;
        if (run >= 2) {
            out.push_back((uint8_t)(0x80 | (run - 1)));
            pushPx(x);
            x += run;
        } else {
            int start = x, n = 1;
            x++;
            while (x < w && n < 128) {
                if (x + 1 < w && same(x, x + 1)) break;  // a repeat run starts here
                x++;
                n++;
            }
            out.push_back((uint8_t)(n - 1));
            for (int i = start; i < start + n; i++) pushPx(i);
        }
    }
}

Bytes encodeRle24(const Image& im) {
    Bytes out;
    out.reserve((size_t)im.w * im.h * 3 / 2);
    for (int y = 0; y < im.h; y++) encodeRleRow(im.rgb.data() + (size_t)y * im.w * 3, im.w, out);
    return out;
}

Bytes encodeRaw(const Image& im) {
    Bytes out((size_t)im.w * im.h * 3);
    for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
        out[i * 3 + 0] = im.rgb[i * 3 + 2];
        out[i * 3 + 1] = im.rgb[i * 3 + 1];
        out[i * 3 + 2] = im.rgb[i * 3 + 0];
    }
    return out;
}

}  // namespace

namespace {

constexpr size_t kPadGap = 0x1000;     // zero bytes in front of every record (padded layout)
constexpr size_t kMaxPadded = 64;

// 1 = extracted, 0 = skipped (warning logged), -1 = fatal
int extractImage(const Bytes& f, uint32_t i, uint32_t w, uint32_t h, uint32_t type, uint64_t off,
                 uint64_t size, const fs::path& outDir, const Callbacks& cb,
                 std::vector<fs::path>& extracted) {
    const char* tname = type == 1 ? "compressed" : "raw";
    cb.info(std::string("Extracting ") + tname + " splash " + std::to_string(i) + " (" +
            std::to_string(w) + "x" + std::to_string(h) + ")...");
    if (type > 1) {
        cb.warn("    Unknown image type " + std::to_string(type) + ", skipping.");
        return 0;
    }
    if (w == 0 || h == 0 || w > 32768 || h > 32768 || (uint64_t)w * h > 50000000ull) {
        cb.warn("    Invalid image size, skipping.");
        return 0;
    }
    if (off >= f.size()) {
        cb.warn("    Image data lies outside the file, skipping.");
        return 0;
    }
    size = std::min<uint64_t>(size, f.size() - off);

    Image im;
    im.w = (int)w;
    im.h = (int)h;
    im.rgb.assign((size_t)w * h * 3, 0);
    bool complete = type == 1 ? decodeRle24(f.data() + off, (size_t)size, im)
                              : decodeRaw(f.data() + off, (size_t)size, im);
    if (!complete) cb.warn("    Image data is truncated, the rest is left black.");

    fs::path outFile = outDir / (std::to_string(i) + ".png");
    if (!writeFile(outFile, encodePNG(im))) {
        cb.error("    Cannot write " + pathToUtf8(outFile));
        return -1;
    }
    extracted.push_back(outFile);
    return 1;
}

struct Packed {
    uint32_t w, h, type, blocks;
    Bytes data;  // padded to a multiple of 512
};

bool packImages(const std::vector<fs::path>& files, bool rle, size_t maxImages, const Callbacks& cb,
                std::vector<Packed>& out) {
    for (size_t i = 0; i < files.size(); i++) {
        cb.step((int)i, (int)files.size());
        cb.info("Parsing " + pathToUtf8(files[i].filename()) + "...");
        Image im;
        std::string err;
        if (!loadImageFile(files[i], im, err)) {
            cb.warn("    Can't open image (" + err + "), skipping...");
            continue;
        }
        if (out.size() >= maxImages) {
            cb.error("Too many images: this format holds at most " + std::to_string(maxImages) + ".");
            return false;
        }
        cb.info(std::string("    ") + std::to_string(im.w) + "x" + std::to_string(im.h) +
                (rle ? ", compressing with RLE24..." : ", storing raw..."));
        Packed p;
        p.data = rle ? encodeRle24(im) : encodeRaw(im);
        p.blocks = (uint32_t)((p.data.size() + kBlock - 1) / kBlock);
        p.data.resize((size_t)p.blocks * kBlock, 0);
        p.w = (uint32_t)im.w;
        p.h = (uint32_t)im.h;
        p.type = rle ? 1u : 0u;
        out.push_back(std::move(p));
    }
    if (out.empty()) {
        cb.error("No usable images.");
        return false;
    }
    return true;
}

bool finishWrite(const fs::path& outImg, const Bytes& out, size_t nFiles, const Callbacks& cb) {
    cb.info("Writing out image...");
    if (!writeFile(outImg, out)) {
        cb.error("Output file is inaccessible!");
        return false;
    }
    cb.step((int)nFiles, (int)nFiles);
    cb.ok("All done, output is \"" + pathToUtf8(outImg) + "\" (" + std::to_string(out.size()) + " bytes).");
    return true;
}

}  // namespace

bool unpackQualcomm(const fs::path& in, const fs::path& outDir, const Callbacks& cb,
                    std::vector<fs::path>& extracted) {
    cb.info("Loading splash payload...");
    Bytes f;
    if (!readFile(in, f)) {
        cb.error("Specified payload is inaccessible!");
        return false;
    }
    if (f.size() < kBlock) {
        cb.error("File is too small to be a splash image.");
        return false;
    }
    if (std::memcmp(f.data(), "SPLASH!!", 8) != 0) {
        cb.error("Magic header mismatched, expected SPLASH!! at offset 0. "
                 "(If the magic is at 0x1000, choose the \"padded\" Qualcomm format.)");
        return false;
    }
    cb.info("Magic header matched.");

    uint32_t count = rdLE32(f, 8);
    if (count == 0) {
        cb.error("Payload doesn't contain any images.");
        return false;
    }
    if (count > kMaxImages) {
        cb.error("Header claims " + std::to_string(count) + " images, but at most " +
                 std::to_string(kMaxImages) + " fit in the header.");
        return false;
    }
    cb.info("Found " + std::to_string(count) + " images in payload.");

    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        cb.error("Output dir is inaccessible!");
        return false;
    }

    for (uint32_t i = 0; i < count; i++) {
        cb.step((int)i, (int)count);
        size_t e = 12 + (size_t)i * 20;
        int r = extractImage(f, i, rdLE32(f, e), rdLE32(f, e + 4), rdLE32(f, e + 8), rdLE32(f, e + 16),
                             (uint64_t)rdLE32(f, e + 12) * kBlock, outDir, cb, extracted);
        if (r < 0) return false;
    }
    cb.step((int)count, (int)count);
    if (extracted.empty()) {
        cb.error("No image could be extracted.");
        return false;
    }
    cb.ok("Extracted " + std::to_string(extracted.size()) + " image(s) to \"" + pathToUtf8(outDir) + "\".");
    return true;
}

bool repackQualcomm(const fs::path& dir, const fs::path& outImg, bool rle, const Callbacks& cb) {
    cb.info("Processing input folder...");
    auto files = listImageFiles(dir);
    if (files.empty()) {
        cb.error("No images (png/jpg/bmp/gif/tif) found in the folder.");
        return false;
    }
    cb.info("Found " + std::to_string(files.size()) + " images (natural file-name order).");

    std::vector<Packed> packed;
    if (!packImages(files, rle, kMaxImages, cb, packed)) return false;

    cb.info("Constructing payload header...");
    Bytes out = {'S', 'P', 'L', 'A', 'S', 'H', '!', '!'};
    wrLE32(out, (uint32_t)packed.size());
    size_t offset = kBlock;
    for (auto& p : packed) {
        wrLE32(out, p.w);
        wrLE32(out, p.h);
        wrLE32(out, p.type);
        wrLE32(out, p.blocks);
        wrLE32(out, (uint32_t)offset);
        offset += p.data.size();
    }
    out.resize(kBlock, 0);
    for (auto& p : packed) out.insert(out.end(), p.data.begin(), p.data.end());
    return finishWrite(outImg, out, files.size(), cb);
}

// ---- padded layout (Lenovo & co.) -------------------------------------------

bool unpackQualcommPadded(const fs::path& in, const fs::path& outDir, const Callbacks& cb,
                          std::vector<fs::path>& extracted) {
    cb.info("Loading splash payload...");
    Bytes f;
    if (!readFile(in, f)) {
        cb.error("Specified payload is inaccessible!");
        return false;
    }
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        cb.error("Output dir is inaccessible!");
        return false;
    }

    uint32_t idx = 0;
    size_t pos = 0;
    while (pos + kBlock <= f.size() && idx < kMaxPadded) {
        // next record header: 512-aligned "SPLASH!!"
        size_t o = pos;
        while (o + kBlock <= f.size() && std::memcmp(f.data() + o, "SPLASH!!", 8) != 0) o += kBlock;
        if (o + kBlock > f.size()) break;
        if (idx == 0) cb.info("Magic header matched at 0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%zX", o); return std::string(b); }() + ".");
        uint32_t blocks = rdLE32(f, o + 20);
        int r = extractImage(f, idx, rdLE32(f, o + 8), rdLE32(f, o + 12), rdLE32(f, o + 16), o + kBlock,
                             (uint64_t)blocks * kBlock, outDir, cb, extracted);
        if (r < 0) return false;
        idx++;
        pos = o + kBlock + (size_t)blocks * kBlock;
    }
    if (idx == 0) {
        cb.error("Magic header mismatched, no \"SPLASH!!\" record found.");
        return false;
    }
    cb.info("Found " + std::to_string(idx) + " image record(s) in payload.");
    cb.step((int)idx, (int)idx);
    if (extracted.empty()) {
        cb.error("No image could be extracted.");
        return false;
    }
    cb.ok("Extracted " + std::to_string(extracted.size()) + " image(s) to \"" + pathToUtf8(outDir) + "\".");
    return true;
}

bool repackQualcommPadded(const fs::path& dir, const fs::path& outImg, bool rle, const Callbacks& cb) {
    cb.info("Processing input folder...");
    auto files = listImageFiles(dir);
    if (files.empty()) {
        cb.error("No images (png/jpg/bmp/gif/tif) found in the folder.");
        return false;
    }
    cb.info("Found " + std::to_string(files.size()) + " images (natural file-name order).");

    std::vector<Packed> packed;
    if (!packImages(files, rle, kMaxPadded, cb, packed)) return false;

    cb.info("Constructing records...");
    Bytes out;
    for (auto& p : packed) {
        out.insert(out.end(), kPadGap, 0);
        size_t h0 = out.size();
        out.insert(out.end(), {'S', 'P', 'L', 'A', 'S', 'H', '!', '!'});
        wrLE32(out, p.w);
        wrLE32(out, p.h);
        wrLE32(out, p.type);
        wrLE32(out, p.blocks);
        out.resize(h0 + kBlock, 0);
        out.insert(out.end(), p.data.begin(), p.data.end());
    }
    return finishWrite(outImg, out, files.size(), cb);
}
