// Wireless Bar: a regular taskbar button whose progress bar shows the battery level of a wireless
// device (green / yellow / red), with an overlay badge (% or charging bolt) and a compact window
// listing every device.
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <dwmapi.h>
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

namespace {

constexpr UINT WM_APP_SCANDONE = WM_APP + 1;
constexpr UINT WM_APP_NOTIFY   = WM_APP + 2;
constexpr UINT_PTR TIMER_POLL = 1, TIMER_SOON = 2, TIMER_CLOCK = 3, TIMER_NOTIFY_END = 4;
const wchar_t* CLASS_NAME     = L"WirelessBarWnd";
const wchar_t* AUTOSTART_NAME = L"WirelessBar";
const wchar_t* APP_ID         = L"WirelessTray.WirelessBar";

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

enum Hover { HOVER_NONE, HOVER_REFRESH, HOVER_SETTINGS };
int  g_hover = HOVER_NONE;
RECT g_btnRefresh{}, g_btnSettings{};

// ---------------------------------------------------------------------------------------------
// Helpers

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

std::wstring Ago(ULONGLONG tick) {
    ULONGLONG min = (GetTickCount64() - tick) / 60000;
    if (min < 1) return L"agora";
    if (min < 60) return L"há " + std::to_wstring(min) + L" min";
    return L"há " + std::to_wstring(min / 60) + L" h";
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
        rows.push_back({ k, n, Kind::Other, nullptr });
    }
    return rows;
}

