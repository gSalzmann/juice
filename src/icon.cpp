#include "icon.h"

#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

// 3x5 pixel digits (bit 2 = left column). Crisp at 16px where TrueType text would blur.
const uint8_t kDigits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

wchar_t GlyphFor(Kind k) {
    // Codepoints shared by "Segoe Fluent Icons" (Win11) and "Segoe MDL2 Assets" (Win10).
    switch (k) {
        case Kind::Mouse:    return 0xE962;
        case Kind::Keyboard: return 0xE765;
        case Kind::Headset:  return 0xE7F6;
        case Kind::App:      return 0xE83F;  // battery
        default:             return 0xE702;  // bluetooth
    }
}

const wchar_t* IconFontName() {
    static const wchar_t* name = nullptr;
    if (!name) {
        HDC dc = GetDC(nullptr);
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
        bool found = false;
        EnumFontFamiliesExW(dc, &lf, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
            *(bool*)p = true; return 0; }, (LPARAM)&found, 0);
        ReleaseDC(nullptr, dc);
        name = found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }
    return name;
}

// Renders white text on black into `bits` and returns the coverage mask (0..255 per pixel).
void RenderMask(HDC dc, uint32_t* bits, int size, const wchar_t* text, int len, HFONT font, RECT rc,
                std::vector<uint8_t>& mask) {
    memset(bits, 0, size * size * 4);
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, text, len, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    SelectObject(dc, old);
    GdiFlush();
    mask.resize(size * size);
    for (int i = 0; i < size * size; ++i) {
        uint32_t p = bits[i];
        uint8_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        mask[i] = r > g ? (r > b ? r : b) : (g > b ? g : b);
    }
}

HFONT MakeFont(const wchar_t* face, int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
}

}  // namespace

bool TaskbarIsLight() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return false;
    return v != 0;
}

HICON MakeTrayIcon(Kind kind, int percent, bool charging, int lowThreshold) {
    const int size = GetSystemMetrics(SM_CXSMICON);
    const bool light = TaskbarIsLight();

    COLORREF fg = light ? RGB(0x1A, 0x1A, 0x1A) : RGB(0xFF, 0xFF, 0xFF);
    COLORREF num = fg;
    if (percent >= 0) {
        if (charging)                   num = light ? RGB(0x10, 0x7C, 0x10) : RGB(0x6C, 0xCB, 0x5F);
        else if (percent <= lowThreshold) num = light ? RGB(0xC4, 0x2B, 0x1C) : RGB(0xFF, 0x63, 0x63);
        else if (percent < 30)          num = light ? RGB(0x9D, 0x5D, 0x00) : RGB(0xFF, 0xC8, 0x3D);
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    void* raw = nullptr;
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
    ReleaseDC(nullptr, screen);
    HGDIOBJ oldBmp = SelectObject(dc, color);
    uint32_t* bits = (uint32_t*)raw;

    std::vector<uint8_t> glyphMask, numMask(size * size, 0);
    wchar_t glyph = GlyphFor(kind);

    // Full-size glyph, like the system tray icons (network, volume...).
    HFONT f = MakeFont(IconFontName(), size, FW_NORMAL);
    RenderMask(dc, bits, size, &glyph, 1, f, RECT{ 0, 0, size, size }, glyphMask);
    DeleteObject(f);

    if (percent >= 0) {
        // Small pixel-font number in the bottom-right corner, with a transparent knockout
        // around it so it stays readable on top of the glyph.
        char txt[4];
        int len = sprintf_s(txt, "%d", percent > 100 ? 100 : percent);
        const int k = size >= 24 ? size / 12 : 1;            // pixel scale: 16/20px -> 1, 24px -> 2, 32px -> 2
        const int w = (len * 4 - 1) * k, h = 5 * k;
        const int x0 = size - w, y0 = size - h;
        for (int c = 0; c < len; ++c) {
            const uint8_t* rows = kDigits[txt[c] - '0'];
            for (int ry = 0; ry < 5; ++ry)
                for (int rx = 0; rx < 3; ++rx)
                    if (rows[ry] & (4 >> rx))
                        for (int dy = 0; dy < k; ++dy)
                            for (int dx = 0; dx < k; ++dx) {
                                int x = x0 + (c * 4 + rx) * k + dx, y = y0 + ry * k + dy;
                                if (x >= 0 && x < size && y >= 0 && y < size) numMask[y * size + x] = 255;
                            }
        }
        // Knockout: clear the glyph in a k-pixel ring around the digits' bounding box.
        for (int y = y0 - k; y < size; ++y)
            for (int x = x0 - k; x < size; ++x)
                if (x >= 0 && y >= 0) glyphMask[y * size + x] = 0;
    }

    // Compose premultiplied BGRA.
    auto blend = [](COLORREF c, uint8_t a) -> uint32_t {
        uint32_t r = GetRValue(c) * a / 255, g = GetGValue(c) * a / 255, b = GetBValue(c) * a / 255;
        return (uint32_t)a << 24 | r << 16 | g << 8 | b;
    };
    for (int i = 0; i < size * size; ++i) {
        uint8_t na = numMask[i], ga = glyphMask[i];
        bits[i] = na >= ga ? blend(num, na) : blend(fg, ga);
    }
    GdiFlush();
    SelectObject(dc, oldBmp);
    DeleteDC(dc);

    std::vector<uint8_t> maskBits(((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}
