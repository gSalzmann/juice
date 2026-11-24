// Xbox Wireless Controller over Bluetooth: battery from input report 0x04.
//   Found as a HID gamepad (usage page 1 / usage 5) from Microsoft (VID 045E) whose descriptor has an
//   input value Generic Device Controls (page 0x06) / Battery Strength (usage 0x20) in report 0x04.
//   Value: bits 0-1 level (0..3 -> 10/40/70/100%, same mapping as SDL and GameInput),
//          bits 2-3 battery type (0 = powered by USB, i.e. charging).
//   HidD_GetInputReport(0x04) is rejected, so the report is only seen when the controller sends it
//   (about every 20 s, also while idle). Reading does not take the controller from games: HID
//   gamepads are opened shared.

#include "xbox.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <cstdio>
#include <memory>
#include <mutex>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr USHORT MS_VID = 0x045E;
constexpr DWORD  RESCAN_MS = 60 * 1000;   // look for new controllers at least this often

void (*g_log)(const wchar_t*) = nullptr;

void Log(const wchar_t* fmt, ...) {
    if (!g_log) return;
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log(buf);
}

struct Pad {
    std::wstring path, key, name;
    HANDLE h = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{};
    PHIDP_PREPARSED_DATA pp = nullptr;
    std::vector<BYTE> buf;
    int percent = -1;
    bool charging = false;
    bool pending = false;

