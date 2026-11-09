// OPlus (OPPO / OnePlus / Realme) splash format ("SPLASH LOGO!").
//
//   0x0000  padding[0x4000]               (kept in .metadata)
//   0x4000  header[0x4000]
//             char[12] magic "SPLASH LOGO!"
//             char[64] desc1..desc4
//             u32 count, version, width, height, compress(0 raw / 1 GZIP)
//             0x120: entries { u32 offset, real_size, data_size; char[116] name }  (0x80 each)
//   0x8000  payload (entry offsets are relative to here); each image is a .bmp
//
// Unpacking writes <name>.bmp files and a ".metadata" JSON that is compatible
// with the original PHP tool, so both tools can repack each other's output.
#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#include "image.h"
#include "json.h"
#include "splash.h"

namespace {

constexpr size_t kPad = 0x4000, kHdr = 0x4000, kData = 0x8000;
constexpr size_t kEntries = 288, kEntrySize = 128, kNameLen = 116;
constexpr size_t kMaxEntries = (kHdr - kEntries) / kEntrySize;  // 125
constexpr char kMagic[12] = {'S', 'P', 'L', 'A', 'S', 'H', ' ', 'L', 'O', 'G', 'O', '!'};

void padTo(Bytes& b, size_t n) { b.resize(n, 0); }

void appendField(Bytes& out, const std::string& s, size_t width) {
    size_t n = std::min(s.size(), width);
    out.insert(out.end(), s.begin(), s.begin() + (ptrdiff_t)n);
    out.insert(out.end(), width - n, 0);
}

}  // namespace

bool unpackOPlus(const fs::path& in, const fs::path& outDir, const Callbacks& cb,
                 std::vector<fs::path>& extracted) {
    cb.info("Loading splash payload...");
    Bytes f;
    if (!readFile(in, f)) {
        cb.error("Specified payload is inaccessible!");
        return false;
    }
    if (f.size() < kPad + kEntries) {
        cb.error("File is too small to be an OPlus splash image.");
        return false;
    }
    Bytes padding(f.begin(), f.begin() + (ptrdiff_t)kPad);
    Bytes hdr(f.begin() + (ptrdiff_t)kPad, f.begin() + (ptrdiff_t)std::min(f.size(), kData));
    padTo(hdr, kHdr);

    cb.info("Analyzing payload header...");
    if (std::memcmp(hdr.data(), kMagic, 12) != 0) {
        cb.error("Magic header mismatched, expected \"SPLASH LOGO!\" at 0x4000.");
        return false;
    }
    cb.info("Magic header matched.");

    std::vector<std::string> descs;
    for (int i = 0; i < 4; i++) descs.push_back(trimField(hdr.data() + 12 + 64 * i, 64));
    uint32_t count = rdLE32(hdr, 268), version = rdLE32(hdr, 272), width = rdLE32(hdr, 276),
             height = rdLE32(hdr, 280);
    bool compress = rdLE32(hdr, 284) == 1;

    if (count == 0) {
        cb.error("Payload doesn't contain any images.");
        return false;
    }
    if (count > kMaxEntries) {
        cb.error("Header claims " + std::to_string(count) + " images, at most " +
                 std::to_string(kMaxEntries) + " fit in the header.");
        return false;
    }
    for (auto& d : descs)
        if (!d.empty()) cb.info("Splash description: " + d);
    cb.info("Splash version: " + std::to_string(version));
    cb.info("Splash size: " + std::to_string(width) + "x" + std::to_string(height));
    cb.info(std::string("GZIP compression: ") + (compress ? "enabled" : "disabled"));
    cb.info("Found " + std::to_string(count) + " images in payload.");

    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        cb.error("Output dir is inaccessible!");
        return false;
    }

    std::vector<std::string> names;
    std::set<std::string> used;
    for (uint32_t i = 0; i < count; i++) {
        cb.step((int)i, (int)count);
        size_t e = kEntries + (size_t)i * kEntrySize;
        uint64_t off = rdLE32(hdr, e), real = rdLE32(hdr, e + 4), dsize = rdLE32(hdr, e + 8);
        std::string raw = trimField(hdr.data() + e + 12, kNameLen);
        std::string name = sanitizeName(raw);
        if (name != raw) cb.warn("    Unsafe/empty image name \"" + raw + "\" replaced by \"" + name + "\".");
        // keep names unique (Windows is case-insensitive)
        std::string base = name;
        for (int n = 2; used.count(toLower(name)); n++) name = base + "_" + std::to_string(n);
        used.insert(toLower(name));
        names.push_back(name);

        cb.info(std::string("Extracting ") + (compress ? "compressed" : "raw") + " splash " + name + "...");
        uint64_t start = kData + off;
        if (start + dsize > f.size()) {
            cb.warn("    Image data lies outside the file, skipping.");
            continue;
        }
        Bytes payload(f.begin() + (ptrdiff_t)start, f.begin() + (ptrdiff_t)(start + dsize));
        if (compress) {
            Bytes plain;
            if (!gunzip(payload.data(), payload.size(), plain)) {
                cb.warn("    GZIP data is corrupt, skipping.");
                continue;
            }
            payload.swap(plain);
        }
        if (payload.size() != real)
            cb.warn("    Size mismatched, expected " + std::to_string(real) + ", but found " +
                    std::to_string(payload.size()));
        fs::path outFile = outDir / utf8ToPath(name + ".bmp");
        if (!writeFile(outFile, payload)) {
            cb.error("    Cannot write " + pathToUtf8(outFile));
            return false;
        }
        extracted.push_back(outFile);
    }
    cb.step((int)count, (int)count);
    if (extracted.empty()) {
        cb.error("No image could be extracted.");
        return false;
    }

    cb.info("Writing out payload metadata...");
    Bytes padGz;
    gzipCompress(padding.data(), padding.size(), padGz, 9);
    std::string js = "{\"c\":";
    js += compress ? "true" : "false";
    js += ",\"d\":[";
    for (size_t i = 0; i < descs.size(); i++) {
        if (i) js += ",";
        js += jsonQuote(base64Encode((const uint8_t*)descs[i].data(), descs[i].size()));
    }
    js += "],\"h\":" + std::to_string(height);
    js += ",\"p\":" + jsonQuote(base64Encode(padGz.data(), padGz.size()));
    js += ",\"s\":[";
    for (size_t i = 0; i < names.size(); i++) {
        if (i) js += ",";
        js += jsonQuote(names[i]);
    }
    js += "],\"v\":" + std::to_string(version) + ",\"w\":" + std::to_string(width) + "}";
    if (!writeFile(outDir / ".metadata", Bytes(js.begin(), js.end()))) {
        cb.error("Cannot write .metadata");
        return false;
    }
    cb.ok("Extracted " + std::to_string(extracted.size()) + " image(s) to \"" + pathToUtf8(outDir) +
          "\". Keep the .metadata file for repacking!");
    return true;
}