const Device* SelectedDevice() {
    const Device* best = nullptr;
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
            d->name = L"Demonstração";
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
    const Device* d = SelectedDevice();
    std::wstring title;
    if (d) {
        title = d->Label() + L" — " + std::to_wstring(d->percent) + L"%" + (d->charging ? L" · carregando" : L"") +
                (g_demoPercent >= 0 ? L" (demo)" : L"");
        SetWindowIcons(d->kind);
    } else {
        title = g_dm.Scanning() ? L"Wireless Bar — procurando dispositivos..." : L"Wireless Bar — nenhum dispositivo";
        SetWindowIcons(Kind::App);
    }
    SetWindowTextW(g_hwnd, title.c_str());

    if (!g_taskbar) return;
    if (d) {
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
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uCallbackMessage = WM_APP_NOTIFY;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
    if (g_notifyIcon) DestroyIcon(g_notifyIcon);
    g_notifyIcon = MakeIllustrationIcon(d.kind, GetSystemMetrics(SM_CXSMICON));
    nid.hIcon = g_notifyIcon;
    wcscpy_s(nid.szTip, L"Wireless Bar");
    nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(nid.szInfoTitle, L"Bateria baixa");
    std::wstring msg = d.Label() + L": " + std::to_wstring(d.percent) + L"%";
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
        wp.showCmd = SW_SHOWMINNOACTIVE;
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
        DrawTextAt(dc, g_dm.Scanning() ? L"Procurando dispositivos..." : L"Nenhum dispositivo encontrado",
                   RECT{ 0, y, W, y + S(EMPTY_H) }, DT_CENTER | DT_VCENTER, fSub, th.sub);
        y += S(EMPTY_H);
    }
    for (const Row& r : rows) {
        const Device* d = r.dev;
        const bool online = d && d->present && d->percent >= 0;
        const uint32_t textColor = online ? th.text : th.sub;

        // Illustration (dimmed when offline by drawing a translucent background veil over it).
        const int ix = S(PAD), iy = y + (S(ROW_H) - S(ILL)) / 2;
        Painter ip = painterAt(ix, iy, S(ILL), S(ILL), S(ILL) / 32.0f);
        DrawIllustration(ip, d ? d->kind : r.kind);
        if (!online)
            ip.Fill([](float, float) { return -1.0f; }, Hex(th.bg, 0.55f));

        // Name + percentage
        const int lineH = S(22);
        std::wstring pct = online ? std::to_wstring(d->percent) + L"%" : L"—";
        DrawTextAt(dc, pct, RECT{ tx, y + S(10), right, y + S(10) + lineH }, DT_RIGHT | DT_VCENTER, fPct, textColor);
        SIZE pctSize{};
        HGDIOBJ o = SelectObject(dc, fPct);
        GetTextExtentPoint32W(dc, pct.c_str(), (int)pct.size(), &pctSize);
        SelectObject(dc, o);
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
        }

        // Status line
        std::wstring status;
        if (!online)            status = L"Desconectado";
        else if (d->charging)   status = L"Carregando";
        else if (d->asleep)     status = L"Em repouso · leitura " + Ago(d->lastGood);
        else                    status = L"Atualizado " + Ago(d->lastGood);
        if (online && g_taskbar && SelectedDevice() == d) status += L"  ·  na barra de tarefas";
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
    button(g_btnRefresh, S(PAD) - S(10), false, L"\xE72C", g_dm.Scanning() ? L"Atualizando..." : L"Atualizar",
           g_hover == HOVER_REFRESH);
    button(g_btnSettings, W - S(PAD) + S(10), true, L"\xE713", L"Configurações", g_hover == HOVER_SETTINGS);

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
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Automático (menor bateria)");
    st.comboKeys.push_back(L"");
    int sel = 0;
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
    LVCOLUMNW col{ LVCF_TEXT | LVCF_WIDTH };
    const int w2 = MulDiv(80, GetDpiForWindow(list), 96);
    col.cx = rc.right - w2 - GetSystemMetrics(SM_CXVSCROLL);
    col.pszText = (LPWSTR)L"Dispositivo";
    ListView_InsertColumn(list, 0, &col);
    col.cx = w2;
    col.pszText = (LPWSTR)L"Bateria";
    ListView_InsertColumn(list, 1, &col);
    int i = 0;
    for (auto& [k, n] : all) {
        st.listKeys.push_back(k);
        LVITEMW it{ LVIF_TEXT };
        it.iItem = i;
        it.pszText = (LPWSTR)n.c_str();
        ListView_InsertItem(list, &it);
        std::wstring status = L"desconectado";
        if (Device* d = g_dm.Find(k); d && d->present && d->percent >= 0)
            status = std::to_wstring(d->percent) + L"%" + (d->charging ? L" ⚡" : L"");
        ListView_SetItemText(list, i, 1, (LPWSTR)status.c_str());
        ListView_SetCheckState(list, i, !g_dm.IsHidden(k));
        ++i;
    }
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    static DlgState* st = nullptr;
    switch (msg) {
    case WM_INITDIALOG:
        st = new DlgState;
        g_settingsDlg = dlg;
        FillDeviceControls(dlg, *st);
        SendDlgItemMessageW(dlg, IDC_INTERVAL_SPIN, UDM_SETRANGE32, 1, 60);
        SendDlgItemMessageW(dlg, IDC_INTERVAL_SPIN, UDM_SETPOS32, 0, g_cfg.intervalMin);
        SendDlgItemMessageW(dlg, IDC_THRESHOLD_SPIN, UDM_SETRANGE32, 5, 50);
        SendDlgItemMessageW(dlg, IDC_THRESHOLD_SPIN, UDM_SETPOS32, 0, g_cfg.lowThreshold);
        CheckDlgButton(dlg, IDC_LOWWARN, g_cfg.lowWarning ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_AUTOSTART, AutostartEnabled(AUTOSTART_NAME) ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(dlg, IDC_THRESHOLD), g_cfg.lowWarning);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_LOWWARN:
            EnableWindow(GetDlgItem(dlg, IDC_THRESHOLD), IsDlgButtonChecked(dlg, IDC_LOWWARN) == BST_CHECKED);
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
            bool autostart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
            if (autostart != AutostartEnabled(AUTOSTART_NAME)) SetAutostart(AUTOSTART_NAME, autostart, L"--minimized");
            g_cfg.Save();
            RestartPollTimer();
            RefreshAll();
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
        break;
    }
    return FALSE;
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
        return 0;
    }
    switch (msg) {
    case WM_APP_SCANDONE:
        g_dm.OnScanDone((ScanResult*)lp);
        ApplyDemo();
        for (Device* d : g_dm.TakeLowBatteryAlerts()) NotifyLow(*d);
        RefreshAll();
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        return 0;
    case WM_TIMER:
        switch (wp) {
        case TIMER_SOON: KillTimer(hwnd, TIMER_SOON); [[fallthrough]];
        case TIMER_POLL: StartScan(); break;
        case TIMER_CLOCK: if (!IsIconic(hwnd)) InvalidateRect(hwnd, nullptr, FALSE); break;
        case TIMER_NOTIFY_END: KillTimer(hwnd, TIMER_NOTIFY_END); RemoveNotifyIcon(); break;
        }
        return 0;
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED) SetTimer(hwnd, TIMER_SOON, 3000, nullptr);
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
        int h = PtInRect(&g_btnRefresh, pt) ? HOVER_REFRESH : PtInRect(&g_btnSettings, pt) ? HOVER_SETTINGS : HOVER_NONE;
        if (h != g_hover) {
            g_hover = h;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hover != HOVER_NONE) { g_hover = HOVER_NONE; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hover != HOVER_NONE) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONUP:
        if (g_hover == HOVER_REFRESH) StartScan();
        else if (g_hover == HOVER_SETTINGS) OpenSettings();
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
    case WM_DESTROY:
        RemoveNotifyIcon();
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
    }

    // The demo runs alongside the real instance, with its own taskbar button.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, g_demoPercent >= 0 ? L"WirelessTray.WirelessBar.Demo"
                                                                  : L"WirelessTray.WirelessBar.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS && g_demoPercent < 0) {
        if (HWND other = FindWindowW(CLASS_NAME, nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetCurrentProcessExplicitAppUserModelID(g_demoPercent >= 0 ? L"WirelessTray.WirelessBar.Demo" : APP_ID);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g_cfg.file = L"bar.ini";
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
    g_hwnd = CreateWindowExW(0, CLASS_NAME, L"Wireless Bar", style, mi.rcWork.right - 400, mi.rcWork.bottom - 300,
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
    UpdateTaskbar();
    const bool minimized = cmdLine && wcsstr(cmdLine, L"--minimized");
    ShowWindow(g_hwnd, minimized ? SW_SHOWMINNOACTIVE : SW_SHOW);

    StartScan();
    RestartPollTimer();
    SetTimer(g_hwnd, TIMER_CLOCK, 60000, nullptr);

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
