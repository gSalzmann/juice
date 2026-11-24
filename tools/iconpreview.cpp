// Dev tool: renders sample tray icons on the taskbar background color, scaled 8x, into a BMP.
//   iconpreview.exe out.bmp
#include <windows.h>
#include <cstdio>
#include "../src/icon.h"

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const int s = GetSystemMetrics(SM_CXSMICON), scale = 8, pad = 4;
    struct Sample { Kind k; int p; bool c; } items[] = {
        {Kind::Mouse, 97, false},   {Kind::Headset, 37, false}, {Kind::Headset, 22, false},
        {Kind::Keyboard, 12, false}, {Kind::Mouse, 55, true},   {Kind::App, -1, false}};
    const int n = sizeof(items) / sizeof(items[0]);
    const int W = n * (s + pad), H = s + pad;

    HDC screen = GetDC(nullptr);
    HDC dcSrc = CreateCompatibleDC(screen);
    HBITMAP bmpSrc = CreateCompatibleBitmap(screen, W, H);
    SelectObject(dcSrc, bmpSrc);
    RECT r{0, 0, W, H};
    HBRUSH bg = CreateSolidBrush(TaskbarIsLight() ? RGB(243, 243, 243) : RGB(32, 32, 32));
    FillRect(dcSrc, &r, bg);
    for (int i = 0; i < n; ++i) {
        HICON ic = MakeTrayIcon(items[i].k, items[i].p, items[i].c, true);
        DrawIconEx(dcSrc, i * (s + pad) + pad / 2, pad / 2, ic, s, s, 0, nullptr, DI_NORMAL);
        DestroyIcon(ic);
    }

    HDC dcDst = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), W * scale, H * scale, 1, 24, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmpDst = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(dcDst, bmpDst);
    StretchBlt(dcDst, 0, 0, W * scale, H * scale, dcSrc, 0, 0, W, H, SRCCOPY);
    GdiFlush();

    const int stride = (W * scale * 3 + 3) & ~3, size = stride * H * scale;
    BITMAPFILEHEADER fh{0x4D42, (DWORD)(sizeof(fh) + sizeof(BITMAPINFOHEADER) + size), 0, 0,
                        sizeof(fh) + sizeof(BITMAPINFOHEADER)};
    FILE* f = nullptr;
    if (_wfopen_s(&f, argc > 1 ? argv[1] : L"icons.bmp", L"wb") || !f) return 1;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
    fwrite(bits, size, 1, f);
    fclose(f);
    wprintf(L"icon size %d px\n", s);
    return 0;
}
