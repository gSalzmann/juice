// Tiny anti-aliased vector renderer based on signed distance fields (SDF), used for the
// detailed device illustrations, the taskbar overlay badge and the rounded bars of the window.
#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>

#include "icon.h"   // Kind

struct Rgba { float r, g, b, a; };
inline Rgba Hex(uint32_t rgb, float a = 1.0f) {
    return { ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a };
}

// Paints into a premultiplied BGRA buffer. Shapes are given in "design units": design point (u, v)
// maps to pixel (ox + u * scale, oy + v * scale). Only pixels inside [0,w)x[0,h) are touched.
class Painter {
public:
    using Sdf = std::function<float(float, float)>;   // distance in design units, < 0 inside

    Painter(uint32_t* px, int stride, int w, int h, float ox = 0, float oy = 0, float scale = 1)
        : px_(px), stride_(stride), w_(w), h_(h), ox_(ox), oy_(oy), s_(scale) {}

    // Solid fill. `soft` widens the edge (in design units) for shadows / glows.
    void Fill(const Sdf& d, Rgba c, float soft = 0) { Fill(d, c, c, 0, 0, 0, 1, soft); }
    // Linear gradient from (gx0, gy0) with c0 to (gx1, gy1) with c1.
    void Fill(const Sdf& d, Rgba c0, Rgba c1, float gx0, float gy0, float gx1, float gy1, float soft = 0);

private:
    uint32_t* px_;
    int stride_, w_, h_;
    float ox_, oy_, s_;
};

namespace sdf {
float Circle(float x, float y, float cx, float cy, float r);
float Box(float x, float y, float cx, float cy, float hw, float hh, float r);   // rounded box
float Segment(float x, float y, float ax, float ay, float bx, float by, float r);  // capsule
float Polygon(float x, float y, const float* xy, int n);                         // n points
}  // namespace sdf

// Detailed device illustration on a 32x32 design grid.
void DrawIllustration(Painter& p, Kind kind);

// Battery level colors of the taskbar app: blue >= 30, amber 15..29, red < 15, green when charging.
uint32_t LevelColor(int percent, bool charging);

// Icons (caller owns the HICON).
HICON MakeIllustrationIcon(Kind kind, int size);
HICON MakeBadgeIcon(int percent, bool charging, int size);   // overlay for the taskbar button
