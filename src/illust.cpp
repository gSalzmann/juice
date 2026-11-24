#include "illust.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "digits.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// ---------------------------------------------------------------------------------------------
// Renderer

void Painter::Fill(const Sdf& d, Rgba c0, Rgba c1, float gx0, float gy0, float gx1, float gy1, float soft) {
    const float edge = std::max(1.0f, soft * s_);          // anti-aliasing width in pixels
    const float gdx = gx1 - gx0, gdy = gy1 - gy0;
    const float glen2 = std::max(1e-6f, gdx * gdx + gdy * gdy);
    for (int py = 0; py < h_; ++py) {
        const float v = (py + 0.5f - oy_) / s_;
        uint32_t* row = px_ + (size_t)py * stride_;
        for (int px = 0; px < w_; ++px) {
            const float u = (px + 0.5f - ox_) / s_;
            const float dist = d(u, v) * s_;
            float cov = 0.5f - dist / edge;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            float t = ((u - gx0) * gdx + (v - gy0) * gdy) / glen2;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            const float a = (c0.a + (c1.a - c0.a) * t) * cov;
            const float r = (c0.r + (c1.r - c0.r) * t) * a;
            const float g = (c0.g + (c1.g - c0.g) * t) * a;
            const float b = (c0.b + (c1.b - c0.b) * t) * a;
            const uint32_t dst = row[px];
            const float k = 1 - a;
            const float da = ((dst >> 24) & 0xFF) * k + a * 255;
            const float dr = ((dst >> 16) & 0xFF) * k + r * 255;
            const float dg = ((dst >> 8) & 0xFF) * k + g * 255;
            const float db = (dst & 0xFF) * k + b * 255;
            row[px] = (uint32_t)(da + 0.5f) << 24 | (uint32_t)(dr + 0.5f) << 16 |
                      (uint32_t)(dg + 0.5f) << 8 | (uint32_t)(db + 0.5f);
        }
    }
}

