#include "image.h"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

void be32(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 24));
    b.push_back((uint8_t)(v >> 16));
    b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)v);
}

void pngChunk(Bytes& out, const char type[4], const Bytes& data) {
    be32(out, (uint32_t)data.size());
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + start, (uInt)(out.size() - start));
    be32(out, (uint32_t)crc);
}

inline int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

}  // namespace

Bytes encodePNG(const Image& im) {
    const size_t stride = (size_t)im.w * 3;
    Bytes raw;
    raw.reserve((stride + 1) * (size_t)im.h);

    Bytes zero(stride, 0);
    Bytes cand[5];
    for (auto& c : cand) c.resize(stride);

    for (int y = 0; y < im.h; y++) {
        const uint8_t* cur = im.rgb.data() + (size_t)y * stride;
        const uint8_t* prev = y ? cur - stride : zero.data();
        for (size_t i = 0; i < stride; i++) {
            int a = i >= 3 ? cur[i - 3] : 0;
            int b = prev[i];
            int c = i >= 3 ? prev[i - 3] : 0;
            cand[0][i] = cur[i];
            cand[1][i] = (uint8_t)(cur[i] - a);
            cand[2][i] = (uint8_t)(cur[i] - b);
            cand[3][i] = (uint8_t)(cur[i] - ((a + b) >> 1));
            cand[4][i] = (uint8_t)(cur[i] - paeth(a, b, c));
        }
        // Heuristic from the PNG spec: minimise the sum of absolute values.
        int best = 0;
        long bestScore = -1;
        for (int f = 0; f < 5; f++) {
            long score = 0;
            for (size_t i = 0; i < stride; i++) score += std::abs((int)(int8_t)cand[f][i]);
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                best = f;
            }
        }
        raw.push_back((uint8_t)best);
        raw.insert(raw.end(), cand[best].begin(), cand[best].end());
    }

    uLongf zlen = compressBound((uLong)raw.size());
    Bytes z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), 6) != Z_OK) return {};
    z.resize(zlen);

    Bytes out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes ihdr;
    be32(ihdr, (uint32_t)im.w);
    be32(ihdr, (uint32_t)im.h);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // truecolor
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", z);
    pngChunk(out, "IEND", Bytes());
    return out;
}

Bytes encodeBMP24(const Image& im) {
    const size_t rowSize = ((size_t)im.w * 3 + 3) & ~(size_t)3;
    const size_t dataSize = rowSize * (size_t)im.h;
    Bytes out;
    out.reserve(54 + dataSize);
    out.push_back('B');
    out.push_back('M');
    wrLE32(out, (uint32_t)(54 + dataSize));
    wrLE32(out, 0);
    wrLE32(out, 54);
    // BITMAPINFOHEADER
    wrLE32(out, 40);
    wrLE32(out, (uint32_t)im.w);
    wrLE32(out, (uint32_t)im.h);  // positive = bottom-up
    out.push_back(1);
    out.push_back(0);  // planes
    out.push_back(24);
    out.push_back(0);  // bpp
    wrLE32(out, 0);    // BI_RGB
    wrLE32(out, (uint32_t)dataSize);
    wrLE32(out, 2835);
    wrLE32(out, 2835);
    wrLE32(out, 0);
    wrLE32(out, 0);
    for (int y = im.h - 1; y >= 0; y--) {
        const uint8_t* row = im.rgb.data() + (size_t)y * im.w * 3;
        for (int x = 0; x < im.w; x++) {
            out.push_back(row[x * 3 + 2]);
            out.push_back(row[x * 3 + 1]);
            out.push_back(row[x * 3 + 0]);
        }
        for (size_t p = (size_t)im.w * 3; p < rowSize; p++) out.push_back(0);
    }
    return out;
}

bool decodeBMP(const Bytes& d, Image& out) {
    if (!looksLikeBMP(d) || d.size() < 54) return false;
    uint32_t dataOff = rdLE32(d, 10);
    uint32_t hdrSize = rdLE32(d, 14);
    if (hdrSize < 40) return false;
    int32_t w = (int32_t)rdLE32(d, 18);
    int32_t h = (int32_t)rdLE32(d, 22);
    uint16_t bpp = (uint16_t)(d[28] | (d[29] << 8));
    uint32_t comp = rdLE32(d, 30);
    bool topDown = h < 0;
    if (topDown) h = -h;
    if (w <= 0 || h <= 0 || w > 32768 || h > 32768) return false;
    if ((bpp != 24 && bpp != 32) || (comp != 0 && comp != 3)) return false;
    size_t bytesPP = bpp / 8;
    size_t rowSize = ((size_t)w * bpp + 31) / 32 * 4;
    if ((size_t)dataOff + rowSize * (size_t)h > d.size()) return false;
    out.w = w;
    out.h = h;
    out.rgb.assign((size_t)w * h * 3, 0);
    for (int y = 0; y < h; y++) {
        int srcY = topDown ? y : h - 1 - y;
        const uint8_t* src = d.data() + dataOff + rowSize * (size_t)srcY;
        uint8_t* dst = out.rgb.data() + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            dst[x * 3 + 0] = src[x * bytesPP + 2];
            dst[x * 3 + 1] = src[x * bytesPP + 1];
            dst[x * 3 + 2] = src[x * bytesPP + 0];
        }
    }
    return true;
}

#ifndef _WIN32
// Non-Windows fallback (only used for development/testing of the core logic).
bool initImaging() { return true; }
void shutdownImaging() {}
bool loadImageFile(const fs::path& p, Image& out, std::string& err) {
    Bytes b;
    if (!readFile(p, b)) {
        err = "cannot read file";
        return false;
    }
    if (!decodeBMP(b, out)) {
        err = "unsupported image (this build only reads BMP)";
        return false;
    }
    return true;
}
#endif
