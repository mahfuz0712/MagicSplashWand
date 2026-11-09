// Magic Splash Wand - C++ GUI port
// Image helpers. Decoding of arbitrary formats (PNG/JPEG/GIF/TIFF/BMP...) is done
// by GDI+ on Windows. PNG/BMP *encoding* is implemented here (portable).
#pragma once

#include "util.h"

struct Image {
    int w = 0, h = 0;
    Bytes rgb;  // w*h*3, row-major, R,G,B (alpha is dropped like the PHP/GD tool does)
};

// GDI+ startup/shutdown (no-ops on other platforms).
bool initImaging();
void shutdownImaging();

// Decode any supported image file into RGB.
bool loadImageFile(const fs::path& p, Image& out, std::string& err);

// Portable decoder for uncompressed 24/32-bit BMP.
bool decodeBMP(const Bytes& data, Image& out);

Bytes encodePNG(const Image& im);   // 8-bit RGB PNG
Bytes encodeBMP24(const Image& im); // 24-bit BMP (what GD's imagebmp produces for truecolor)

inline bool looksLikeBMP(const Bytes& b) { return b.size() > 2 && b[0] == 'B' && b[1] == 'M'; }
