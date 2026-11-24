#include "icon.h"

#include <cmath>
#include <vector>

#include "digits.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

// Solid device shapes designed on a 16x16 grid, drawn 4x supersampled and box-filtered down,
// which gives smooth anti-aliased edges using plain GDI.
class Canvas {
public:
    Canvas(int size, int ss) : size_(size), ss_(ss), n_(size * ss), u_(n_ / 16.0f) {
        BITMAPINFO bi{};
        bi.bmiHeader = { sizeof(BITMAPINFOHEADER), n_, -n_, 1, 32, BI_RGB };
        HDC screen = GetDC(nullptr);
        dc_ = CreateCompatibleDC(screen);
        bmp_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void**)&bits_, nullptr, 0);
        ReleaseDC(nullptr, screen);
        old_ = SelectObject(dc_, bmp_);
        SelectObject(dc_, GetStockObject(NULL_PEN));
        memset(bits_, 0, (size_t)n_ * n_ * 4);
    }
    ~Canvas() {
        SelectObject(dc_, old_);
        DeleteObject(bmp_);
        DeleteDC(dc_);
    }

    enum Shape { Rect, Round, Oval };
    // Paints a shape in grid units. `on` = draw (true) or cut out (false).
    void Fill(bool on, Shape s, float x0, float y0, float x1, float y1, float r = 0) {
        SelectObject(dc_, GetStockObject(on ? WHITE_BRUSH : BLACK_BRUSH));
        int l = P(x0), t = P(y0), rr = P(x1) + 1, b = P(y1) + 1;   // NULL_PEN excludes right/bottom edge
        switch (s) {
            case Rect:  Rectangle(dc_, l, t, rr, b); break;
            case Round: RoundRect(dc_, l, t, rr, b, P(2 * r), P(2 * r)); break;
            case Oval:  Ellipse(dc_, l, t, rr, b); break;
        }
    }

    std::vector<uint8_t> Downsample() {
        GdiFlush();
        std::vector<uint8_t> m((size_t)size_ * size_);
        for (int y = 0; y < size_; ++y)
            for (int x = 0; x < size_; ++x) {
                unsigned sum = 0;
                for (int dy = 0; dy < ss_; ++dy)
                    for (int dx = 0; dx < ss_; ++dx)
                        sum += (bits_[(y * ss_ + dy) * n_ + x * ss_ + dx] >> 16) & 0xFF;
                m[y * size_ + x] = (uint8_t)(sum / (ss_ * ss_));
            }
        return m;
    }

private:
    int P(float v) const { return (int)std::lround(v * u_); }
    int size_, ss_, n_;
    float u_;
    HDC dc_;
    HBITMAP bmp_;
    HGDIOBJ old_;
    uint32_t* bits_ = nullptr;
};

std::vector<uint8_t> RenderShape(Kind kind, int size) {
    Canvas c(size, 4);
    using S = Canvas::Shape;
    switch (kind) {
    case Kind::Mouse:
        c.Fill(true,  S::Round, 3, 0.5f, 12, 15.5f, 4.5f);   // body (capsule)
        c.Fill(false, S::Rect,  3, 6, 12, 7);                // button / palm split
        c.Fill(false, S::Rect,  7, 0, 8, 6);                 // left / right button split
        break;
    case Kind::Headset:
        c.Fill(true,  S::Oval,  1, 1, 15, 15);               // headband (ring, upper half)
        c.Fill(false, S::Oval,  3, 3, 13, 13);
        c.Fill(false, S::Rect,  0, 8.5f, 16, 16);
        c.Fill(true,  S::Round, 0.5f, 8, 5.5f, 15.5f, 2);    // ear cups
        c.Fill(true,  S::Round, 10.5f, 8, 15.5f, 15.5f, 2);
        break;
    case Kind::Keyboard:
        c.Fill(true,  S::Round, 0, 3, 15, 12, 2);            // body
        for (int row = 0; row < 2; ++row)                    // two rows of keys
            for (int k = 0; k < 6; ++k)
                c.Fill(false, S::Rect, 2.0f + 2 * k, 5.0f + 2 * row, 3.0f + 2 * k, 6.0f + 2 * row);
        c.Fill(false, S::Rect,  4, 9, 11, 10);               // space bar
        break;
    case Kind::Speaker:
        c.Fill(true,  S::Round, 0.5f, 4.5f, 15.5f, 11.5f, 3.5f); // cylinder seen from the front
        c.Fill(false, S::Rect,  3.2f, 4.5f, 4, 11.5f);       // end caps
        c.Fill(false, S::Rect,  12, 4.5f, 12.8f, 11.5f);
        c.Fill(false, S::Round, 6.5f, 7, 9.5f, 9, 0.5f);     // logo
        break;
    default:                                                 // battery (app / unknown device)
        c.Fill(true,  S::Round, 0.5f, 4, 14, 12, 2);
        c.Fill(false, S::Round, 2, 5.5f, 12.5f, 10.5f, 1);
        c.Fill(true,  S::Round, 3, 6.5f, 9, 9.5f, 0.5f);     // charge level
        c.Fill(true,  S::Round, 14.5f, 6, 15.5f, 10, 0.5f);  // terminal
        break;
    }
    return c.Downsample();
}

}  // namespace