namespace sdf {

float Circle(float x, float y, float cx, float cy, float r) {
    return std::hypot(x - cx, y - cy) - r;
}

float Box(float x, float y, float cx, float cy, float hw, float hh, float r) {
    const float qx = std::fabs(x - cx) - hw + r, qy = std::fabs(y - cy) - hh + r;
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}

float Segment(float x, float y, float ax, float ay, float bx, float by, float r) {
    const float pax = x - ax, pay = y - ay, bax = bx - ax, bay = by - ay;
    float h = (pax * bax + pay * bay) / (bax * bax + bay * bay);
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    return std::hypot(pax - bax * h, pay - bay * h) - r;
}

float Polygon(float x, float y, const float* v, int n) {
    float d = (x - v[0]) * (x - v[0]) + (y - v[1]) * (y - v[1]);
    float s = 1;
    for (int i = 0, j = n - 1; i < n; j = i, ++i) {
        const float ex = v[2 * j] - v[2 * i], ey = v[2 * j + 1] - v[2 * i + 1];
        const float wx = x - v[2 * i], wy = y - v[2 * i + 1];
        float t = (wx * ex + wy * ey) / (ex * ex + ey * ey);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const float bx = wx - ex * t, by = wy - ey * t;
        d = std::min(d, bx * bx + by * by);
        const bool c1 = y >= v[2 * i + 1], c2 = y < v[2 * j + 1], c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * std::sqrt(d);
}

}  // namespace sdf

// ---------------------------------------------------------------------------------------------
// Illustrations (32x32 design grid). Palette: light "aluminium" bodies with a graphite rim so
// they read on both dark and light taskbars, graphite details and a Windows-blue accent.

namespace {

using namespace sdf;

const Rgba kShadow    = Hex(0x000000, 0.35f);
const Rgba kBodyTop   = Hex(0xF5F7FA), kBodyBot   = Hex(0xBFC6D1);
const Rgba kRimTop    = Hex(0x7D8693), kRimBot    = Hex(0x3E444D);
const Rgba kDarkTop   = Hex(0x5C6470), kDarkBot   = Hex(0x2C3138);
const Rgba kAccentTop = Hex(0x5CCBFF), kAccentBot = Hex(0x0A6CC8);
const Rgba kLine      = Hex(0x8A93A0);

// Bolt shape centered at (cx, cy) with height h.
float Bolt(float x, float y, float cx, float cy, float h) {
    const float k = h / 12.0f;
    const float pts[] = { cx + 1.5f * k, cy - 6 * k, cx - 3.6f * k, cy + 1.0f * k, cx - 0.3f * k, cy + 1.0f * k,
                          cx - 1.5f * k, cy + 6 * k, cx + 3.6f * k, cy - 1.0f * k, cx + 0.3f * k, cy - 1.0f * k };
    return Polygon(x, y, pts, 6);
}

void DrawMouse(Painter& p) {
    p.Fill([](float x, float y) { return Box(x, y, 16, 29.6f, 6.5f, 1.1f, 1.1f); }, kShadow, 2.2f);
    p.Fill([](float x, float y) { return Box(x, y, 16, 16, 9.4f, 14.2f, 9.4f); }, kRimTop, kRimBot, 0, 2, 0, 30);
    auto body = [](float x, float y) { return Box(x, y, 16, 16, 8.4f, 13.2f, 8.4f); };
    p.Fill(body, kBodyTop, kBodyBot, 0, 3, 0, 29);
    // button split (horizontal) and left/right split (vertical)
    p.Fill([=](float x, float y) { return std::max(body(x, y), std::fabs(y - 13.0f) - 0.45f); }, kLine);
    p.Fill([=](float x, float y) { return std::max(body(x, y), std::max(std::fabs(x - 16) - 0.45f, y - 13.0f)); }, kLine);
    // scroll wheel
    p.Fill([](float x, float y) { return Box(x, y, 16, 8.6f, 1.9f, 3.4f, 1.9f); }, kDarkTop, kDarkBot, 0, 5, 0, 12);
    p.Fill([](float x, float y) { return Box(x, y, 16, 8.6f, 0.9f, 2.4f, 0.9f); }, kAccentTop, kAccentBot, 0, 6, 0, 11);
    // gloss on the left side
    p.Fill([](float x, float y) { return Segment(x, y, 10.6f, 9.5f, 10.6f, 20.5f, 0.9f); }, Hex(0xFFFFFF, 0.7f));
}

void DrawHeadset(Painter& p) {
    p.Fill([](float x, float y) { return Box(x, y, 16, 29.8f, 12.5f, 1.0f, 1.0f); }, kShadow, 2.0f);
    // headband: upper half of a thick ring, with an accent line on top
    auto ring = [](float x, float y, float t) {
        return std::max(std::fabs(Circle(x, y, 16, 18, 11.6f)) - t, y - 19.0f);
    };
    p.Fill([=](float x, float y) { return ring(x, y, 2.1f); }, kRimTop, kRimBot, 0, 4, 0, 19);
    p.Fill([=](float x, float y) { return std::max(ring(x, y, 1.2f), Circle(x, y, 16, 18, 11.6f)); },
           kBodyTop, kBodyBot, 0, 5, 0, 19);
    p.Fill([](float x, float y) { return std::max(std::fabs(Circle(x, y, 16, 18, 12.9f)) - 0.45f, y - 12.0f); },
           kAccentTop, kAccentBot, 0, 5, 0, 12);
    // ear cups: graphite shell, light cushion facing inward, blue accent outside
    for (int side = -1; side <= 1; side += 2) {
        const float cx = 16 + side * 9.6f;
        p.Fill([=](float x, float y) { return Box(x, y, cx, 21.5f, 4.8f, 7.4f, 4.2f); }, kRimTop, kRimBot, 0, 14, 0, 29);
        p.Fill([=](float x, float y) { return Box(x, y, cx, 21.5f, 3.8f, 6.4f, 3.4f); }, kDarkTop, kDarkBot, 0, 15, 0, 28);
        p.Fill([=](float x, float y) { return Box(x, y, cx - side * 1.9f, 21.5f, 1.9f, 5.6f, 1.9f); },
               kBodyTop, kBodyBot, 0, 16, 0, 27);
        p.Fill([=](float x, float y) { return Box(x, y, cx + side * 2.0f, 21.5f, 0.7f, 3.0f, 0.7f); },
               kAccentTop, kAccentBot, 0, 18, 0, 25);
    }
}

void DrawKeyboard(Painter& p) {
    p.Fill([](float x, float y) { return Box(x, y, 16, 27.4f, 13.0f, 1.0f, 1.0f); }, kShadow, 2.0f);
    p.Fill([](float x, float y) { return Box(x, y, 16, 17, 15.4f, 9.6f, 3.6f); }, kRimTop, kRimBot, 0, 7, 0, 27);
    p.Fill([](float x, float y) { return Box(x, y, 16, 17, 14.4f, 8.6f, 2.8f); }, kBodyTop, kBodyBot, 0, 8, 0, 26);
    auto key = [&](float cx, float cy, float hw, bool accent) {
        p.Fill([=](float x, float y) { return Box(x, y, cx, cy, hw, 1.45f, 0.7f); },
               accent ? kAccentTop : kDarkTop, accent ? kAccentBot : kDarkBot, 0, cy - 1.5f, 0, cy + 1.5f);
    };
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < 6; ++i)
            key(6.4f + 3.84f * i, 12.6f + 4.1f * row, 1.45f, row == 1 && i == 5);
    key(6.4f, 20.8f, 1.45f, false);
    key(16, 20.8f, 6.8f, false);       // space bar
    key(25.6f, 20.8f, 1.45f, false);
}

void DrawSpeaker(Painter& p) {
    // Portable cylinder speaker lying down: fabric body, rubber end caps, small badge in the middle.
    p.Fill([](float x, float y) { return Box(x, y, 16, 27.6f, 12.5f, 1.0f, 1.0f); }, kShadow, 2.0f);
    p.Fill([](float x, float y) { return Box(x, y, 16, 17, 15.2f, 8.6f, 8.6f); }, kRimTop, kRimBot, 0, 8, 0, 26);
    auto body = [](float x, float y) { return Box(x, y, 16, 17, 14.2f, 7.6f, 7.6f); };
    p.Fill(body, kDarkTop, kDarkBot, 0, 9, 0, 25);
    for (int i = 0; i < 5; ++i) {                          // fabric weave
        const float ly = 12.6f + 2.2f * i;
        p.Fill([=](float x, float y) { return std::max(body(x, y), std::fabs(y - ly) - 0.35f); }, Hex(0xFFFFFF, 0.10f));
    }
    for (int side = -1; side <= 1; side += 2) {           // end caps (passive radiators)
        const float cx = 16 + side * 12.4f;
        p.Fill([=](float x, float y) { return Box(x, y, cx, 17, 2.0f, 6.6f, 2.0f); }, kBodyTop, kBodyBot, 0, 10, 0, 24);
    }
    p.Fill([](float x, float y) { return Box(x, y, 16, 17, 3.4f, 1.7f, 1.2f); }, kAccentTop, kAccentBot, 0, 15, 0, 19);
    p.Fill([](float x, float y) { return Segment(x, y, 8.5f, 10.6f, 23.5f, 10.6f, 0.6f); }, Hex(0xFFFFFF, 0.35f));
}

void DrawBattery(Painter& p) {
    p.Fill([](float x, float y) { return Box(x, y, 15, 26.4f, 11.0f, 1.0f, 1.0f); }, kShadow, 2.0f);
    p.Fill([](float x, float y) { return Box(x, y, 14.6f, 16, 12.8f, 8.4f, 3.4f); }, kRimTop, kRimBot, 0, 7, 0, 25);
    p.Fill([](float x, float y) { return Box(x, y, 14.6f, 16, 11.4f, 7.0f, 2.4f); }, Hex(0x2A2F36), Hex(0x1C2026), 0, 9, 0, 23);
    p.Fill([](float x, float y) { return Box(x, y, 10.6f, 16, 6.6f, 5.4f, 1.6f); }, Hex(0x7EE07A), Hex(0x23A03A), 0, 10, 0, 22);
    p.Fill([](float x, float y) { return Box(x, y, 29.2f, 16, 1.6f, 3.6f, 1.2f); }, kRimTop, kRimBot, 0, 12, 0, 20);
    p.Fill([](float x, float y) { return Bolt(x, y, 16.5f, 16, 12); }, Hex(0xFFFFFF));
}

HICON IconFromPixels(const std::vector<uint32_t>& px, int size) {
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB };
    uint32_t* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    memcpy(bits, px.data(), px.size() * 4);
    std::vector<uint8_t> maskBits(((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

}  // namespace

void DrawIllustration(Painter& p, Kind kind) {
    switch (kind) {
        case Kind::Mouse:    DrawMouse(p); break;
        case Kind::Headset:  DrawHeadset(p); break;
        case Kind::Keyboard: DrawKeyboard(p); break;
        case Kind::Speaker:  DrawSpeaker(p); break;
        default:             DrawBattery(p); break;
    }
}

uint32_t LevelColor(int percent, bool charging) {
    if (charging)      return 0x2EA043;   // green
    if (percent < 15)  return 0xD13438;   // red
    if (percent < 30)  return 0xE3A21A;   // amber
    return 0x0078D4;                      // blue (matches the taskbar's normal progress state)
}

HICON MakeIllustrationIcon(Kind kind, int size) {
    std::vector<uint32_t> px((size_t)size * size, 0);
    Painter p(px.data(), size, size, size, 0, 0, size / 32.0f);
    DrawIllustration(p, kind);
    return IconFromPixels(px, size);
}

HICON MakeBadgeIcon(int percent, bool charging, int size) {
    std::vector<uint32_t> px((size_t)size * size, 0);
    Painter p(px.data(), size, size, size, 0, 0, size / 16.0f);
    const uint32_t bg = LevelColor(percent, charging);
    // Dark ring around the badge so it separates from the illustration underneath.
    p.Fill([](float x, float y) { return Box(x, y, 8, 8, 8, 8, 5); }, Hex(0x1C1C1C, 0.9f));
    p.Fill([](float x, float y) { return Box(x, y, 8, 8, 7, 7, 4.2f); }, Hex(bg));

    const bool darkText = !charging && percent >= 15 && percent < 30;   // amber needs dark text
    if (charging) {
        p.Fill([](float x, float y) { return Bolt(x, y, 8, 8, 11); }, Hex(0xFFFFFF));
    } else {
        char txt[4];
        int len = sprintf_s(txt, "%d", percent > 100 ? 100 : percent < 0 ? 0 : percent);
        const int k = std::max(1, size / 16);
        const int w = DigitsWidth(len, k), h = DIGIT_H * k;
        const int x0 = (size - w) / 2, y0 = (size - h) / 2;
        const uint32_t ink = darkText ? 0xFF1A1A1A : 0xFFFFFFFF;
        StampDigits(txt, len, x0, y0, k, [&](int x, int y) {
            if (x >= 0 && x < size && y >= 0 && y < size) px[(size_t)y * size + x] = ink;
        });
    }
    return IconFromPixels(px, size);
}