    ~Pad() {
        if (h != INVALID_HANDLE_VALUE) {
            if (pending) {
                CancelIo(h);
                DWORD got;
                GetOverlappedResult(h, &ov, &got, TRUE);
            }
            CloseHandle(h);
        }
        if (ov.hEvent) CloseHandle(ov.hEvent);
        if (pp) HidD_FreePreparsedData(pp);
    }
    bool Arm() {
        ResetEvent(ov.hEvent);
        pending = ReadFile(h, buf.data(), (DWORD)buf.size(), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING;
        return pending;
    }
    // Returns true if the level changed.
    bool Decode(DWORD got) {
        ULONG v = 0;
        if (got == 0 || buf[0] != 0x04 ||
            HidP_GetUsageValue(HidP_Input, 0x06, 0, 0x20, &v, pp, (PCHAR)buf.data(), got) != HIDP_STATUS_SUCCESS)
            return false;
        static const int pct[] = { 10, 40, 70, 100 };
        const int p = pct[v & 3];
        const bool c = ((v >> 2) & 3) == 0;
        if (p == percent && c == charging) return false;
        percent = p;
        charging = c;
        Log(L"xbox %s raw=0x%02X -> %d%%%s", key.c_str(), v, p, c ? L" (charging)" : L"");
        return true;
    }
};

// Bluetooth address from the HID transport devnode (parent of the collection), e.g.
// BTHENUM\{00001124-...}_VID&0002045E_PID&02E0\8&...&0&C83F266F29A8_C00000000
std::wstring BtAddress(DEVINST collection) {
    DEVINST parent;
    wchar_t id[MAX_DEVICE_ID_LEN];
    if (CM_Get_Parent(&parent, collection, 0) != CR_SUCCESS || CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
        return L"";
    std::wstring s = id;
    if (s.rfind(L"BTHENUM\\", 0) != 0) return L"";
    const size_t us = s.rfind(L'_');
    const size_t amp = us == std::wstring::npos ? us : s.rfind(L'&', us);
    if (amp == std::wstring::npos || us - amp - 1 != 12) return L"";
    return s.substr(amp + 1, 12);
}

// Name the user gave the controller in Windows ("Xbox Wireless Controller" by default), from the
// Bluetooth device node BTHENUM\DEV_<address>.
std::wstring BtFriendlyName(const std::wstring& addr) {
    std::wstring name;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return name;
    const std::wstring prefix = L"BTHENUM\\DEV_" + addr + L"\\";
    SP_DEVINFO_DATA dev{ sizeof(dev) };
    for (DWORD i = 0; name.empty() && SetupDiEnumDeviceInfo(set, i, &dev); ++i) {
        wchar_t id[MAX_DEVICE_ID_LEN];
        if (CM_Get_Device_IDW(dev.DevInst, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS || _wcsnicmp(id, prefix.c_str(), prefix.size()))
            continue;
        wchar_t buf[256]{};
        DEVPROPTYPE type;
        if (SetupDiGetDevicePropertyW(set, &dev, &DEVPKEY_Device_FriendlyName, &type, (PBYTE)buf, sizeof(buf) - 2, nullptr, 0))
            name = buf;
    }
    SetupDiDestroyDeviceInfoList(set);
    return name;
}

// Opens every Xbox Bluetooth controller not already in `pads`.
void FindPads(std::vector<std::unique_ptr<Pad>>& pads) {
    GUID g;
    HidD_GetHidGuid(&g);
    HDEVINFO set = SetupDiGetClassDevsW(&g, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &g, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        std::vector<BYTE> mem(need);
        auto* det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(mem.data());
        det->cbSize = sizeof(*det);
        SP_DEVINFO_DATA dev{ sizeof(dev) };
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, &dev)) continue;
        bool known = false;
        for (auto& p : pads) known |= _wcsicmp(p->path.c_str(), det->DevicePath) == 0;
        if (known) continue;

        auto pad = std::make_unique<Pad>();
        pad->h = CreateFileW(det->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED, nullptr);
        if (pad->h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attr{ sizeof(attr) };
        HIDP_CAPS caps{};
        HIDP_VALUE_CAPS vc{};
        USHORT n = 1;
        if (!HidD_GetAttributes(pad->h, &attr) || attr.VendorID != MS_VID || !HidD_GetPreparsedData(pad->h, &pad->pp) ||
            HidP_GetCaps(pad->pp, &caps) != HIDP_STATUS_SUCCESS || caps.UsagePage != 0x01 || caps.Usage != 0x05 ||
            HidP_GetSpecificValueCaps(HidP_Input, 0x06, 0, 0x20, &vc, &n, pad->pp) != HIDP_STATUS_SUCCESS || n < 1 ||
            vc.ReportID != 0x04)
            continue;
        const std::wstring addr = BtAddress(dev.DevInst);
        if (addr.empty()) continue;   // USB-wired controllers are not batteries we can read here
        pad->path = det->DevicePath;
        pad->key = L"xbox:" + addr;
        pad->name = BtFriendlyName(addr);
        if (pad->name.empty()) pad->name = L"Xbox Wireless Controller";
        pad->buf.resize(caps.InputReportByteLength);
        pad->ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!pad->Arm()) continue;
        Log(L"xbox %s \"%s\" PID=%04X", pad->key.c_str(), pad->name.c_str(), attr.ProductID);
        pads.push_back(std::move(pad));
    }
    SetupDiDestroyDeviceInfoList(set);
}

// Waits for reports. Returns true if anything visible changed. Removes controllers whose read fails.
bool Pump(std::vector<std::unique_ptr<Pad>>& pads, HANDLE extra, DWORD timeoutMs, bool* extraSignaled) {
    HANDLE ev[MAXIMUM_WAIT_OBJECTS];
    DWORD n = 0;
    if (extra) ev[n++] = extra;
    for (auto& p : pads) if (n < MAXIMUM_WAIT_OBJECTS) ev[n++] = p->ov.hEvent;
    if (extraSignaled) *extraSignaled = false;
    if (!n) { Sleep(timeoutMs); return false; }
    const DWORD w = WaitForMultipleObjects(n, ev, FALSE, timeoutMs);
    if (w == WAIT_TIMEOUT || w >= WAIT_OBJECT_0 + n) return false;
    if (extra && w == WAIT_OBJECT_0) { if (extraSignaled) *extraSignaled = true; return false; }

    bool changed = false;
    for (size_t i = 0; i < pads.size();) {
        Pad& p = *pads[i];
        if (WaitForSingleObject(p.ov.hEvent, 0) != WAIT_OBJECT_0) { ++i; continue; }
        DWORD got = 0;
        p.pending = false;
        if (GetOverlappedResult(p.h, &p.ov, &got, FALSE)) {
            changed |= p.Decode(got);
            if (p.Arm()) { ++i; continue; }
        }
        Log(L"xbox %s gone", p.key.c_str());
        changed = true;
        pads.erase(pads.begin() + i);   // turned off or out of range
    }
    return changed;
}

std::mutex g_mutex;
std::vector<XboxDeviceInfo> g_state;
HANDLE g_rescan = nullptr;
HWND g_notify = nullptr;
UINT g_msg = 0;

void Publish(const std::vector<std::unique_ptr<Pad>>& pads) {
    std::vector<XboxDeviceInfo> s;
    for (auto& p : pads) s.push_back({ p->key, p->name, p->percent, p->charging });
    std::lock_guard<std::mutex> g(g_mutex);
    g_state = std::move(s);
}

DWORD WINAPI WatchThread(LPVOID) {
    std::vector<std::unique_ptr<Pad>> pads;
    ULONGLONG nextScan = 0;
    for (;;) {
        bool changed = false;
        if (GetTickCount64() >= nextScan) {
            const size_t before = pads.size();
            FindPads(pads);
            changed = pads.size() != before;
            nextScan = GetTickCount64() + RESCAN_MS;
        }
        bool rescan = false;
        changed |= Pump(pads, g_rescan, 5000, &rescan);
        if (rescan) nextScan = GetTickCount64() + 1500;   // give the new device a moment to settle
        if (changed) {
            Publish(pads);
            PostMessageW(g_notify, g_msg, 0, 0);
        }
    }
}

}  // namespace

void XboxStart(HWND notify, UINT msg) {
    if (g_rescan) return;
    g_notify = notify;
    g_msg = msg;
    g_rescan = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 64 * 1024, WatchThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (t) CloseHandle(t);
}

void XboxRescan() {
    if (g_rescan) SetEvent(g_rescan);
}

std::vector<XboxDeviceInfo> XboxSnapshot() {
    std::lock_guard<std::mutex> g(g_mutex);
    return g_state;
}

std::vector<XboxDeviceInfo> XboxProbe(DWORD timeoutMs, void (*log)(const wchar_t*)) {
    g_log = log;
    std::vector<std::unique_ptr<Pad>> pads;
    FindPads(pads);
    if (pads.empty()) Log(L"xbox: no Bluetooth controller");
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    for (;;) {
        bool all = true;
        for (auto& p : pads) all &= p->percent >= 0;
        const ULONGLONG now = GetTickCount64();
        if (all || now >= end) break;
        Pump(pads, nullptr, (DWORD)(end - now), nullptr);
    }
    std::vector<XboxDeviceInfo> out;
    for (auto& p : pads) out.push_back({ p->key, p->name, p->percent, p->charging });
    g_log = nullptr;
    return out;
}
