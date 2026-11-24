// Wireless Tray: shows battery levels of wireless mice/keyboards/headsets in the notification area.
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dbt.h>

#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "devices.h"
#include "icon.h"
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

namespace {

constexpr UINT WM_APP_TRAY     = WM_APP + 1;
constexpr UINT WM_APP_SCANDONE = WM_APP + 2;
constexpr UINT_PTR TIMER_POLL  = 1;
constexpr UINT_PTR TIMER_SOON  = 2;   // debounced refresh after device change / resume
constexpr UINT APP_ICON_ID     = 0;   // fallback icon shown when no device icon is visible
const wchar_t* AUTOSTART_NAME  = L"WirelessTray";

enum MenuId : UINT { ID_REFRESH = 100, ID_SETTINGS, ID_EXIT };

// Per-device tray icon state.
struct TrayIcon {
    UINT  id = 0;
    bool  shown = false;
    HICON hIcon = nullptr;
    int   drawnPercent = -2;
    bool  drawnCharging = false;
};

HWND   g_hwnd;
UINT   g_taskbarCreated;
Config g_cfg;
DeviceManager g_dm(g_cfg);
std::map<std::wstring, TrayIcon> g_icons;
UINT   g_nextIconId = 1;
bool   g_appIconShown = false;
HICON  g_appIcon = nullptr;
HWND   g_settingsDlg = nullptr;

std::wstring Tooltip(const Device& d) {
    std::wstring t = d.Label() + L" — " + std::to_wstring(d.percent) + L"%";
    if (d.charging) t += L" (carregando)";
    return t;
}

TrayIcon& IconFor(const std::wstring& key) {
    TrayIcon& t = g_icons[key];
    if (!t.id) t.id = g_nextIconId++;
    return t;
}

// ---------------------------------------------------------------------------------------------
// Tray icons

void FillNid(NOTIFYICONDATAW& nid, UINT id) {
    nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = id;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.uVersion = NOTIFYICON_VERSION_4;
}

void ShowOrUpdate(UINT id, HICON icon, const std::wstring& tip, bool& shown) {
    NOTIFYICONDATAW nid;
    FillNid(nid, id);
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = icon;
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    if (!shown) {
        if (Shell_NotifyIconW(NIM_ADD, &nid)) {
            Shell_NotifyIconW(NIM_SETVERSION, &nid);
            shown = true;
        }
    } else {
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void Remove(UINT id, bool& shown) {
    if (!shown) return;
    NOTIFYICONDATAW nid;
    FillNid(nid, id);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    shown = false;
}

void Notify(UINT id, const Device& d) {
    NOTIFYICONDATAW nid;
    FillNid(nid, id);
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(nid.szInfoTitle, L"Bateria baixa");
    std::wstring msg = d.Label() + L": " + std::to_wstring(d.percent) + L"%";
    wcsncpy_s(nid.szInfo, msg.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void UpdateIcons(bool forceRedraw = false) {
    bool anyShown = false;
    for (auto& d : g_dm.Devices()) {
        TrayIcon& t = IconFor(d.key);
        bool want = d.present && d.percent >= 0 && !g_dm.IsHidden(d.key);
        if (!want) {
            Remove(t.id, t.shown);
            continue;
        }
        if (forceRedraw || !t.hIcon || t.drawnPercent != d.percent || t.drawnCharging != d.charging) {
            if (t.hIcon) DestroyIcon(t.hIcon);
            t.hIcon = MakeTrayIcon(d.kind, d.percent, d.charging, g_cfg.colorLevels);
            t.drawnPercent = d.percent;
            t.drawnCharging = d.charging;
        }
        ShowOrUpdate(t.id, t.hIcon, Tooltip(d), t.shown);
        anyShown = true;
    }
    for (Device* d : g_dm.TakeLowBatteryAlerts()) Notify(IconFor(d->key).id, *d);

    // Without any device icon there would be no way to reach the menu: show a neutral app icon.
    if (anyShown) {
        Remove(APP_ICON_ID, g_appIconShown);
    } else {
        if (forceRedraw || !g_appIcon) {
            if (g_appIcon) DestroyIcon(g_appIcon);
            g_appIcon = MakeTrayIcon(Kind::App, -1, false, false);
        }
        ShowOrUpdate(APP_ICON_ID, g_appIcon, g_dm.Scanning() ? L"Wireless Tray — procurando dispositivos..."
                                                            : L"Wireless Tray — nenhum dispositivo", g_appIconShown);
    }
}

void RemoveAllIcons() {
    for (auto& [k, t] : g_icons) Remove(t.id, t.shown);
    Remove(APP_ICON_ID, g_appIconShown);
}

void StartScan() { g_dm.StartScan(g_hwnd, WM_APP_SCANDONE); }
void RestartPollTimer() { SetTimer(g_hwnd, TIMER_POLL, g_cfg.intervalMin * 60u * 1000u, nullptr); }

// ---------------------------------------------------------------------------------------------
// Settings dialog

struct DlgState { std::vector<std::wstring> keys; };

void InitDeviceList(HWND list, DlgState& st) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    RECT rc;
    GetClientRect(list, &rc);
    LVCOLUMNW col{ LVCF_TEXT | LVCF_WIDTH };
    int w2 = MulDiv(80, GetDpiForWindow(list), 96);
    col.cx = rc.right - w2 - GetSystemMetrics(SM_CXVSCROLL);
    col.pszText = (LPWSTR)L"Dispositivo";
    ListView_InsertColumn(list, 0, &col);
    col.cx = w2;
    col.pszText = (LPWSTR)L"Bateria";
    ListView_InsertColumn(list, 1, &col);

    // Current devices first, then remembered ones that are offline.
    std::vector<std::pair<std::wstring, std::wstring>> rows;  // key, name
    for (auto& d : g_dm.Devices())
        if (!d.name.empty() || d.present) rows.emplace_back(d.key, d.Label());
    for (auto& [k, n] : g_cfg.known) {
        bool dup = false;
        for (auto& r : rows) if (r.first == k) { dup = true; break; }
        if (!dup) rows.emplace_back(k, n);
    }

    int i = 0;
    for (auto& [key, name] : rows) {
        st.keys.push_back(key);
        LVITEMW it{ LVIF_TEXT | LVIF_PARAM };
        it.iItem = i;
        it.pszText = (LPWSTR)name.c_str();
        it.lParam = i;
        ListView_InsertItem(list, &it);

        std::wstring status = L"desconectado";
        if (Device* d = g_dm.Find(key); d && d->present && d->percent >= 0)
            status = std::to_wstring(d->percent) + L"%" + (d->charging ? L" ⚡" : L"");
        ListView_SetItemText(list, i, 1, (LPWSTR)status.c_str());
        ListView_SetCheckState(list, i, !g_dm.IsHidden(key));
        ++i;
    }
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    static DlgState* st = nullptr;
    switch (msg) {
    case WM_INITDIALOG: {
        st = new DlgState;
        g_settingsDlg = dlg;
        InitDeviceList(GetDlgItem(dlg, IDC_DEVICES), *st);
        SendDlgItemMessageW(dlg, IDC_INTERVAL_SPIN, UDM_SETRANGE32, 1, 60);
        SendDlgItemMessageW(dlg, IDC_INTERVAL_SPIN, UDM_SETPOS32, 0, g_cfg.intervalMin);
        SendDlgItemMessageW(dlg, IDC_THRESHOLD_SPIN, UDM_SETRANGE32, 5, 50);
        SendDlgItemMessageW(dlg, IDC_THRESHOLD_SPIN, UDM_SETPOS32, 0, g_cfg.lowThreshold);
        CheckDlgButton(dlg, IDC_LOWWARN, g_cfg.lowWarning ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_AUTOSTART, AutostartEnabled(AUTOSTART_NAME) ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_COLORLEVELS, g_cfg.colorLevels ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(dlg, IDC_THRESHOLD), g_cfg.lowWarning);
        SetForegroundWindow(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_LOWWARN:
            EnableWindow(GetDlgItem(dlg, IDC_THRESHOLD), IsDlgButtonChecked(dlg, IDC_LOWWARN) == BST_CHECKED);
            return TRUE;
        case IDOK: {
            HWND list = GetDlgItem(dlg, IDC_DEVICES);
            for (int i = 0; i < (int)st->keys.size(); ++i) {
                bool visible = ListView_GetCheckState(list, i) != FALSE;
                if (visible) g_cfg.hidden.erase(st->keys[i]);
                else g_cfg.hidden[st->keys[i]] = true;
            }
            BOOL ok;
            int v = (int)GetDlgItemInt(dlg, IDC_INTERVAL, &ok, FALSE);
            if (ok) g_cfg.intervalMin = v < 1 ? 1 : v > 60 ? 60 : v;
            v = (int)GetDlgItemInt(dlg, IDC_THRESHOLD, &ok, FALSE);
            if (ok) g_cfg.lowThreshold = v < 5 ? 5 : v > 50 ? 50 : v;
            g_cfg.lowWarning = IsDlgButtonChecked(dlg, IDC_LOWWARN) == BST_CHECKED;
            g_cfg.colorLevels = IsDlgButtonChecked(dlg, IDC_COLORLEVELS) == BST_CHECKED;
            bool autostart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
            if (autostart != AutostartEnabled(AUTOSTART_NAME)) SetAutostart(AUTOSTART_NAME, autostart);
            g_cfg.Save();
            RestartPollTimer();
            UpdateIcons(true);
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
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, SettingsProc, 0);
}

// ---------------------------------------------------------------------------------------------
void ShowMenu(POINT pt) {
    HMENU m = CreatePopupMenu();
    bool any = false;
    for (auto& d : g_dm.Devices()) {
        if (!d.present || d.percent < 0) continue;
        AppendMenuW(m, MF_STRING | MF_DISABLED, 0, Tooltip(d).c_str());
        any = true;
    }
    if (!any) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"Nenhum dispositivo encontrado");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_dm.Scanning() ? MF_GRAYED : 0), ID_REFRESH, L"Atualizar agora");
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"Configurações...");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Sair");

    SetForegroundWindow(g_hwnd);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
        case ID_REFRESH:  StartScan(); break;
        case ID_SETTINGS: OpenSettings(); break;
        case ID_EXIT:     DestroyWindow(g_hwnd); break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreated && msg != 0) {   // Explorer restarted: re-add everything
        for (auto& [k, t] : g_icons) t.shown = false;
        g_appIconShown = false;
        UpdateIcons(true);
        return 0;
    }
    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT: {
            POINT pt{ (short)LOWORD(wp), (short)HIWORD(wp) };
            ShowMenu(pt);
            break;
        }
        }
        return 0;
    case WM_APP_SCANDONE:
        g_dm.OnScanDone((ScanResult*)lp);
        UpdateIcons();
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);  // trim RAM after the scan
        return 0;
    case WM_TIMER:
        if (wp == TIMER_SOON) KillTimer(hwnd, TIMER_SOON);
        StartScan();
        return 0;
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED) SetTimer(hwnd, TIMER_SOON, 3000, nullptr);
        return TRUE;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC) SetTimer(hwnd, TIMER_SOON, 5000, nullptr);
        return TRUE;
    case WM_SETTINGCHANGE:
        if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) UpdateIcons(true);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        UpdateIcons(true);
        return 0;
    case WM_DESTROY:
        RemoveAllIcons();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"WirelessTray.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g_cfg.Load();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"WirelessTrayWnd";
    RegisterClassExW(&wc);

    // A hidden top-level window (not message-only) so it receives WM_DEVICECHANGE broadcasts.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Wireless Tray", WS_POPUP,
                             0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_hwnd, g_taskbarCreated, MSGFLT_ALLOW, nullptr);

    StartScan();
    UpdateIcons();     // fallback icon while the first scan runs
    RestartPollTimer();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (g_settingsDlg && IsDialogMessageW(g_settingsDlg, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    for (auto& [k, t] : g_icons) if (t.hIcon) DestroyIcon(t.hIcon);
    if (g_appIcon) DestroyIcon(g_appIcon);
    if (mutex) CloseHandle(mutex);
    return 0;
}
