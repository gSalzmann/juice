// Dev tool: renders the taskbar illustrations (24/32/64 px) and overlay badges on dark and light
// backgrounds into a BMP, scaled up for inspection.
//   illustpreview.exe out.bmp
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "../src/illust.h"

int wmain(int argc, wchar_t** argv) {
    const int scale = 4, pad = 8;
    const Kind kinds[] = { Kind::Mouse, Kind::Headset, Kind::Keyboard, Kind::Speaker, Kind::App };
    const int sizes[] = { 24, 32, 64 };
    struct B { int p; bool c; } badges[] = { {97, false}, {22, false}, {9, false}, {100, false}, {55, true} };

    // Layout: per background, one row per size with every illustration, then a row of badges.
    int rowW = 0;
    for (int s : sizes) rowW = std::max(rowW, (int)(sizeof(kinds) / sizeof(kinds[0])) * (s + pad));
    const int H1 = (24 + pad) + (32 + pad) + (64 + pad) + (16 + pad);
    const int W = rowW * 2, H = H1;
    std::vector<uint32_t> img((size_t)W * H);

    for (int bgi = 0; bgi < 2; ++bgi) {
        const uint32_t bg = bgi == 0 ? 0xFF202020 : 0xFFF3F3F3;
        const int ox = bgi * rowW;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < rowW; ++x) img[(size_t)y * W + ox + x] = bg;

        auto blit = [&](const std::vector<uint32_t>& px, int s, int x0, int y0) {
            for (int y = 0; y < s; ++y)
                for (int x = 0; x < s; ++x) {
                    uint32_t src = px[(size_t)y * s + x], &dst = img[(size_t)(y0 + y) * W + x0 + x];
                    unsigned a = src >> 24, k = 255 - a;
                    unsigned r = ((src >> 16) & 0xFF) + ((dst >> 16) & 0xFF) * k / 255;
                    unsigned g = ((src >> 8) & 0xFF) + ((dst >> 8) & 0xFF) * k / 255;
                    unsigned b = (src & 0xFF) + (dst & 0xFF) * k / 255;
                    dst = 0xFF000000 | r << 16 | g << 8 | b;
                }
        };

        int y = pad / 2;
        for (int s : sizes) {
            for (int i = 0; i < (int)(sizeof(kinds) / sizeof(kinds[0])); ++i) {
                std::vector<uint32_t> px((size_t)s * s, 0);
                Painter p(px.data(), s, s, s, 0, 0, s / 32.0f);
                DrawIllustration(p, kinds[i]);
                blit(px, s, ox + pad / 2 + i * (s + pad), y);
            }
            y += s + pad;
        }
        for (int i = 0; i < 5; ++i) {
            HICON ic = MakeBadgeIcon(badges[i].p, badges[i].c, 16);
            ICONINFO ii;
            GetIconInfo(ic, &ii);
            std::vector<uint32_t> px(16 * 16);
            BITMAPINFO bi{};
            bi.bmiHeader = { sizeof(BITMAPINFOHEADER), 16, -16, 1, 32, BI_RGB };
            HDC dc = GetDC(nullptr);
            GetDIBits(dc, ii.hbmColor, 0, 16, px.data(), &bi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, dc);
            DeleteObject(ii.hbmColor);
            DeleteObject(ii.hbmMask);
            DestroyIcon(ic);
            blit(px, 16, ox + pad / 2 + i * (16 + pad), y);
        }
    }

    // Write scaled 24-bit BMP (bottom-up).
    const int SW = W * scale, SH = H * scale, stride = (SW * 3 + 3) & ~3;
    std::vector<uint8_t> out((size_t)stride * SH);
    for (int y = 0; y < SH; ++y)
        for (int x = 0; x < SW; ++x) {
            uint32_t c = img[(size_t)(y / scale) * W + x / scale];
            uint8_t* o = &out[(size_t)(SH - 1 - y) * stride + x * 3];
            o[0] = c & 0xFF; o[1] = (c >> 8) & 0xFF; o[2] = (c >> 16) & 0xFF;
        }
    BITMAPINFOHEADER bh{ sizeof(bh), SW, SH, 1, 24, BI_RGB };
    BITMAPFILEHEADER fh{ 0x4D42, (DWORD)(sizeof(fh) + sizeof(bh) + out.size()), 0, 0, sizeof(fh) + sizeof(bh) };
    FILE* f = nullptr;
    if (_wfopen_s(&f, argc > 1 ? argv[1] : L"illust.bmp", L"wb") || !f) return 1;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&bh, sizeof(bh), 1, f);
    fwrite(out.data(), out.size(), 1, f);
    fclose(f);
    return 0;
}
