// Juice: a regular taskbar button whose progress bar shows the battery level of a wireless
// device (green / yellow / red), with an overlay badge (% or charging bolt) and a compact window
// listing every device. Closing the window sends the app to the notification area (tray icon with
// the battery level); "Sair" in the tray menu exits.
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <dbt.h>

#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "devices.h"
#include "illust.h"
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {

constexpr UINT WM_APP_SCANDONE = WM_APP + 1;
constexpr UINT WM_APP_NOTIFY   = WM_APP + 2;
constexpr UINT_PTR TIMER_POLL = 1, TIMER_SOON = 2, TIMER_CLOCK = 3, TIMER_NOTIFY_END = 4, TIMER_ROTATE = 5;
const wchar_t* CLASS_NAME     = L"JuiceWnd";
const wchar_t* AUTOSTART_NAME = L"Juice";
const wchar_t* APP_ID         = L"Juice.App";

// Layout in DIPs.
constexpr int WIN_W = 340, PAD = 16, TOP_PAD = 8, ROW_H = 72, ILL = 40, FOOT_H = 48, EMPTY_H = 72;

HWND   g_hwnd;
Config g_cfg;
DeviceManager g_dm(g_cfg);
ITaskbarList3* g_taskbar = nullptr;
UINT   g_msgButtonCreated, g_msgTaskbarCreated;
HICON  g_badge = nullptr, g_iconBig = nullptr, g_iconSmall = nullptr;
Kind   g_iconKind = Kind::Other;
bool   g_iconValid = false;
HWND   g_settingsDlg = nullptr;
bool   g_notifyShown = false;
HICON  g_notifyIcon = nullptr;

enum Hover { HOVER_NONE, HOVER_REFRESH, HOVER_SETTINGS, HOVER_REMOVE };
int  g_hover = HOVER_NONE;
int  g_hoverRow = -1;   // row under the mouse (disconnected rows show a remove button)
RECT g_btnRefresh{}, g_btnSettings{};

// ---------------------------------------------------------------------------------------------
// Helpers

// UI language follows Windows: Portuguese when the Windows display language is Portuguese, English
// otherwise. "--lang pt|en" overrides it (for testing / screenshots).
bool g_en = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_PORTUGUESE;
const wchar_t* T(const wchar_t* pt, const wchar_t* en) { return g_en ? en : pt; }

bool AppsUseLightTheme() {
    DWORD v = 1, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v != 0;
}

struct Theme {
    uint32_t bg, hover, text, sub, track, sep;
};
Theme CurrentTheme() {
    if (AppsUseLightTheme()) return { 0xF3F3F3, 0xE6E6E6, 0x1A1A1A, 0x5F5F5F, 0xD9D9D9, 0xE0E0E0 };
    return { 0x202020, 0x2D2D2D, 0xFFFFFF, 0xA8A8A8, 0x3D3D3D, 0x333333 };
}

int Dpi() { return g_hwnd ? (int)GetDpiForWindow(g_hwnd) : 96; }
int S(int v) { return MulDiv(v, Dpi(), 96); }

COLORREF Cref(uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }

std::wstring AgoMinutes(long long min) {
    if (min < 1) return T(L"agora", L"just now");
    std::wstring n = min < 60        ? std::to_wstring(min) + L" min"
                   : min < 48 * 60   ? std::to_wstring(min / 60) + L" h"
                   : std::to_wstring(min / (24 * 60)) + (g_en ? L" days" : L" dias");
    return g_en ? n + L" ago" : L"há " + n;
}
// "~5 h restantes" / "~2 dias restantes".
std::wstring TimeLeftText(long long seconds) {
    const long long h = (seconds + 1800) / 3600;
    std::wstring n = h < 1 ? std::wstring(L"<1 h") : h < 48 ? L"~" + std::to_wstring(h) + L" h"
                   : L"~" + std::to_wstring((h + 12) / 24) + (g_en ? L" days" : L" dias");
    return n + T(L" restantes", L" left");
}

std::wstring Ago(ULONGLONG tick) { return AgoMinutes((long long)((GetTickCount64() - tick) / 60000)); }

long long UnixNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (long long)((((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ull) - 11644473600ll;
}

// Rows displayed in the window: present devices first, then known but disconnected ones.
struct Row {
    std::wstring key, name;
    Kind kind = Kind::Other;
    const Device* dev = nullptr;   // null when the device was never seen this session
};

std::vector<Row> VisibleRows() {
    std::vector<Row> rows;
    for (auto& d : g_dm.Devices())
        if (d.present && d.percent >= 0 && !g_dm.IsHidden(d.key)) rows.push_back({ d.key, d.Label(), d.kind, &d });
    for (auto& d : g_dm.Devices())
        if (!(d.present && d.percent >= 0) && !d.name.empty() && !g_dm.IsHidden(d.key))
            rows.push_back({ d.key, d.Label(), d.kind, &d });
    for (auto& [k, n] : g_cfg.known) {
        if (g_dm.Find(k) || g_dm.IsHidden(k)) continue;
        rows.push_back({ k, n, g_dm.KnownKind(k), nullptr });
    }
    return rows;
}

size_t g_rotateIndex = 0;   // "Todos": position in the rotation, advanced every minute

const Device* SelectedDevice() {
    const Device* best = nullptr;
    if (g_cfg.barDevice == L"*") {   // rotate between the connected, visible devices
        std::vector<const Device*> live;
        for (auto& d : g_dm.Devices())
            if (d.present && d.percent >= 0 && !g_dm.IsHidden(d.key)) live.push_back(&d);
        return live.empty() ? nullptr : live[g_rotateIndex % live.size()];
    }
    for (auto& d : g_dm.Devices()) {
        if (!d.present || d.percent < 0 || g_dm.IsHidden(d.key)) continue;
        if (!g_cfg.barDevice.empty()) {
            if (d.key == g_cfg.barDevice) return &d;
            continue;
        }
        // Automatic: lowest battery, preferring devices that are not charging.
        if (!best || (best->charging && !d.charging) ||
            (best->charging == d.charging && d.percent < best->percent))
            best = &d;
    }
    if (!best && !g_cfg.barDevice.empty()) {   // chosen device offline: fall back to automatic
        std::wstring saved = g_cfg.barDevice;
        g_cfg.barDevice.clear();
        best = SelectedDevice();
        g_cfg.barDevice = saved;
    }
    return best;
}

// ---------------------------------------------------------------------------------------------
// Demo mode (--demo N[c]): overrides the level of the device on the taskbar to test the colors.
// Up/Down change the level by 5, C toggles charging.

int  g_demoPercent = -1;   // -1 = off
bool g_demoCharging = false;

void ApplyDemo() {
    if (g_demoPercent < 0) return;
    Device* d = const_cast<Device*>(SelectedDevice());
    if (!d) {   // nothing connected: use a synthetic device
        d = g_dm.Find(L"demo");
        if (!d) {
            g_dm.Devices().push_back(Device{});
            d = &g_dm.Devices().back();
            d->key = L"demo";
            d->name = T(L"Demonstração", L"Demo");
            d->kind = Kind::Headset;
        }
    }
    d->present = true;
    d->asleep = false;
    d->lastGood = GetTickCount64();
    d->percent = g_demoPercent;
    d->charging = g_demoCharging;
}

// ---------------------------------------------------------------------------------------------
// Tray mode: closing the window hides it and shows a battery icon in the notification area
// (the device on the taskbar, drawn like the classic tray icons). Right-click for the menu.

constexpr UINT WM_APP_TRAY = WM_APP + 3, WM_APP_SHOW = WM_APP + 4, WM_APP_PADS = WM_APP + 5;
constexpr UINT TRAY_ID = 2;
bool  g_inTray = false, g_trayShown = false;
HICON g_trayIcon = nullptr;

std::wstring TrayTip() {
    std::wstring tip;
    for (auto& d : g_dm.Devices()) {
        if (!d.present || d.percent < 0 || g_dm.IsHidden(d.key)) continue;
        if (!tip.empty()) tip += L"\n";
        tip += d.Label() + L": " + std::to_wstring(d.percent) + L"%" + (d.charging ? T(L" (carregando)", L" (charging)") : L"");
    }
    if (tip.empty()) tip = g_dm.Scanning() ? T(L"Juice — procurando dispositivos...", L"Juice — searching for devices...")
                                           : T(L"Juice — nenhum dispositivo", L"Juice — no devices");
    return tip;
}

void FillTrayNid(NOTIFYICONDATAW& nid) {
    nid = NOTIFYICONDATAW{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    nid.uCallbackMessage = WM_APP_TRAY;
}

void UpdateTray() {
    if (!g_inTray) return;
    const Device* d = SelectedDevice();
    HICON icon = d ? MakeTrayIcon(d->kind, d->percent, d->charging, g_cfg.colorLevels)
                   : MakeTrayIcon(Kind::App, -1, false, false);
    NOTIFYICONDATAW nid;
    FillTrayNid(nid);
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = icon;
    wcsncpy_s(nid.szTip, TrayTip().c_str(), _TRUNCATE);
    if (!g_trayShown) {
        if (Shell_NotifyIconW(NIM_ADD, &nid)) {
            nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &nid);
            g_trayShown = true;
        }
    } else {
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    if (g_trayIcon) DestroyIcon(g_trayIcon);
    g_trayIcon = icon;
}

void RemoveTray() {
    if (!g_trayShown) return;
    NOTIFYICONDATAW nid;
    FillTrayNid(nid);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayShown = false;
}

void GoToTray() {
    ShowWindow(g_hwnd, SW_HIDE);
    g_inTray = true;
    UpdateTray();
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

void RestoreFromTray() {
    g_inTray = false;
    RemoveTray();
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

// ---------------------------------------------------------------------------------------------
// Taskbar button

void SetWindowIcons(Kind kind) {
    if (g_iconValid && g_iconKind == kind) return;
    HICON iconBig = MakeIllustrationIcon(kind, GetSystemMetricsForDpi(SM_CXICON, Dpi()));
    HICON iconSmall = MakeIllustrationIcon(kind, GetSystemMetricsForDpi(SM_CXSMICON, Dpi()));
    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconBig);
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
    if (g_iconBig) DestroyIcon(g_iconBig);
    if (g_iconSmall) DestroyIcon(g_iconSmall);
    g_iconBig = iconBig;
    g_iconSmall = iconSmall;
    g_iconKind = kind;
    g_iconValid = true;
}

void UpdateTaskbar() {
    UpdateTray();
    const Device* d = SelectedDevice();
    std::wstring title;
    if (d) {
        title = d->Label() + L" — " + std::to_wstring(d->percent) + L"%" + (d->charging ? T(L" · carregando", L" · charging") : L"") +
                (g_demoPercent >= 0 ? L" (demo)" : L"");
        SetWindowIcons(d->kind);
    } else {
        title = g_dm.Scanning() ? T(L"Juice — procurando dispositivos...", L"Juice — searching for devices...")
                                : T(L"Juice — nenhum dispositivo", L"Juice — no devices");
        SetWindowIcons(Kind::App);
    }
    SetWindowTextW(g_hwnd, title.c_str());

    if (!g_taskbar) return;
    if (d && !g_cfg.showNumber && !d->charging) {   // number hidden: only the progress bar
        TBPFLAG state = d->percent >= 30 ? TBPF_NORMAL : d->percent >= 15 ? TBPF_PAUSED : TBPF_ERROR;
        g_taskbar->SetProgressValue(g_hwnd, d->percent < 1 ? 1 : d->percent, 100);
        g_taskbar->SetProgressState(g_hwnd, state);
        g_taskbar->SetOverlayIcon(g_hwnd, nullptr, nullptr);
        if (g_badge) { DestroyIcon(g_badge); g_badge = nullptr; }
    } else if (d) {
        TBPFLAG state = d->charging || d->percent >= 30 ? TBPF_NORMAL : d->percent >= 15 ? TBPF_PAUSED : TBPF_ERROR;
        g_taskbar->SetProgressValue(g_hwnd, d->percent < 1 ? 1 : d->percent, 100);
        g_taskbar->SetProgressState(g_hwnd, state);
        HICON badge = MakeBadgeIcon(d->percent, d->charging, GetSystemMetricsForDpi(SM_CXSMICON, Dpi()));
        g_taskbar->SetOverlayIcon(g_hwnd, badge, title.c_str());
        if (g_badge) DestroyIcon(g_badge);
        g_badge = badge;
    } else {
        g_taskbar->SetProgressState(g_hwnd, TBPF_NOPROGRESS);
        g_taskbar->SetOverlayIcon(g_hwnd, nullptr, nullptr);
        if (g_badge) { DestroyIcon(g_badge); g_badge = nullptr; }
    }
}

// Low-battery notification through a temporary notification-area icon (removed after a while).
void NotifyLow(const Device& d) {
    std::wstring msg = d.Label() + L": " + std::to_wstring(d.percent) + L"%";
    if (g_trayShown) {   // already in the tray: use that icon for the balloon
        NOTIFYICONDATAW nid;
        FillTrayNid(nid);
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
        wcscpy_s(nid.szInfoTitle, T(L"Bateria baixa", L"Low battery"));
        wcsncpy_s(nid.szInfo, msg.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        return;
    }
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uCallbackMessage = WM_APP_NOTIFY;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
    if (g_notifyIcon) DestroyIcon(g_notifyIcon);
    g_notifyIcon = MakeIllustrationIcon(d.kind, GetSystemMetrics(SM_CXSMICON));
    nid.hIcon = g_notifyIcon;
    wcscpy_s(nid.szTip, L"Juice");
    nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(nid.szInfoTitle, T(L"Bateria baixa", L"Low battery"));
    wcsncpy_s(nid.szInfo, msg.c_str(), _TRUNCATE);
    Shell_NotifyIconW(g_notifyShown ? NIM_MODIFY : NIM_ADD, &nid);
    g_notifyShown = true;
    SetTimer(g_hwnd, TIMER_NOTIFY_END, 30000, nullptr);
}

void RemoveNotifyIcon() {
    if (!g_notifyShown) return;
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_notifyShown = false;
}

// ---------------------------------------------------------------------------------------------
// Window

bool RowOnline(const Row& r) { return r.dev && r.dev->present && r.dev->percent >= 0; }

// Remove (×) button of a row, in place of the percentage.
RECT RemoveRect(int row) {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    const int y = S(TOP_PAD) + row * S(ROW_H), sz = S(24), right = rc.right - S(PAD) + S(4);
    return RECT{ right - sz, y + S(9), right, y + S(9) + sz };
}

int ContentHeight() {
    size_t n = VisibleRows().size();
    return S(TOP_PAD) + (n ? (int)n * S(ROW_H) : S(EMPTY_H)) + S(FOOT_H);
}

void ResizeToContent(bool anchorBottom) {
    RECT r{ 0, 0, S(WIN_W), ContentHeight() };
    DWORD style = (DWORD)GetWindowLongPtrW(g_hwnd, GWL_STYLE), ex = (DWORD)GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, Dpi());
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (IsIconic(g_hwnd)) {
        // Minimized: change the restore rectangle without showing/activating the window.
        WINDOWPLACEMENT wp{ sizeof(wp) };
        GetWindowPlacement(g_hwnd, &wp);
        RECT& n = wp.rcNormalPosition;
        if (n.bottom - n.top == h && n.right - n.left == w) return;
        if (anchorBottom) n.top = n.bottom - h;
        n.right = n.left + w;
        n.bottom = n.top + h;
        wp.showCmd = IsWindowVisible(g_hwnd) ? SW_SHOWMINNOACTIVE : SW_HIDE;   // hidden = in the tray
        SetWindowPlacement(g_hwnd, &wp);
    } else {
        RECT cur;
        GetWindowRect(g_hwnd, &cur);
        if (cur.bottom - cur.top == h && cur.right - cur.left == w) return;
        const int top = anchorBottom ? cur.bottom - h : cur.top;
        SetWindowPos(g_hwnd, nullptr, cur.left, top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

HFONT MakeUiFont(int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
}

void DrawTextAt(HDC dc, const std::wstring& s, RECT r, UINT fmt, HFONT f, uint32_t color) {
    HGDIOBJ o = SelectObject(dc, f);
    SetTextColor(dc, Cref(color));
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, o);
}

void Paint(HDC target) {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;
    const Theme th = CurrentTheme();

    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    uint32_t* px = nullptr;
    HBITMAP bmp = CreateDIBSection(target, &bi, DIB_RGB_COLORS, (void**)&px, nullptr, 0);
    HDC dc = CreateCompatibleDC(target);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    for (int i = 0; i < W * H; ++i) px[i] = 0xFF000000 | th.bg;
    SetBkMode(dc, TRANSPARENT);

    HFONT fName = MakeUiFont(14, FW_SEMIBOLD), fPct = MakeUiFont(16, FW_SEMIBOLD), fSub = MakeUiFont(12, FW_NORMAL);
    HFONT fGlyph = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe Fluent Icons");

    auto painterAt = [&](int x, int y, int w, int h, float scale) {
        x = std::max(0, x); y = std::max(0, y);
        w = std::min(w, W - x); h = std::min(h, H - y);
        return Painter(px + (size_t)y * W + x, W, std::max(0, w), std::max(0, h), 0, 0, scale);
    };

    const std::vector<Row> rows = VisibleRows();
    const int tx = S(PAD) + S(ILL) + S(12), right = W - S(PAD);
    int y = S(TOP_PAD);

    if (rows.empty()) {
        DrawTextAt(dc, g_dm.Scanning() ? T(L"Procurando dispositivos...", L"Searching for devices...") : T(L"Nenhum dispositivo encontrado", L"No devices found"),
                   RECT{ 0, y, W, y + S(EMPTY_H) }, DT_CENTER | DT_VCENTER, fSub, th.sub);
        y += S(EMPTY_H);
    }
    for (int i = 0; i < (int)rows.size(); ++i) {
        const Row& r = rows[i];
        const Device* d = r.dev;
        const bool online = RowOnline(r);
        const uint32_t textColor = online ? th.text : th.sub;
        // Disconnected: optionally keep showing the last level read, greyed out.
        int lastPct = -1;
        long long lastTime = 0;
        const bool hasLast = !online && g_cfg.showLast && g_dm.LastKnown(r.key, lastPct, lastTime);

        // Illustration (dimmed when offline by drawing a translucent background veil over it).
        const int ix = S(PAD), iy = y + (S(ROW_H) - S(ILL)) / 2;
        Painter ip = painterAt(ix, iy, S(ILL), S(ILL), S(ILL) / 32.0f);
        DrawIllustration(ip, d ? d->kind : r.kind);
        if (!online)
            ip.Fill([](float, float) { return -1.0f; }, Hex(th.bg, 0.55f));

        // Name + percentage
        const int lineH = S(22);
        SIZE pctSize{};
        if (!online && i == g_hoverRow) {
            // Disconnected row under the mouse: × to forget the device.
            const RECT xr = RemoveRect(i);
            const int bw = xr.right - xr.left, bh = xr.bottom - xr.top;
            const bool hot = g_hover == HOVER_REMOVE;
            Painter hp = painterAt(xr.left, xr.top, bw, bh, 1.0f);
            const float hw = bw / 2.0f, hh = bh / 2.0f, rad = (float)S(4);
            hp.Fill([=](float xx, float yy) { return sdf::Box(xx, yy, hw, hh, hw, hh, rad); },
                    Hex(hot ? 0xC42B1C : th.hover));
            DrawTextAt(dc, L"\xE711", xr, DT_CENTER | DT_VCENTER, fGlyph, hot ? 0xFFFFFF : th.text);
            pctSize.cx = right - xr.left;
        } else {
            std::wstring pct = online ? std::to_wstring(d->percent) + L"%" : hasLast ? std::to_wstring(lastPct) + L"%" : L"—";
            DrawTextAt(dc, pct, RECT{ tx, y + S(10), right, y + S(10) + lineH }, DT_RIGHT | DT_VCENTER, fPct, textColor);
            HGDIOBJ o = SelectObject(dc, fPct);
            GetTextExtentPoint32W(dc, pct.c_str(), (int)pct.size(), &pctSize);
            SelectObject(dc, o);
        }
        DrawTextAt(dc, r.name, RECT{ tx, y + S(10), right - pctSize.cx - S(8), y + S(10) + lineH },
                   DT_LEFT | DT_VCENTER, fName, textColor);

        // Battery bar
        const int by = y + S(37), bh = S(6), bw = right - tx;
        Painter bp = painterAt(tx, by, bw, bh, 1.0f);
        const float fw = (float)bw, fh = (float)bh, rr = fh / 2;
        bp.Fill([=](float x, float yy) { return sdf::Box(x, yy, fw / 2, fh / 2, fw / 2, fh / 2, rr); }, Hex(th.track));
        if (online) {
            const float lw = std::max(fh, fw * d->percent / 100.0f);
            bp.Fill([=](float x, float yy) { return sdf::Box(x, yy, lw / 2, fh / 2, lw / 2, fh / 2, rr); },
                    Hex(LevelColor(d->percent, d->charging)));
        } else if (hasLast) {
            const float lw = std::max(fh, fw * lastPct / 100.0f);
            bp.Fill([=](float x, float yy) { return sdf::Box(x, yy, lw / 2, fh / 2, lw / 2, fh / 2, rr); },
                    Hex(th.sub, 0.45f));
        }

        // Status line
        std::wstring status;
        if (hasLast)            status = T(L"Desconectado · leitura ", L"Disconnected · read ") + AgoMinutes((UnixNow() - lastTime) / 60);
        else if (!online)       status = T(L"Desconectado", L"Disconnected");
        else if (d->charging)   status = T(L"Carregando", L"Charging");
        else if (d->asleep)     status = T(L"Em repouso · leitura ", L"Asleep · read ") + Ago(d->lastGood);
        else if (d->parts[0] >= 0 || d->parts[1] >= 0) {   // earbuds: each one and the case
            const wchar_t* label[3] = { T(L"E ", L"L "), T(L"D ", L"R "), T(L"estojo ", L"case ") };
            for (int p = 0; p < 3; ++p) {
                if (d->parts[p] < 0) continue;
                if (!status.empty()) status += L" · ";
                status += label[p] + std::to_wstring(d->parts[p]) + L"%" + (d->partCharging[p] ? L"⚡" : L"");
            }
        }
        else if (long long left; g_dm.TimeLeft(*d, left)) status = TimeLeftText(left);
        else                    status = T(L"Atualizado ", L"Updated ") + Ago(d->lastGood);
        if (online && g_taskbar && SelectedDevice() == d) status += T(L"  ·  na barra de tarefas", L"  ·  on the taskbar");
        DrawTextAt(dc, status, RECT{ tx, y + S(48), right, y + S(66) }, DT_LEFT | DT_VCENTER, fSub, th.sub);

        y += S(ROW_H);
    }

    // Footer: separator + two text buttons.
    if (y < H)
        for (int x = 0; x < W; ++x) px[(size_t)y * W + x] = 0xFF000000 | th.sep;
    const int fy = y + S(8), fh = S(FOOT_H) - S(16);
    auto button = [&](RECT& out, int x, bool alignRight, const wchar_t* glyph, const wchar_t* label, bool hover) {
        SIZE ts{}, gs{};
        HGDIOBJ o = SelectObject(dc, fSub);
        GetTextExtentPoint32W(dc, label, (int)wcslen(label), &ts);
        SelectObject(dc, fGlyph);
        GetTextExtentPoint32W(dc, glyph, 1, &gs);
        SelectObject(dc, o);
        const int bw = S(10) + gs.cx + S(8) + ts.cx + S(10);
        const int bx = alignRight ? x - bw : x;
        out = RECT{ bx, fy, bx + bw, fy + fh };
        if (hover) {
            Painter hp = painterAt(bx, fy, bw, fh, 1.0f);
            const float hw = bw / 2.0f, hh = fh / 2.0f, rad = (float)S(4);
            hp.Fill([=](float xx, float yy) { return sdf::Box(xx, yy, hw, hh, hw, hh, rad); }, Hex(th.hover));
        }
        DrawTextAt(dc, glyph, RECT{ bx + S(10), fy, bx + S(10) + gs.cx, fy + fh }, DT_LEFT | DT_VCENTER, fGlyph, th.text);
        DrawTextAt(dc, label, RECT{ bx + S(10) + gs.cx + S(8), fy, bx + bw, fy + fh }, DT_LEFT | DT_VCENTER, fSub, th.text);
    };
    button(g_btnRefresh, S(PAD) - S(10), false, L"\xE72C", g_dm.Scanning() ? T(L"Atualizando...", L"Refreshing...") : T(L"Atualizar", L"Refresh"),
           g_hover == HOVER_REFRESH);
    button(g_btnSettings, W - S(PAD) + S(10), true, L"\xE713", T(L"Configurações", L"Settings"), g_hover == HOVER_SETTINGS);

    BitBlt(target, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    DeleteObject(fName); DeleteObject(fPct); DeleteObject(fSub); DeleteObject(fGlyph);
    SelectObject(dc, oldBmp);
    DeleteDC(dc);
    DeleteObject(bmp);
}

void ApplyThemeToFrame() {
    BOOL dark = !AppsUseLightTheme();
    DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
}

void RefreshAll() {
    UpdateTaskbar();
    ResizeToContent(true);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void StartScan() {
    g_dm.StartScan(g_hwnd, WM_APP_SCANDONE);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
void RestartPollTimer() { SetTimer(g_hwnd, TIMER_POLL, g_cfg.intervalMin * 60u * 1000u, nullptr); }

// ---------------------------------------------------------------------------------------------
// Settings dialog

struct DlgState { std::vector<std::wstring> listKeys, comboKeys; };

void FillDeviceControls(HWND dlg, DlgState& st) {
    // Every device we know about: seen this session or remembered in the ini.
    std::vector<std::pair<std::wstring, std::wstring>> all;
    for (auto& d : g_dm.Devices())
        if (!d.name.empty()) all.emplace_back(d.key, d.Label());
    for (auto& [k, n] : g_cfg.known) {
        bool dup = false;
        for (auto& a : all) if (a.first == k) { dup = true; break; }
        if (!dup) all.emplace_back(k, n);
    }

    HWND combo = GetDlgItem(dlg, IDC_BAR_DEVICE);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)T(L"Automático (menor bateria)", L"Automatic (lowest battery)"));
    st.comboKeys.push_back(L"");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)T(L"Todos (alterna a cada minuto)", L"All (rotate every minute)"));
    st.comboKeys.push_back(L"*");
    int sel = g_cfg.barDevice == L"*" ? 1 : 0;
    for (auto& [k, n] : all) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)n.c_str());
        st.comboKeys.push_back(k);
        if (k == g_cfg.barDevice) sel = (int)st.comboKeys.size() - 1;
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);

    HWND list = GetDlgItem(dlg, IDC_DEVICES);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    RECT rc;
    GetClientRect(list, &rc);
    // One column: the list only chooses which devices are shown (their levels are in the main window).
    LVCOLUMNW col{ LVCF_WIDTH };
    col.cx = rc.right;
    ListView_InsertColumn(list, 0, &col);
    int i = 0;
    for (auto& [k, n] : all) {
        st.listKeys.push_back(k);
        LVITEMW it{ LVIF_TEXT };
        it.iItem = i;
        it.pszText = (LPWSTR)n.c_str();
        ListView_InsertItem(list, &it);
        ListView_SetCheckState(list, i, !g_dm.IsHidden(k));
        ++i;
    }
    // The column fills the list exactly; room for the scroll bar only when it shows.
    const bool scroll = i > ListView_GetCountPerPage(list);
    ListView_SetColumnWidth(list, 0, rc.right - (scroll ? GetSystemMetrics(SM_CXVSCROLL) : 0));
}

// Settings dialog look: same background and text colors as the main window, following the Windows
// theme. Controls use the system's dark themes; checkboxes are custom drawn because a themed checkbox
// ignores the text color.
struct DlgLook {
    Theme   th{};
    bool    dark = false;
    HBRUSH  bg = nullptr, field = nullptr;
    HFONT   section = nullptr;
    HTHEME  button = nullptr;
};
DlgLook g_look;

const int SECTION_IDS[] = { IDC_SEC_DEVICES, IDC_SEC_ICONS, IDC_SEC_REFRESH, IDC_SEC_GENERAL };
const int CHECK_IDS[] = { IDC_SHOWLAST, IDC_JBL, IDC_APPLE, IDC_SHOWNUMBER, IDC_COLORLEVELS, IDC_LOWWARN, IDC_AUTOSTART };

void ApplyDialogLook(HWND dlg) {
    g_look.dark = !AppsUseLightTheme();
    g_look.th = CurrentTheme();
    const Theme& th = g_look.th;
    g_look.bg = CreateSolidBrush(Cref(th.bg));
    g_look.field = CreateSolidBrush(Cref(g_look.dark ? 0x2B2B2B : 0xFFFFFF));
    BOOL dark = g_look.dark;
    DwmSetWindowAttribute(dlg, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    LOGFONTW lf{};
    GetObjectW((HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0), sizeof(lf), &lf);
    lf.lfWeight = FW_SEMIBOLD;
    g_look.section = CreateFontIndirectW(&lf);
    for (int id : SECTION_IDS) SendDlgItemMessageW(dlg, id, WM_SETFONT, (WPARAM)g_look.section, FALSE);

    if (g_look.dark) {
        SetWindowTheme(GetDlgItem(dlg, IDC_BAR_DEVICE), L"DarkMode_CFD", nullptr);
        SetWindowTheme(GetDlgItem(dlg, IDC_INTERVAL), L"DarkMode_CFD", nullptr);
        SetWindowTheme(GetDlgItem(dlg, IDC_THRESHOLD), L"DarkMode_CFD", nullptr);
        SetWindowTheme(GetDlgItem(dlg, IDC_DEVICES), L"DarkMode_ItemsView", nullptr);   // no column dividers
        for (int id : { IDC_INTERVAL_SPIN, IDC_THRESHOLD_SPIN, IDOK, IDCANCEL })
            SetWindowTheme(GetDlgItem(dlg, id), L"DarkMode_Explorer", nullptr);
    }
    HWND list = GetDlgItem(dlg, IDC_DEVICES);
    ListView_SetBkColor(list, Cref(g_look.dark ? 0x2B2B2B : 0xFFFFFF));
    ListView_SetTextBkColor(list, CLR_NONE);
    ListView_SetTextColor(list, Cref(th.text));
    g_look.button = OpenThemeData(dlg, g_look.dark ? L"DarkMode_Explorer::Button" : L"Button");
}

void FreeDialogLook() {
    if (g_look.bg) DeleteObject(g_look.bg);
    if (g_look.field) DeleteObject(g_look.field);
    if (g_look.section) DeleteObject(g_look.section);
    if (g_look.button) CloseThemeData(g_look.button);
    g_look = DlgLook{};
}

// Checkbox: themed box + text in the dialog's text color.
LRESULT DrawCheckbox(const NMCUSTOMDRAW* cd) {
    if (cd->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    HDC dc = cd->hdc;
    RECT r = cd->rc;
    FillRect(dc, &r, g_look.bg);
    const bool on = SendMessageW(cd->hdr.hwndFrom, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool off = (cd->uItemState & CDIS_DISABLED) != 0;
    const bool down = (cd->uItemState & CDIS_SELECTED) != 0, hot = (cd->uItemState & CDIS_HOT) != 0;
    const int state = (on ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL) + (off ? 3 : down ? 2 : hot ? 1 : 0);
    SIZE box{ 13, 13 };
    if (g_look.button) GetThemePartSize(g_look.button, dc, BP_CHECKBOX, state, nullptr, TS_DRAW, &box);
    RECT br{ r.left, r.top + (r.bottom - r.top - box.cy) / 2, 0, 0 };
    br.right = br.left + box.cx;
    br.bottom = br.top + box.cy;
    if (g_look.button) DrawThemeBackground(g_look.button, dc, BP_CHECKBOX, state, &br, nullptr);
    else DrawFrameControl(dc, &br, DFC_BUTTON, DFCS_BUTTONCHECK | (on ? DFCS_CHECKED : 0));

    wchar_t text[128];
    GetWindowTextW(cd->hdr.hwndFrom, text, 128);
    RECT tr{ br.right + MulDiv(5, GetDpiForWindow(cd->hdr.hwndFrom), 96), r.top, r.right, r.bottom };
    HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(cd->hdr.hwndFrom, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Cref(off ? g_look.th.sub : g_look.th.text));
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((cd->uItemState & CDIS_FOCUS) && !(SendMessageW(cd->hdr.hwndFrom, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        RECT fr = tr;
        DrawTextW(dc, text, -1, &fr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
        OffsetRect(&fr, 0, (tr.bottom - tr.top - (fr.bottom - fr.top)) / 2);
        InflateRect(&fr, 2, 1);
        DrawFocusRect(dc, &fr);
    }
    SelectObject(dc, old);
    return CDRF_SKIPDEFAULT;
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    static DlgState* st = nullptr;
    switch (msg) {
    case WM_INITDIALOG:
        st = new DlgState;
        g_settingsDlg = dlg;
        if (g_en) {   // the dialog resource is written in Portuguese
            SetWindowTextW(dlg, L"Juice — Settings");
            SetDlgItemTextW(dlg, IDC_SEC_DEVICES, L"DEVICES");
            SetDlgItemTextW(dlg, IDC_LBL_BARDEVICE, L"On the taskbar");
            SetDlgItemTextW(dlg, IDC_SHOWLAST, L"Show the last level of disconnected devices");
            SetDlgItemTextW(dlg, IDC_JBL, L"Read JBL speakers (Bluetooth LE)");
            SetDlgItemTextW(dlg, IDC_APPLE, L"Read AirPods and Beats earbuds (Bluetooth LE)");
            SetDlgItemTextW(dlg, IDC_SEC_ICONS, L"ICONS");
            SetDlgItemTextW(dlg, IDC_SHOWNUMBER, L"Percentage on the taskbar icon");
            SetDlgItemTextW(dlg, IDC_COLORLEVELS, L"Color the tray number by level");
            SetDlgItemTextW(dlg, IDC_SEC_REFRESH, L"REFRESH AND WARNINGS");
            SetDlgItemTextW(dlg, IDC_LBL_EVERY, L"Refresh every");
            SetDlgItemTextW(dlg, IDC_LOWWARN, L"Warn when the battery drops below");
            SetDlgItemTextW(dlg, IDC_SEC_GENERAL, L"GENERAL");
            SetDlgItemTextW(dlg, IDC_AUTOSTART, L"Start with Windows (in the tray)");
            SetDlgItemTextW(dlg, IDCANCEL, L"Cancel");
        }
        ApplyDialogLook(dlg);
        FillDeviceControls(dlg, *st);
        SendDlgItemMessageW(dlg, IDC_INTERVAL_SPIN, UDM_SETRANGE32, 1, 60);
        SendDlgItemMessageW(dlg, IDC_INTERVAL_SPIN, UDM_SETPOS32, 0, g_cfg.intervalMin);
        SendDlgItemMessageW(dlg, IDC_THRESHOLD_SPIN, UDM_SETRANGE32, 5, 50);
        SendDlgItemMessageW(dlg, IDC_THRESHOLD_SPIN, UDM_SETPOS32, 0, g_cfg.lowThreshold);
        CheckDlgButton(dlg, IDC_LOWWARN, g_cfg.lowWarning ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_SHOWNUMBER, g_cfg.showNumber ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_COLORLEVELS, g_cfg.colorLevels ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_AUTOSTART, AutostartEnabled(AUTOSTART_NAME) ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_JBL, g_cfg.jbl ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_APPLE, g_cfg.apple ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_SHOWLAST, g_cfg.showLast ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(dlg, IDC_THRESHOLD), g_cfg.lowWarning);
        EnableWindow(GetDlgItem(dlg, IDC_THRESHOLD_SPIN), g_cfg.lowWarning);
        return TRUE;
    case WM_CTLCOLORDLG:
        return (INT_PTR)g_look.bg;
    case WM_CTLCOLORSTATIC: {   // labels, section titles and the disabled threshold field
        HDC dc = (HDC)wp;
        const int id = GetDlgCtrlID((HWND)lp);
        const bool section = id == IDC_SEC_DEVICES || id == IDC_SEC_ICONS || id == IDC_SEC_REFRESH || id == IDC_SEC_GENERAL;
        SetTextColor(dc, Cref(section || !IsWindowEnabled((HWND)lp) ? g_look.th.sub : g_look.th.text));
        SetBkColor(dc, Cref(g_look.th.bg));
        return (INT_PTR)g_look.bg;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:   // number fields and the combo's drop-down list
        if (!g_look.dark) break;
        SetTextColor((HDC)wp, Cref(g_look.th.text));
        SetBkColor((HDC)wp, Cref(0x2B2B2B));
        return (INT_PTR)g_look.field;
    case WM_NOTIFY: {
        const NMHDR* h = (const NMHDR*)lp;
        if (h->code == NM_CUSTOMDRAW)
            for (int id : CHECK_IDS)
                if ((int)h->idFrom == id) {
                    SetWindowLongPtrW(dlg, DWLP_MSGRESULT, DrawCheckbox((const NMCUSTOMDRAW*)lp));
                    return TRUE;
                }
        break;
    }
    case WM_PAINT: {   // thin line under each section title
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(dlg, &ps);
        HBRUSH line = CreateSolidBrush(Cref(g_look.th.track));
        for (int id : SECTION_IDS) {
            RECT r;
            GetWindowRect(GetDlgItem(dlg, id), &r);
            MapWindowPoints(nullptr, dlg, (POINT*)&r, 2);
            const int y = r.bottom + MulDiv(4, GetDpiForWindow(dlg), 96);
            RECT l{ r.left, y, r.right, y + 1 };
            FillRect(dc, &l, line);
        }
        DeleteObject(line);
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_LOWWARN:
            EnableWindow(GetDlgItem(dlg, IDC_THRESHOLD), IsDlgButtonChecked(dlg, IDC_LOWWARN) == BST_CHECKED);
            EnableWindow(GetDlgItem(dlg, IDC_THRESHOLD_SPIN), IsDlgButtonChecked(dlg, IDC_LOWWARN) == BST_CHECKED);
            return TRUE;
        case IDOK: {
            int sel = (int)SendDlgItemMessageW(dlg, IDC_BAR_DEVICE, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)st->comboKeys.size()) g_cfg.barDevice = st->comboKeys[sel];
            HWND list = GetDlgItem(dlg, IDC_DEVICES);
            for (int i = 0; i < (int)st->listKeys.size(); ++i) {
                if (ListView_GetCheckState(list, i)) g_cfg.hidden.erase(st->listKeys[i]);
                else g_cfg.hidden[st->listKeys[i]] = true;
            }
            BOOL ok;
            int v = (int)GetDlgItemInt(dlg, IDC_INTERVAL, &ok, FALSE);
            if (ok) g_cfg.intervalMin = v < 1 ? 1 : v > 60 ? 60 : v;
            v = (int)GetDlgItemInt(dlg, IDC_THRESHOLD, &ok, FALSE);
            if (ok) g_cfg.lowThreshold = v < 5 ? 5 : v > 50 ? 50 : v;
            g_cfg.lowWarning = IsDlgButtonChecked(dlg, IDC_LOWWARN) == BST_CHECKED;
            g_cfg.showNumber = IsDlgButtonChecked(dlg, IDC_SHOWNUMBER) == BST_CHECKED;
            g_cfg.colorLevels = IsDlgButtonChecked(dlg, IDC_COLORLEVELS) == BST_CHECKED;
            const bool jblWasOn = g_cfg.jbl, appleWasOn = g_cfg.apple;
            g_cfg.jbl = IsDlgButtonChecked(dlg, IDC_JBL) == BST_CHECKED;
            g_cfg.apple = IsDlgButtonChecked(dlg, IDC_APPLE) == BST_CHECKED;
            g_cfg.showLast = IsDlgButtonChecked(dlg, IDC_SHOWLAST) == BST_CHECKED;
            bool autostart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
            // Rewritten when on, so older entries (--minimized) pick up the current arguments.
            if (autostart || AutostartEnabled(AUTOSTART_NAME)) SetAutostart(AUTOSTART_NAME, autostart, L"--tray");
            g_cfg.Save();
            RestartPollTimer();
            RefreshAll();
            if ((g_cfg.jbl && !jblWasOn) || (g_cfg.apple && !appleWasOn)) StartScan();   // show them right away
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        delete st;
        st = nullptr;
        g_settingsDlg = nullptr;
        FreeDialogLook();
        break;
    }
    return FALSE;
}

void OpenSettings();

void ShowTrayMenu(POINT pt) {
    enum { ID_OPEN = 1, ID_REFRESH, ID_SETTINGS, ID_EXIT };
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN, T(L"Abrir", L"Open"));
    SetMenuDefaultItem(m, ID_OPEN, FALSE);
    AppendMenuW(m, MF_STRING, ID_REFRESH, T(L"Atualizar agora", L"Refresh now"));
    AppendMenuW(m, MF_STRING, ID_SETTINGS, T(L"Configurações...", L"Settings..."));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, T(L"Sair", L"Exit"));
    SetForegroundWindow(g_hwnd);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case ID_OPEN:     RestoreFromTray(); break;
    case ID_REFRESH:  StartScan(); break;
    case ID_SETTINGS: OpenSettings(); break;
    case ID_EXIT:     DestroyWindow(g_hwnd); break;
    }
}

void OpenSettings() {
    if (g_settingsDlg) { SetForegroundWindow(g_settingsDlg); return; }
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_BAR_SETTINGS), g_hwnd, SettingsProc, 0);
}

// ---------------------------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_msgButtonCreated && msg != 0) {
        if (!g_taskbar &&
            SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_taskbar))))
            g_taskbar->HrInit();
        UpdateTaskbar();
        return 0;
    }
    if (msg == g_msgTaskbarCreated && msg != 0) {   // Explorer restarted
        g_notifyShown = false;
        g_trayShown = false;
        UpdateTray();
        return 0;
    }
    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            RestoreFromTray();
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu(POINT{ (short)LOWORD(wp), (short)HIWORD(wp) });
            break;
        }
        return 0;
    case WM_APP_SHOW:   // second launch
        if (g_inTray) RestoreFromTray();
        else { ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); }
        return 0;
    case WM_CLOSE:
        GoToTray();
        return 0;
    case WM_APP_SCANDONE:
        g_dm.OnScanDone((ScanResult*)lp);
        ApplyDemo();
        for (Device* d : g_dm.TakeLowBatteryAlerts()) NotifyLow(*d);
        RefreshAll();
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        return 0;
    case WM_APP_PADS:   // a controller reported its battery, connected or left
        g_dm.MergePads();
        for (Device* d : g_dm.TakeLowBatteryAlerts()) NotifyLow(*d);
        RefreshAll();
        return 0;
    case WM_TIMER:
        switch (wp) {
        case TIMER_SOON: KillTimer(hwnd, TIMER_SOON); [[fallthrough]];
        case TIMER_POLL: StartScan(); break;
        case TIMER_CLOCK: if (!IsIconic(hwnd)) InvalidateRect(hwnd, nullptr, FALSE); break;
        case TIMER_NOTIFY_END: KillTimer(hwnd, TIMER_NOTIFY_END); RemoveNotifyIcon(); break;
        case TIMER_ROTATE:
            if (g_cfg.barDevice == L"*") {
                ++g_rotateIndex;
                UpdateTaskbar();
                InvalidateRect(hwnd, nullptr, FALSE);   // "na barra de tarefas" moves to the next row
            }
            break;
        }
        return 0;
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED) { SetTimer(hwnd, TIMER_SOON, 3000, nullptr); PadsRescan(); }
        return TRUE;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC) SetTimer(hwnd, TIMER_SOON, 5000, nullptr);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        Paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
        const std::vector<Row> rows = VisibleRows();
        int row = pt.y >= S(TOP_PAD) ? (pt.y - S(TOP_PAD)) / S(ROW_H) : -1;
        if (row >= (int)rows.size() || (row >= 0 && RowOnline(rows[row]))) row = -1;
        RECT xr = row >= 0 ? RemoveRect(row) : RECT{};
        int h = PtInRect(&g_btnRefresh, pt) ? HOVER_REFRESH : PtInRect(&g_btnSettings, pt) ? HOVER_SETTINGS
              : row >= 0 && PtInRect(&xr, pt) ? HOVER_REMOVE : HOVER_NONE;
        if (h != g_hover || row != g_hoverRow) {
            g_hover = h;
            g_hoverRow = row;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hover != HOVER_NONE || g_hoverRow >= 0) {
            g_hover = HOVER_NONE;
            g_hoverRow = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hover != HOVER_NONE) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONUP:
        if (g_hover == HOVER_REFRESH) StartScan();
        else if (g_hover == HOVER_SETTINGS) OpenSettings();
        else if (g_hover == HOVER_REMOVE) {
            // Re-check against the current list: a scan may have reordered the rows since the last move.
            const std::vector<Row> rows = VisibleRows();
            POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
            RECT xr = g_hoverRow >= 0 ? RemoveRect(g_hoverRow) : RECT{};
            if (g_hoverRow >= 0 && g_hoverRow < (int)rows.size() && !RowOnline(rows[g_hoverRow]) && PtInRect(&xr, pt)) {
                g_dm.Forget(rows[g_hoverRow].key);
                g_hover = HOVER_NONE;
                g_hoverRow = -1;
                RefreshAll();
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) ShowWindow(hwnd, SW_MINIMIZE);
        else if (wp == VK_F5) StartScan();
        else if (g_demoPercent >= 0 && (wp == VK_UP || wp == VK_DOWN || wp == 'C')) {
            if (wp == 'C') g_demoCharging = !g_demoCharging;
            else g_demoPercent = std::min(100, std::max(0, g_demoPercent + (wp == VK_UP ? 5 : -5)));
            ApplyDemo();
            for (Device* d : g_dm.TakeLowBatteryAlerts()) NotifyLow(*d);
            RefreshAll();
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
            ApplyThemeToFrame();
            UpdateTray();   // tray digits follow the taskbar theme
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_DPICHANGED: {
        const RECT* r = (const RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g_iconValid = false;
        RefreshAll();
        return 0;
    }
    case WM_ENDSESSION:   // Windows shutting down: keep the time of the last readings
        if (wp && g_dm.keepLast) { g_dm.RecordLast(); g_cfg.Save(); }
        return 0;
    case WM_DESTROY:
        if (g_dm.keepLast) { g_dm.RecordLast(); g_cfg.Save(); }   // keeps the time of the last readings
        RemoveNotifyIcon();
        RemoveTray();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int) {
    if (const wchar_t* demo = cmdLine ? wcsstr(cmdLine, L"--demo") : nullptr) {
        wchar_t* end = nullptr;
        long v = wcstol(demo + 6, &end, 10);
        g_demoPercent = end == demo + 6 ? 50 : (int)std::min(100L, std::max(0L, v));
        g_demoCharging = end && (*end == L'c' || *end == L'C');
        g_dm.keepLast = false;
    }

    // The demo runs alongside the real instance, with its own taskbar button.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, g_demoPercent >= 0 ? L"Juice.Demo"
                                                                  : L"Juice.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS && g_demoPercent < 0) {
        if (HWND other = FindWindowW(CLASS_NAME, nullptr)) {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(other, WM_APP_SHOW, 0, 0);
        }
        return 0;
    }

    if (const wchar_t* lang = cmdLine ? wcsstr(cmdLine, L"--lang ") : nullptr) g_en = _wcsnicmp(lang + 7, L"pt", 2) != 0;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetCurrentProcessExplicitAppUserModelID(g_demoPercent >= 0 ? L"Juice.Demo" : APP_ID);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g_cfg.file = L"juice.ini";
    g_cfg.Load();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    g_msgButtonCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // Initial placement: bottom-right corner of the work area of the monitor under the cursor.
    POINT cur;
    GetCursorPos(&cur);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY), &mi);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    g_hwnd = CreateWindowExW(0, CLASS_NAME, L"Juice", style, mi.rcWork.right - 400, mi.rcWork.bottom - 300,
                             360, 260, nullptr, nullptr, inst, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, g_msgButtonCreated, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, g_msgTaskbarCreated, MSGFLT_ALLOW, nullptr);
    ApplyThemeToFrame();

    // Size to content, then anchor to the bottom-right of the work area.
    ResizeToContent(false);
    RECT wr;
    GetWindowRect(g_hwnd, &wr);
    const int margin = S(12);
    SetWindowPos(g_hwnd, nullptr, mi.rcWork.right - (wr.right - wr.left) - margin,
                 mi.rcWork.bottom - (wr.bottom - wr.top) - margin, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    ApplyDemo();
    // --tray (autostart) starts closed, in the notification area; --minimized is the old autostart flag.
    const bool startInTray = cmdLine && (wcsstr(cmdLine, L"--tray") || wcsstr(cmdLine, L"--minimized"));
    if (startInTray) g_inTray = true;
    else ShowWindow(g_hwnd, SW_SHOW);
    UpdateTaskbar();

    PadsStart(g_hwnd, WM_APP_PADS);
    StartScan();
    RestartPollTimer();
    SetTimer(g_hwnd, TIMER_CLOCK, 60000, nullptr);
    SetTimer(g_hwnd, TIMER_ROTATE, 60000, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (g_settingsDlg && IsDialogMessageW(g_settingsDlg, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (g_taskbar) g_taskbar->Release();
    if (g_badge) DestroyIcon(g_badge);
    if (g_iconBig) DestroyIcon(g_iconBig);
    if (g_iconSmall) DestroyIcon(g_iconSmall);
    if (g_notifyIcon) DestroyIcon(g_notifyIcon);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
