#include "util/image.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace ReaClaw::Image;

namespace {

uint32_t be32(const std::vector<uint8_t>& d, size_t off) {
    return (d[off] << 24) | (d[off + 1] << 16) | (d[off + 2] << 8) | d[off + 3];
}

// Minimal PNG parser used only by the tests: validates structure and rebuilds
// the raw pixel buffer from the DEFLATE "stored" IDAT stream the encoder emits.
struct ParsedPng {
    bool valid = false;
    uint32_t w = 0, h = 0;
    bool has_iend = false;
    std::vector<uint8_t> raw;  // reconstructed scanlines (filter byte + RGB)

    uint8_t at(int x, int y, int c) const {
        size_t stride = 1 + static_cast<size_t>(w) * 3;
        return raw[y * stride + 1 + static_cast<size_t>(x) * 3 + c];
    }
};

ParsedPng parse(const std::vector<uint8_t>& d) {
    ParsedPng p;
    const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (d.size() < 8)
        return p;
    for (int i = 0; i < 8; i++)
        if (d[i] != sig[i])
            return p;

    std::vector<uint8_t> idat;
    size_t off = 8;
    bool saw_ihdr = false;
    while (off + 8 <= d.size()) {
        uint32_t len = be32(d, off);
        std::string type(d.begin() + off + 4, d.begin() + off + 8);
        size_t data = off + 8;
        if (type == "IHDR") {
            p.w = be32(d, data);
            p.h = be32(d, data + 4);
            saw_ihdr = true;
        } else if (type == "IDAT") {
            idat.insert(idat.end(), d.begin() + data, d.begin() + data + len);
        } else if (type == "IEND") {
            p.has_iend = true;
        }
        off = data + len + 4;  // skip data + CRC
    }
    if (!saw_ihdr || !p.has_iend)
        return p;

    // Inflate the "stored" zlib stream (skip 2-byte zlib header + trailing adler).
    size_t z = 2;
    while (z + 5 <= idat.size()) {
        uint8_t bfinal = idat[z] & 1;
        uint8_t btype = (idat[z] >> 1) & 3;
        if (btype != 0)
            break;  // encoder only emits stored blocks
        uint16_t blen = idat[z + 1] | (idat[z + 2] << 8);
        z += 5;
        p.raw.insert(p.raw.end(), idat.begin() + z, idat.begin() + z + blen);
        z += blen;
        if (bfinal)
            break;
    }
    p.valid = true;
    return p;
}

}  // namespace

TEST(ImagePng, SignatureAndDimensions) {
    Canvas c(7, 5, 10, 20, 30);
    auto png = encode_png(c);
    auto p = parse(png);
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.w, 7u);
    EXPECT_EQ(p.h, 5u);
    EXPECT_TRUE(p.has_iend);
}

TEST(ImagePng, BackgroundFillRoundTrips) {
    Canvas c(4, 3, 11, 22, 33);
    auto p = parse(encode_png(c));
    ASSERT_TRUE(p.valid);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 4; x++) {
            EXPECT_EQ(p.at(x, y, 0), 11);
            EXPECT_EQ(p.at(x, y, 1), 22);
            EXPECT_EQ(p.at(x, y, 2), 33);
        }
}

TEST(ImagePng, SetAndFillRectRoundTrip) {
    Canvas c(6, 6, 0, 0, 0);
    c.set(2, 3, 250, 1, 2);
    c.fill_rect(0, 0, 2, 2, 5, 6, 7);  // covers (0..1, 0..1)
    auto p = parse(encode_png(c));
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.at(2, 3, 0), 250);
    EXPECT_EQ(p.at(2, 3, 1), 1);
    EXPECT_EQ(p.at(0, 0, 0), 5);
    EXPECT_EQ(p.at(1, 1, 2), 7);
    EXPECT_EQ(p.at(2, 2, 0), 0);  // outside the rect (x1/y1 exclusive)
}

TEST(ImageCanvas, OutOfBoundsIsIgnored) {
    Canvas c(3, 3, 0, 0, 0);
    c.set(-1, 0, 9, 9, 9);
    c.set(0, 99, 9, 9, 9);
    c.set(3, 3, 9, 9, 9);
    auto p = parse(encode_png(c));
    ASSERT_TRUE(p.valid);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++)
            EXPECT_EQ(p.at(x, y, 0), 0);
}

TEST(ImageCanvas, LargeCanvasSpansMultipleStoredBlocks) {
    // > 65535 raw bytes forces more than one DEFLATE stored block.
    Canvas c(200, 200, 1, 2, 3);
    auto p = parse(encode_png(c));
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.w, 200u);
    EXPECT_EQ(p.at(199, 199, 1), 2);
}

TEST(ImageText, WidthAndAdvance) {
    EXPECT_EQ(text_width("12", 1), 12);   // 2 chars * (5+1)
    EXPECT_EQ(text_width("abc", 2), 36);  // 3 chars * (5+1) * 2
    Canvas c(40, 10, 0, 0, 0);
    int endx = c.draw_text(0, 0, "12", 1, 200, 200, 200);
    EXPECT_EQ(endx, 12);
}

TEST(ImageText, MinusGlyphDrawsMiddleRowOnly) {
    Canvas c(8, 8, 0, 0, 0);
    c.draw_char(0, 0, '-', 1, 255, 255, 255);  // '-' is row 3 only, all 5 columns
    auto p = parse(encode_png(c));
    ASSERT_TRUE(p.valid);
    for (int x = 0; x < 5; x++) {
        EXPECT_EQ(p.at(x, 3, 0), 255) << "col " << x;  // middle row set
        EXPECT_EQ(p.at(x, 0, 0), 0) << "col " << x;    // top row clear
    }
}

TEST(ImageCanvas, LineWThickness) {
    Canvas c(10, 10, 0, 0, 0);
    c.line_w(0, 5, 9, 5, 3, 255, 0, 0);  // horizontal, 3px tall → rows 4,5,6
    auto p = parse(encode_png(c));
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.at(5, 4, 0), 255);
    EXPECT_EQ(p.at(5, 5, 0), 255);
    EXPECT_EQ(p.at(5, 6, 0), 255);
    EXPECT_EQ(p.at(5, 2, 0), 0);
}

TEST(ImageBase64, KnownVectors) {
    auto enc = [](const std::string& s) {
        return base64(std::vector<uint8_t>(s.begin(), s.end()));
    };
    EXPECT_EQ(enc(""), "");
    EXPECT_EQ(enc("f"), "Zg==");
    EXPECT_EQ(enc("fo"), "Zm8=");
    EXPECT_EQ(enc("foo"), "Zm9v");
    EXPECT_EQ(enc("foob"), "Zm9vYg==");
    EXPECT_EQ(enc("fooba"), "Zm9vYmE=");
    EXPECT_EQ(enc("foobar"), "Zm9vYmFy");
}