bool repackOPlus(const fs::path& dir, const fs::path& outImg, const Callbacks& cb) {
    fs::path metaPath = dir / ".metadata";
    Bytes metaBytes;
    if (!readFile(metaPath, metaBytes)) {
        cb.error("Payload metadata file (.metadata) is missing - it is created when unpacking.");
        return false;
    }
    cb.info("Loading payload metadata...");
    JVal meta;
    if (!parseJson(std::string(metaBytes.begin(), metaBytes.end()), meta) || meta.type != JVal::Obj) {
        cb.error("Invalid metadata format!");
        return false;
    }
    const JVal *jc = meta.get("c"), *jd = meta.get("d"), *jh = meta.get("h"), *jp = meta.get("p"),
               *js = meta.get("s"), *jv = meta.get("v"), *jw = meta.get("w");
    if (!jc || !jd || !jh || !jp || !js || !jv || !jw || jd->type != JVal::Arr || js->type != JVal::Arr ||
        jp->type != JVal::Str || jh->type != JVal::Num || jv->type != JVal::Num || jw->type != JVal::Num) {
        cb.error("Invalid metadata format!");
        return false;
    }
    bool compress = jc->type == JVal::Bool ? jc->b : (jc->type == JVal::Num && jc->n != 0);
    uint32_t height = (uint32_t)jh->n, version = (uint32_t)jv->n, width = (uint32_t)jw->n;

    std::vector<std::string> names;
    for (auto& n : js->a) names.push_back(n.s);  // strings, or raw text of numeric names
    cb.info("Metadata contains " + std::to_string(names.size()) + " registered images.");

    Bytes padGz, padding;
    if (!base64Decode(jp->s, padGz) || !gunzip(padGz.data(), padGz.size(), padding, 1 << 20)) {
        cb.error("Metadata padding is corrupt!");
        return false;
    }
    padTo(padding, kPad);

    // stem -> best candidate file (newest wins if several formats exist)
    cb.info("Processing input folder...");
    auto files = listImageFiles(dir);
    std::map<std::string, fs::path> byStem;
    for (auto& p : files) {
        std::string key = toLower(pathToUtf8(p.stem()));
        auto it = byStem.find(key);
        if (it == byStem.end()) {
            byStem[key] = p;
        } else {
            std::error_code e1, e2;
            auto t1 = fs::last_write_time(it->second, e1), t2 = fs::last_write_time(p, e2);
            fs::path win = (!e1 && !e2 && t2 > t1) ? p : it->second;
            cb.warn("    Several files named \"" + pathToUtf8(p.stem()) + "\" - using the newest: " +
                    pathToUtf8(win.filename()));
            it->second = win;
        }
    }
    cb.info("Found " + std::to_string(files.size()) + " images.");

    std::vector<std::pair<std::string, fs::path>> valid;
    for (auto& n : names) {
        auto it = byStem.find(toLower(n));
        if (it != byStem.end()) valid.emplace_back(n, it->second);
        else cb.warn("    No file for \"" + n + "\" in the folder, it will be left out.");
    }
    if (valid.empty()) {
        cb.error("None of the registered images were found in the folder.");
        return false;
    }
    if (valid.size() > kMaxEntries) {
        cb.error("Too many images (max " + std::to_string(kMaxEntries) + ").");
        return false;
    }
    cb.info("Totally " + std::to_string(valid.size()) + " images will be added.");

    Bytes entries, payload;
    uint32_t added = 0;
    for (size_t i = 0; i < valid.size(); i++) {
        cb.step((int)i, (int)valid.size());
        const std::string& name = valid[i].first;
        const fs::path& file = valid[i].second;
        cb.info("Parsing " + name + "...");
        Bytes bmp;
        if (!readFile(file, bmp)) {
            cb.warn("    Can't read file, skipping...");
            continue;
        }
        if (!looksLikeBMP(bmp)) {
            Image im;
            std::string err;
            if (!loadImageFile(file, im, err)) {
                cb.warn("    Can't open image (" + err + "), skipping...");
                continue;
            }
            cb.info("    Converting to BMP format...");
            bmp = encodeBMP24(im);
        }
        Bytes out;
        if (compress) {
            cb.info("    Compressing with GZIP...");
            if (!gzipCompress(bmp.data(), bmp.size(), out, 9)) {
                cb.warn("    Compression failed, skipping...");
                continue;
            }
        } else {
            out = bmp;
        }
        wrLE32(entries, (uint32_t)payload.size());
        wrLE32(entries, (uint32_t)bmp.size());
        wrLE32(entries, (uint32_t)out.size());
        appendField(entries, name, kNameLen);
        payload.insert(payload.end(), out.begin(), out.end());
        added++;
    }
    if (added == 0) {
        cb.error("No usable images.");
        return false;
    }

    cb.info("Constructing payload header...");
    Bytes hdr;
    hdr.insert(hdr.end(), kMagic, kMagic + 12);
    for (size_t i = 0; i < 4; i++) {
        std::string d;
        if (i < jd->a.size() && jd->a[i].type == JVal::Str) {
            Bytes raw;
            base64Decode(jd->a[i].s, raw);
            d.assign(raw.begin(), raw.end());
        }
        appendField(hdr, d, 64);
    }
    wrLE32(hdr, added);
    wrLE32(hdr, version);
    wrLE32(hdr, width);
    wrLE32(hdr, height);
    wrLE32(hdr, compress ? 1u : 0u);
    hdr.insert(hdr.end(), entries.begin(), entries.end());
    padTo(hdr, kHdr);

    cb.info("Merging payload...");
    Bytes out = padding;
    out.insert(out.end(), hdr.begin(), hdr.end());
    out.insert(out.end(), payload.begin(), payload.end());

    cb.info("Writing out image...");
    if (!writeFile(outImg, out)) {
        cb.error("Output file is inaccessible!");
        return false;
    }
    cb.step((int)valid.size(), (int)valid.size());
    cb.ok("All done, output is \"" + pathToUtf8(outImg) + "\" (" + std::to_string(out.size()) + " bytes).");
    return true;
}
