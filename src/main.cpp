// Wireless Tray: shows battery levels of wireless mice/keyboards/headsets in the notification area.
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dbt.h>

#include <memory>
#include <string>
#include <vector>

#include "btbattery.h"
#include "config.h"
#include "hidpp.h"
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
constexpr ULONGLONG SLEEP_GRACE_MS = 60ull * 60 * 1000;  // keep a sleeping device visible for 1h

enum MenuId : UINT { ID_REFRESH = 100, ID_SETTINGS, ID_EXIT };

struct Device {
    std::wstring key, name;
    Kind  kind = Kind::Other;
    int   percent = -1;
    bool  charging = false;
    bool  present = false;        // should be displayed (online, or sleeping within grace period)
    ULONGLONG lastGood = 0;       // tick of the last successful reading
    bool  lowNotified = false;

    UINT  iconId = 0;
    bool  shown = false;          // icon currently in the tray
    HICON hIcon = nullptr;
    int   drawnPercent = -2;
    bool  drawnCharging = false;
};

struct ScanResult {
    std::vector<HidppDeviceInfo> hid;
    std::vector<BtDeviceInfo>    bt;
};

HWND   g_hwnd;
UINT   g_taskbarCreated;
Config g_cfg;
std::vector<Device> g_devices;
UINT   g_nextIconId = 1;
bool   g_scanning = false, g_rescan = false;
bool   g_appIconShown = false;
HICON  g_appIcon = nullptr;
HWND   g_settingsDlg = nullptr;

// ---------------------------------------------------------------------------------------------
Kind KindFromHidpp(int t) {
    switch (t) {
        case 0: case 2:         return Kind::Keyboard;
        case 3: case 4: case 5: return Kind::Mouse;
        case 8:                 return Kind::Headset;
        default:                return Kind::Other;
    }
}

Kind KindFromBt(int major, int minor) {
    if (major == 4) return Kind::Headset;            // audio/video
    if (major == 5) {                                 // peripheral
        int sub = (minor >> 4) & 3;
        if (sub == 2) return Kind::Mouse;
        if (sub == 1 || sub == 3) return Kind::Keyboard;
    }
    return Kind::Other;
}

Device& FindOrAdd(const std::wstring& key) {
    for (auto& d : g_devices) if (d.key == key) return d;
    Device d;
    d.key = key;
    d.iconId = g_nextIconId++;
    auto it = g_cfg.known.find(key);
    if (it != g_cfg.known.end()) d.name = it->second;
    g_devices.push_back(d);
    return g_devices.back();
}

bool IsHidden(const std::wstring& key) {
    auto it = g_cfg.hidden.find(key);
    return it != g_cfg.hidden.end() && it->second;
}

