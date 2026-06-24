#include "util/image.h"

#include <algorithm>
#include <cstdlib>

namespace ReaClaw::Image {

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

Canvas::Canvas(int width, int height, uint8_t r, uint8_t g, uint8_t b)
    : w(width < 1 ? 1 : width), h(height < 1 ? 1 : height), px(static_cast<size_t>(w) * h * 3) {
    for (size_t i = 0; i < px.size(); i += 3) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
}

void Canvas::set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= w || y >= h)
        return;
    size_t i = (static_cast<size_t>(y) * w + x) * 3;
    px[i] = r;
    px[i + 1] = g;
    px[i + 2] = b;
}

void Canvas::fill_rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    if (x1 < x0)
        std::swap(x0, x1);
    if (y1 < y0)
        std::swap(y0, y1);
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(w, x1);
    y1 = std::min(h, y1);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            set(x, y, r, g, b);
}

void Canvas::hline(int x0, int x1, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x1 < x0)
        std::swap(x0, x1);
    for (int x = x0; x <= x1; x++)
        set(x, y, r, g, b);
}

void Canvas::vline(int x, int y0, int y1, uint8_t r, uint8_t g, uint8_t b) {
    if (y1 < y0)
        std::swap(y0, y1);
    for (int y = y0; y <= y1; y++)
        set(x, y, r, g, b);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        set(x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Canvas::line_w(int x0, int y0, int x1, int y1, int width, uint8_t r, uint8_t g, uint8_t b) {
    if (width < 1)
        width = 1;
    // Stack the line vertically (good enough for the near-horizontal plot lines
    // we draw — spectrum curve, loudness contour).
    int half = width / 2;
    for (int o = -half; o <= half; o++)
        line(x0, y0 + o, x1, y1 + o, r, g, b);
}

// ---------------------------------------------------------------------------
// 5x7 bitmap font — only the glyphs axis labels need. Rows are top→bottom,
// each row's low 5 bits are columns left→bit4 … right→bit0.
// ---------------------------------------------------------------------------

namespace {

const uint8_t* glyph(char ch) {
    // clang-format off
    static const uint8_t G_space[7] = {0,0,0,0,0,0,0};
    static const uint8_t G_0[7] = {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110};
    static const uint8_t G_1[7] = {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110};
    static const uint8_t G_2[7] = {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111};
    static const uint8_t G_3[7] = {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110};
    static const uint8_t G_4[7] = {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010};
    static const uint8_t G_5[7] = {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110};
    static const uint8_t G_6[7] = {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110};
    static const uint8_t G_7[7] = {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000};
    static const uint8_t G_8[7] = {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110};
    static const uint8_t G_9[7] = {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100};
    static const uint8_t G_minus[7] = {0,0,0,0b11111,0,0,0};
    static const uint8_t G_dot[7] = {0,0,0,0,0,0b01100,0b01100};
    static const uint8_t G_colon[7] = {0,0b01100,0b01100,0,0b01100,0b01100,0};
    static const uint8_t G_H[7] = {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001};
    static const uint8_t G_B[7] = {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110};
    static const uint8_t G_k[7] = {0b10000,0b10000,0b10010,0b10100,0b11000,0b10100,0b10010};
    static const uint8_t G_z[7] = {0,0,0b11111,0b00010,0b00100,0b01000,0b11111};
    static const uint8_t G_d[7] = {0b00001,0b00001,0b01101,0b10011,0b10001,0b10011,0b01101};
    static const uint8_t G_s[7] = {0,0,0b01111,0b10000,0b01110,0b00001,0b11110};
    static const uint8_t G_m[7] = {0,0,0b11010,0b10101,0b10101,0b10101,0b10101};
    // clang-format on
    switch (ch) {
        case '0':
            return G_0;
        case '1':
            return G_1;
        case '2':
            return G_2;
        case '3':
            return G_3;
        case '4':
            return G_4;
        case '5':
            return G_5;
        case '6':
            return G_6;
        case '7':
            return G_7;
        case '8':
            return G_8;
        case '9':
            return G_9;
        case '-':
            return G_minus;
        case '.':
            return G_dot;
        case ':':
            return G_colon;
        case 'H':
            return G_H;
        case 'B':
            return G_B;
        case 'k':
            return G_k;
        case 'z':
            return G_z;
        case 'd':
            return G_d;
        case 's':
            return G_s;
        case 'm':
            return G_m;
        default:
            return G_space;
    }
}

}  // namespace

void Canvas::draw_char(int x, int y, char ch, int scale, uint8_t r, uint8_t g, uint8_t b) {
    if (scale < 1)
        scale = 1;
    const uint8_t* gph = glyph(ch);
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (gph[row] & (1 << (4 - col)))
                fill_rect(x + col * scale,
                          y + row * scale,
                          x + col * scale + scale,
                          y + row * scale + scale,
                          r,
                          g,
                          b);
}

int Canvas::draw_text(int x,
                      int y,
                      const std::string& s,
                      int scale,
                      uint8_t r,
                      uint8_t g,
                      uint8_t b) {
    if (scale < 1)
        scale = 1;
    int cx = x;
    for (char ch : s) {
        draw_char(cx, y, ch, scale, r, g, b);
        cx += (5 + 1) * scale;
    }
    return cx;
}

int text_width(const std::string& s, int scale) {
    if (scale < 1)
        scale = 1;
    return static_cast<int>(s.size()) * (5 + 1) * scale;
}

// ---------------------------------------------------------------------------
// PNG encoder
// ---------------------------------------------------------------------------

namespace {

uint32_t crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void put_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((v >> 24) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

void put_chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put_be32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> typed(type, type + 4);
    typed.insert(typed.end(), data.begin(), data.end());
    out.insert(out.end(), typed.begin(), typed.end());
    put_be32(out, crc32(typed.data(), typed.size()));
}

// Wrap raw bytes in a zlib stream using DEFLATE "stored" (uncompressed) blocks.
std::vector<uint8_t> zlib_store(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z;
    z.push_back(0x78);  // CMF: deflate, 32K window
    z.push_back(0x01);  // FLG: no dict, check bits make 0x7801 a multiple of 31
    size_t off = 0;
    while (off < raw.size() || raw.empty()) {
        size_t chunk = std::min<size_t>(raw.size() - off, 65535u);
        bool last = (off + chunk >= raw.size());
        z.push_back(last ? 1 : 0);  // BFINAL, BTYPE=00 (stored)
        uint16_t len = static_cast<uint16_t>(chunk);
        uint16_t nlen = ~len;
        z.push_back(len & 0xFF);
        z.push_back((len >> 8) & 0xFF);
        z.push_back(nlen & 0xFF);
        z.push_back((nlen >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + chunk);
        off += chunk;
        if (last)
            break;
    }
    put_be32(z, adler32(raw.data(), raw.size()));
    return z;
}

}  // namespace

std::vector<uint8_t> encode_png(const Canvas& c) {
    std::vector<uint8_t> out = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};  // signature

    // IHDR
    std::vector<uint8_t> ihdr;
    put_be32(ihdr, static_cast<uint32_t>(c.w));
    put_be32(ihdr, static_cast<uint32_t>(c.h));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // color type: truecolor RGB
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    put_chunk(out, "IHDR", ihdr);

    // Raw image data: each scanline prefixed with filter byte 0 (none).
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(c.h) * (1 + c.w * 3));
    for (int y = 0; y < c.h; y++) {
        raw.push_back(0);
        const uint8_t* row = &c.px[static_cast<size_t>(y) * c.w * 3];
        raw.insert(raw.end(), row, row + static_cast<size_t>(c.w) * 3);
    }
    put_chunk(out, "IDAT", zlib_store(raw));
    put_chunk(out, "IEND", {});
    return out;
}

// ---------------------------------------------------------------------------
// base64
// ---------------------------------------------------------------------------

std::string base64(const std::vector<uint8_t>& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
    }
    size_t rem = data.size() - i;
    if (rem == 1) {
        uint32_t n = data[i] << 16;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

}  // namespace ReaClaw::Image
