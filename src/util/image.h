#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ReaClaw::Image {

// A minimal 8-bit RGB raster canvas with a few drawing primitives, plus a
// dependency-free PNG encoder and base64 helper. Used to render audio
// visualizations (spectrum / waveform / loudness contour) for the perception
// layer. Pure (no REAPER, no OpenSSL), so it is unit-testable on its own.
//
// The PNG encoder emits truecolor 8-bit RGB using DEFLATE "stored" (type-0)
// blocks — i.e. valid PNG with no actual compression. Visualizations are small
// (a few hundred pixels a side) so the size cost is acceptable and the encoder
// stays tiny and obviously correct.
struct Canvas {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px;  // w*h*3, RGB, row-major, top-left origin

    Canvas(int width, int height, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0);

    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b);
    // Inclusive of x0/y0, exclusive of x1/y1; clipped to canvas bounds.
    void fill_rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
    void hline(int x0, int x1, int y, uint8_t r, uint8_t g, uint8_t b);
    void vline(int x, int y0, int y1, uint8_t r, uint8_t g, uint8_t b);
    void line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);  // Bresenham
    // Thick polyline-friendly line (width px, drawn by stacking offset lines).
    void line_w(int x0, int y0, int x1, int y1, int width, uint8_t r, uint8_t g, uint8_t b);

    // Text via a built-in 5x7 bitmap font (digits, '-', '.', ':' and the few
    // letters axis labels need: H z d B k s m). Unknown chars render blank.
    // Each glyph cell is 5x7 scaled by `scale`, with a 1px (×scale) gap. Returns
    // the x just past the drawn text.
    void draw_char(int x, int y, char ch, int scale, uint8_t r, uint8_t g, uint8_t b);
    int draw_text(int x, int y, const std::string& s, int scale, uint8_t r, uint8_t g, uint8_t b);
};

// Pixel width of `s` rendered at `scale` (5px glyph + 1px gap per char).
int text_width(const std::string& s, int scale);

// Encode a canvas as a PNG byte stream (8-bit truecolor RGB).
std::vector<uint8_t> encode_png(const Canvas& c);

// Standard base64 (no line wrapping) — used to embed a PNG in a JSON response.
std::string base64(const std::vector<uint8_t>& data);

}  // namespace ReaClaw::Image