bool TaskbarIsLight() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return false;
    return v != 0;
}

HICON MakeTrayIcon(Kind kind, int percent, bool charging, bool colorLevels) {
    const int size = GetSystemMetrics(SM_CXSMICON);
    const bool light = TaskbarIsLight();

    COLORREF fg = light ? RGB(0x1A, 0x1A, 0x1A) : RGB(0xFF, 0xFF, 0xFF);
    COLORREF num = fg;
    if (percent >= 0) {
        if (charging)         num = light ? RGB(0x10, 0x7C, 0x10) : RGB(0x6C, 0xCB, 0x5F);  // green
        else if (percent < 15) num = light ? RGB(0xC4, 0x2B, 0x1C) : RGB(0xFF, 0x63, 0x63);  // red
        else if (colorLevels)
            num = percent < 30 ? (light ? RGB(0xC2, 0x5E, 0x00) : RGB(0xFF, 0x9F, 0x40))     // orange
                               : (light ? RGB(0x00, 0x5F, 0xB8) : RGB(0x4C, 0xC2, 0xFF));    // blue
    }

    std::vector<uint8_t> shape = RenderShape(kind, size);
    std::vector<uint8_t> numMask((size_t)size * size, 0);

    if (percent >= 0) {
        // Small pixel-font number in the bottom-right corner, with a transparent knockout
        // around it so it stays readable on top of the shape.
        char txt[4];
        int len = sprintf_s(txt, "%d", percent > 100 ? 100 : percent);
        const int k = size >= 24 ? size / 12 : 1;            // pixel scale: 16/20px -> 1, 24/32px -> 2
        const int w = DigitsWidth(len, k), h = DIGIT_H * k;
        const int x0 = size - w, y0 = size - h;
        StampDigits(txt, len, x0, y0, k, [&](int x, int y) {
            if (x >= 0 && x < size && y >= 0 && y < size) numMask[y * size + x] = 255;
        });
        for (int y = y0 - k; y < size; ++y)
            for (int x = x0 - k; x < size; ++x)
                if (x >= 0 && y >= 0) shape[y * size + x] = 0;
    }

    // Compose premultiplied BGRA into a 32-bit DIB.
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB };
    uint32_t* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    ReleaseDC(nullptr, screen);

    auto blend = [](COLORREF c, uint8_t a) -> uint32_t {
        uint32_t r = GetRValue(c) * a / 255, g = GetGValue(c) * a / 255, b = GetBValue(c) * a / 255;
        return (uint32_t)a << 24 | r << 16 | g << 8 | b;
    };
    for (int i = 0; i < size * size; ++i)
        bits[i] = numMask[i] ? blend(num, numMask[i]) : blend(fg, shape[i]);

    std::vector<uint8_t> maskBits(((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}
