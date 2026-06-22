#pragma once
#include <string>

#include <reaper_plugin_functions.h>

namespace ReaClaw::Handlers {

// Parse "#RRGGBB" into a REAPER native color with the custom-color flag set.
// Returns -1 if the string is not a valid 7-char hex color.
inline int parse_hex_color(const std::string& s) {
    if (s.size() != 7 || s[0] != '#')
        return -1;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; i++) {
        v[i] = nib(s[i + 1]);
        if (v[i] < 0)
            return -1;
    }
    int r = v[0] * 16 + v[1], g = v[2] * 16 + v[3], b = v[4] * 16 + v[5];
    return ColorToNative(r, g, b) | 0x1000000;
}

// Convert a REAPER native color (with the custom-color flag) to "#RRGGBB",
// or "" if no custom color is set (the value lacks the 0x1000000 flag).
inline std::string native_color_to_hex(int native) {
    if (!(native & 0x1000000))
        return "";
    int r = 0, g = 0, b = 0;
    ColorFromNative(native & 0xFFFFFF, &r, &g, &b);
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", r & 0xFF, g & 0xFF, b & 0xFF);
    return hex;
}

}  // namespace ReaClaw::Handlers