std::wstring Tooltip(const Device& d) {
    std::wstring t = d.name.empty() ? d.key : d.name;
    t += L" — " + std::to_wstring(d.percent) + L"%";
    if (d.charging) t += L" (carregando)";
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

void Notify(const Device& d) {
    NOTIFYICONDATAW nid;
    FillNid(nid, d.iconId);
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(nid.szInfoTitle, L"Bateria baixa");
    std::wstring msg = (d.name.empty() ? d.key : d.name) + L": " + std::to_wstring(d.percent) + L"%";
    wcsncpy_s(nid.szInfo, msg.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void UpdateIcons(bool forceRedraw = false) {
    bool anyShown = false;
    for (auto& d : g_devices) {
        bool want = d.present && d.percent >= 0 && !IsHidden(d.key);
        if (!want) {
            Remove(d.iconId, d.shown);
            continue;
        }
        if (forceRedraw || !d.hIcon || d.drawnPercent != d.percent || d.drawnCharging != d.charging) {
            if (d.hIcon) DestroyIcon(d.hIcon);
            d.hIcon = MakeTrayIcon(d.kind, d.percent, d.charging, g_cfg.lowThreshold);
            d.drawnPercent = d.percent;
            d.drawnCharging = d.charging;
        }
        ShowOrUpdate(d.iconId, d.hIcon, Tooltip(d), d.shown);
        anyShown = true;

        // Low battery warning, once per discharge cycle.
        if (d.charging || d.percent > g_cfg.lowThreshold + 5) d.lowNotified = false;
        if (g_cfg.lowWarning && !d.charging && d.percent <= g_cfg.lowThreshold && !d.lowNotified) {
            Notify(d);
            d.lowNotified = true;
        }
    }

    // Without any device icon there would be no way to reach the menu: show a neutral app icon.
    if (anyShown) {
        Remove(APP_ICON_ID, g_appIconShown);
    } else {
        if (forceRedraw || !g_appIcon) {
            if (g_appIcon) DestroyIcon(g_appIcon);
            g_appIcon = MakeTrayIcon(Kind::App, -1, false, 0);
        }
        ShowOrUpdate(APP_ICON_ID, g_appIcon, g_scanning ? L"Wireless Tray — procurando dispositivos..."
                                                        : L"Wireless Tray — nenhum dispositivo", g_appIconShown);
    }
}

void RemoveAllIcons() {
    for (auto& d : g_devices) Remove(d.iconId, d.shown);
    Remove(APP_ICON_ID, g_appIconShown);
}

// ---------------------------------------------------------------------------------------------
// Scanning (runs on a worker thread; results are posted back to the UI thread)

DWORD WINAPI ScanThread(LPVOID) {
    auto* r = new ScanResult;
    r->hid = HidppScan();
    r->bt  = BtBatteryScan();
    if (!PostMessageW(g_hwnd, WM_APP_SCANDONE, 0, (LPARAM)r)) delete r;
    return 0;
}

void StartScan() {
    if (g_scanning) { g_rescan = true; return; }
    g_scanning = true;
    HANDLE t = CreateThread(nullptr, 64 * 1024, ScanThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (t) CloseHandle(t); else g_scanning = false;
}

void ApplyScan(std::unique_ptr<ScanResult> r) {
    const ULONGLONG now = GetTickCount64();
    std::vector<std::wstring> seen;
    bool knownChanged = false;

    auto remember = [&](Device& d) {
        if (!d.name.empty() && g_cfg.known[d.key] != d.name) { g_cfg.known[d.key] = d.name; knownChanged = true; }
    };

    for (auto& h : r->hid) {
        Device& d = FindOrAdd(h.key);
        seen.push_back(h.key);
        if (h.online && h.battery.percent >= 0) {
            if (!h.name.empty()) d.name = h.name;
            d.kind = KindFromHidpp(h.deviceType);
            d.percent = h.battery.percent;
            d.charging = h.battery.charging;
            d.lastGood = now;
            d.present = true;
        } else if (h.noResponse) {
            d.present = d.lastGood && now - d.lastGood < SLEEP_GRACE_MS;
        } else {
            d.present = false;
        }
        remember(d);
    }
    for (auto& b : r->bt) {
        Device& d = FindOrAdd(b.key);
        seen.push_back(b.key);
        if (!b.name.empty()) d.name = b.name;
        d.kind = KindFromBt(b.majorClass, b.minorClass);
        d.present = b.connected && b.percent >= 0;
        if (d.present) { d.percent = b.percent; d.charging = false; d.lastGood = now; }
        remember(d);
    }
    // Devices whose receiver/adapter disappeared entirely.
    for (auto& d : g_devices) {
        bool found = false;
        for (auto& k : seen) if (k == d.key) { found = true; break; }
        if (!found) d.present = false;
    }

    if (knownChanged) g_cfg.Save();
    UpdateIcons();
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);  // trim RAM after the scan
}

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
    for (auto& d : g_devices)
        if (!d.name.empty() || d.present) rows.emplace_back(d.key, d.name.empty() ? d.key : d.name);
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
        for (auto& d : g_devices)
            if (d.key == key && d.present && d.percent >= 0)
                status = std::to_wstring(d.percent) + L"%" + (d.charging ? L" ⚡" : L"");
        ListView_SetItemText(list, i, 1, (LPWSTR)status.c_str());
        ListView_SetCheckState(list, i, !IsHidden(key));
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
        CheckDlgButton(dlg, IDC_AUTOSTART, AutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);
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
            bool autostart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
            if (autostart != AutostartEnabled()) SetAutostart(autostart);
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
    for (auto& d : g_devices) {
        if (!d.present || d.percent < 0) continue;
        AppendMenuW(m, MF_STRING | MF_DISABLED, 0, Tooltip(d).c_str());
        any = true;
    }
    if (!any) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"Nenhum dispositivo encontrado");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_scanning ? MF_GRAYED : 0), ID_REFRESH, L"Atualizar agora");
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
        for (auto& d : g_devices) d.shown = false;
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
        g_scanning = false;
        ApplyScan(std::unique_ptr<ScanResult>((ScanResult*)lp));
        if (g_rescan) { g_rescan = false; StartScan(); }
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

    for (auto& d : g_devices) if (d.hIcon) DestroyIcon(d.hIcon);
    if (g_appIcon) DestroyIcon(g_appIcon);
    if (mutex) CloseHandle(mutex);
    return 0;
}
