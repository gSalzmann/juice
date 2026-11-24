// Logitech HID++ 2.0 battery reader.
//
// Protocol notes:
//   short report: [0x10, devIdx, subId/featIdx, func|swId, p0, p1, p2]            (7 bytes)
//   long  report: [0x11, devIdx, featIdx,       func|swId, p0 .. p15]             (20 bytes)
//   HID++ 2.0 error: [0x11, devIdx, 0xFF, featIdx, func|swId, errCode, ...]
//   HID++ 1.0 error: [0x10, devIdx, 0x8F, subId,   reg|swId,  errCode, 0]
// On Windows each top-level collection (short / long) is a separate HID device path,
// so we open both collections of the same interface and read from both.

#include "hidpp.h"

#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <cstdio>
#include <cwchar>
#include <map>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr uint16_t LOGITECH_VID = 0x046D;
constexpr uint8_t  SW_ID        = 0x0A;   // non-zero so we never confuse replies with notifications
constexpr DWORD    TIMEOUT_MS   = 600;

constexpr uint8_t  ERR1_UNKNOWN_DEVICE = 0x08;  // HID++ 1.0: no device paired on this slot
constexpr uint8_t  ERR1_RESOURCE       = 0x09;  // HID++ 1.0: device paired but not reachable

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

// ---------------------------------------------------------------------------------------------
// HID collection with an overlapped read always pending.
struct Collection {
    HANDLE     h = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{};
    uint8_t    buf[64]{};
    DWORD      inLen = 0;
    DWORD      outLen = 0;
    bool       pending = false;

    bool Open(const std::wstring& path, DWORD in, DWORD out) {
        h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        inLen = in > sizeof(buf) ? sizeof(buf) : in;
        outLen = out;
        return true;
    }
    bool IsOpen() const { return h != INVALID_HANDLE_VALUE; }

    // Make sure a read is outstanding. Returns false on hard error.
    bool Arm() {
        if (!IsOpen() || pending) return IsOpen();
        ResetEvent(ov.hEvent);
        if (ReadFile(h, buf, inLen, nullptr, &ov) || GetLastError() == ERROR_IO_PENDING) {
            pending = true;
            return true;
        }
        return false;
    }
    // Called after the event fired. Copies the report out and re-arms.
    DWORD Take(uint8_t* out) {
        DWORD got = 0;
        pending = false;
        if (!GetOverlappedResult(h, &ov, &got, FALSE)) got = 0;
        if (got) memcpy(out, buf, got);
        return got;
    }
    void Close() {
        if (!IsOpen()) return;
        if (pending) {
            CancelIo(h);
            DWORD dummy;
            GetOverlappedResult(h, &ov, &dummy, TRUE);
        }
        CloseHandle(ov.hEvent);
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
        pending = false;
    }
};

struct Interface {
    std::wstring parentId;   // USB interface instance id (groups the collections)
    std::wstring container;  // container id of the physical device ("{GUID}")
    uint16_t     pid = 0;
    std::wstring shortPath, longPath;
    DWORD shortIn = 0, shortOut = 0, longIn = 0, longOut = 0;
};

// ---------------------------------------------------------------------------------------------
class Channel {
public:
    explicit Channel(const Interface& itf) {
        if (!itf.shortPath.empty()) s_.Open(itf.shortPath, itf.shortIn, itf.shortOut);
        if (!itf.longPath.empty())  l_.Open(itf.longPath, itf.longIn, itf.longOut);
    }
    ~Channel() { s_.Close(); l_.Close(); }
    bool Ok() const { return l_.IsOpen(); }

    enum Result { OK, ERR_HIDPP1, ERR_HIDPP2, TIMEOUT, IO_FAIL };

    // Sends a long request and waits for the matching reply. `params` receives 16 bytes.
    Result Request(uint8_t dev, uint8_t feat, uint8_t func, const uint8_t* p, size_t np,
                   uint8_t* params, uint8_t* errCode = nullptr) {
        uint8_t req[64]{};
        req[0] = 0x11; req[1] = dev; req[2] = feat; req[3] = (uint8_t)((func << 4) | SW_ID);
        if (p && np) memcpy(req + 4, p, np > 16 ? 16 : np);

        if (!s_.Arm() && s_.IsOpen()) return IO_FAIL;
        if (!l_.Arm()) return IO_FAIL;

        OVERLAPPED wov{};
        wov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD written = 0;
        BOOL ok = WriteFile(l_.h, req, l_.outLen, nullptr, &wov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(wov.hEvent, TIMEOUT_MS) != WAIT_OBJECT_0) { CancelIo(l_.h); }
            ok = GetOverlappedResult(l_.h, &wov, &written, TRUE);
        }
        CloseHandle(wov.hEvent);
        if (!ok) return IO_FAIL;

        const uint8_t fsw = req[3];
        ULONGLONG deadline = GetTickCount64() + TIMEOUT_MS;
        for (;;) {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline) return TIMEOUT;
            HANDLE ev[2]; Collection* c[2]; int n = 0;
            if (l_.IsOpen()) { ev[n] = l_.ov.hEvent; c[n++] = &l_; }
            if (s_.IsOpen()) { ev[n] = s_.ov.hEvent; c[n++] = &s_; }
            DWORD w = WaitForMultipleObjects(n, ev, FALSE, (DWORD)(deadline - now));
            if (w == WAIT_TIMEOUT) return TIMEOUT;
            if (w >= WAIT_OBJECT_0 + (DWORD)n) return IO_FAIL;
            Collection* src = c[w - WAIT_OBJECT_0];
            uint8_t r[64]{};
            DWORD len = src->Take(r);
            src->Arm();
            if (len < 7 || r[1] != dev) continue;

            if (r[0] == 0x11 && r[2] == feat && r[3] == fsw) {
                memcpy(params, r + 4, 16);
                return OK;
            }
            if (r[0] == 0x11 && r[2] == 0xFF && r[3] == feat && r[4] == fsw) {
                if (errCode) *errCode = r[5];
                return ERR_HIDPP2;
            }
            if (r[0] == 0x10 && r[2] == 0x8F && r[3] == feat && r[4] == fsw) {
                if (errCode) *errCode = r[5];
                return ERR_HIDPP1;
            }
        }
    }

private:
    Collection s_, l_;
};

// ---------------------------------------------------------------------------------------------
std::vector<Interface> EnumerateInterfaces() {
    std::map<std::wstring, Interface> byParent;

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return {};

    SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        std::vector<uint8_t> mem(need);
        auto* det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(mem.data());
        det->cbSize = sizeof(*det);
        SP_DEVINFO_DATA dev{ sizeof(dev) };
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, &dev)) continue;

        std::wstring path = det->DevicePath;
        HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attr{ sizeof(attr) };
        HIDP_CAPS caps{};
        PHIDP_PREPARSED_DATA pp = nullptr;
        bool good = HidD_GetAttributes(h, &attr) && attr.VendorID == LOGITECH_VID &&
                    HidD_GetPreparsedData(h, &pp);
        if (good) {
            good = HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage >= 0xFF00;
            HidD_FreePreparsedData(pp);
        }
        CloseHandle(h);
        if (!good) continue;

        // Group collections by their parent (the USB/BT interface).
        DEVINST parent = 0;
        wchar_t parentId[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Parent(&parent, dev.DevInst, 0) != CR_SUCCESS ||
            CM_Get_Device_IDW(parent, parentId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
            continue;

        Interface& itf = byParent[parentId];
        itf.parentId = parentId;
        GUID cid{};
        DEVPROPTYPE type = 0;
        ULONG size = sizeof(cid);
        if (itf.container.empty() &&
            CM_Get_DevNode_PropertyW(parent, &DEVPKEY_Device_ContainerId, &type, (PBYTE)&cid, &size, 0) == CR_SUCCESS &&
            type == DEVPROP_TYPE_GUID) {
            wchar_t s[64];
            StringFromGUID2(cid, s, 64);
            itf.container = s;
        }
        itf.pid = attr.ProductID;
        if (caps.OutputReportByteLength == 7) {
            itf.shortPath = path; itf.shortIn = caps.InputReportByteLength; itf.shortOut = caps.OutputReportByteLength;
        } else if (caps.OutputReportByteLength == 20) {
            itf.longPath = path; itf.longIn = caps.InputReportByteLength; itf.longOut = caps.OutputReportByteLength;
        }
    }
    SetupDiDestroyDeviceInfoList(set);

    std::vector<Interface> out;
    for (auto& [k, v] : byParent)
        if (!v.longPath.empty()) out.push_back(v);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Feature helpers

// Returns feature index for a feature id, or 0 if unsupported.
uint8_t GetFeatureIndex(Channel& ch, uint8_t dev, uint16_t featureId) {
    uint8_t p[2] = { (uint8_t)(featureId >> 8), (uint8_t)featureId };
    uint8_t r[16];
    if (ch.Request(dev, 0x00, 0, p, 2, r) != Channel::OK) return 0;
    return r[0];
}

std::wstring ReadName(Channel& ch, uint8_t dev, uint8_t fi) {
    uint8_t r[16];
    if (ch.Request(dev, fi, 0, nullptr, 0, r) != Channel::OK) return L"";
    int len = r[0];
    std::string name;
    while ((int)name.size() < len) {
        uint8_t p[1] = { (uint8_t)name.size() };
        if (ch.Request(dev, fi, 1, p, 1, r) != Channel::OK) break;
        for (int i = 0; i < 16 && (int)name.size() < len; ++i) {
            if (!r[i]) { len = (int)name.size(); break; }
            name.push_back((char)r[i]);
        }
    }
    int wl = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), (int)name.size(), nullptr, 0);
    std::wstring w(wl, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), (int)name.size(), w.data(), wl);
    return w;
}

// Approximate Li-ion discharge curve used by Logitech devices (mV -> %).
int VoltageToPercent(int mv) {
    static const int table[][2] = {
        {4186, 100}, {4067, 90}, {3989, 80}, {3922, 70}, {3859, 60}, {3811, 50},
        {3778, 40},  {3751, 30}, {3717, 20}, {3671, 10}, {3646, 5},  {3579, 2}, {3500, 0},
    };
    if (mv >= table[0][0]) return 100;
    for (size_t i = 1; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (mv >= table[i][0]) {
            int v0 = table[i][0], p0 = table[i][1], v1 = table[i - 1][0], p1 = table[i - 1][1];
            return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 0;
}

bool ReadBattery(Channel& ch, uint8_t dev, HidppDeviceInfo& d) {
    uint8_t r[16];
    uint8_t fi;

    if ((fi = GetFeatureIndex(ch, dev, 0x1004))) {           // UNIFIED_BATTERY
        d.batteryFeature = 0x1004;
        bool hasSoc = true;
        if (ch.Request(dev, fi, 0, nullptr, 0, r) == Channel::OK)   // getCapabilities
            hasSoc = (r[1] & 0x02) != 0;
        if (ch.Request(dev, fi, 1, nullptr, 0, r) != Channel::OK) return false;  // getStatus
        if (hasSoc) d.battery.percent = r[0];
        else d.battery.percent = (r[1] & 8) ? 100 : (r[1] & 4) ? 50 : (r[1] & 2) ? 20 : (r[1] & 1) ? 5 : -1;
        d.battery.charging = r[2] == 1 || r[2] == 2;
        Log(L"    0x1004 soc=%u levels=0x%02X charging=%u ext=%u", r[0], r[1], r[2], r[3]);
        return true;
    }
    if ((fi = GetFeatureIndex(ch, dev, 0x1000))) {           // BATTERY_STATUS
        d.batteryFeature = 0x1000;
        if (ch.Request(dev, fi, 0, nullptr, 0, r) != Channel::OK) return false;
        d.battery.percent = r[0];
        d.battery.charging = r[2] == 1 || r[2] == 2 || r[2] == 4;
        Log(L"    0x1000 level=%u next=%u status=%u", r[0], r[1], r[2]);
        return true;
    }
    if ((fi = GetFeatureIndex(ch, dev, 0x1001))) {           // BATTERY_VOLTAGE
        d.batteryFeature = 0x1001;
        if (ch.Request(dev, fi, 0, nullptr, 0, r) != Channel::OK) return false;
        d.battery.voltage_mV = (r[0] << 8) | r[1];
        d.battery.percent = VoltageToPercent(d.battery.voltage_mV);
        d.battery.charging = (r[2] & 0x80) != 0;
        Log(L"    0x1001 voltage=%dmV flags=0x%02X", d.battery.voltage_mV, r[2]);
        return true;
    }
    if ((fi = GetFeatureIndex(ch, dev, 0x1F20))) {           // ADC_MEASUREMENT (G933/G935/G533 headsets)
        d.batteryFeature = 0x1F20;
        uint8_t err = 0;
        auto res = ch.Request(dev, fi, 0, nullptr, 0, r, &err);
        if (res != Channel::OK) {
            Log(L"    0x1F20 (index %u) failed: result=%d err=0x%02X", fi, (int)res, err);
            return false;
        }
        d.battery.voltage_mV = (r[0] << 8) | r[1];
        Log(L"    0x1F20 voltage=%dmV flags=0x%02X", d.battery.voltage_mV, r[2]);
        if (!(r[2] & 0x01)) return false;                    // measurement not valid (headset off)
        d.battery.percent = VoltageToPercent(d.battery.voltage_mV);
        d.battery.charging = (r[2] & 0x02) != 0;
        return true;
    }
    Log(L"    no known battery feature");
    return false;
}

void ProbeDevice(Channel& ch, const Interface& itf, uint8_t dev, std::vector<HidppDeviceInfo>& out) {
    uint8_t ping[3] = { 0, 0, 0xAA };
    uint8_t r[16], err = 0;
    auto res = ch.Request(dev, 0x00, 1, ping, 3, r, &err);
    if (res == Channel::ERR_HIDPP1 && err == ERR1_UNKNOWN_DEVICE) return;  // empty slot

    HidppDeviceInfo d;
    wchar_t key[64];
    if (dev == 0xFF) {
        swprintf_s(key, L"hidpp:%04X:%u", itf.pid, dev);
    } else {
        // Two receivers of the same model (e.g. two LIGHTSPEED dongles) must not share keys.
        uint32_t h = 2166136261u;                        // FNV-1a of the receiver instance id
        for (wchar_t c : itf.parentId) { h ^= (uint32_t)towupper(c); h *= 16777619u; }
        swprintf_s(key, L"hidpp:%04X:%u:%08X", itf.pid, dev, h);
    }
    d.key = key;
    d.container = itf.container;
    d.receiverPid = itf.pid;
    d.index = dev;

    if (res == Channel::TIMEOUT) {      // paired device that is asleep (or out of range)
        Log(L"  [%u] no response (asleep?)", dev);
        d.noResponse = true;
        out.push_back(d);
        return;
    }

    if (res != Channel::OK) {
        Log(L"  [%u] offline (result=%d err=0x%02X)", dev, (int)res, err);
        d.online = false;
        out.push_back(d);
        return;
    }
    Log(L"  [%u] HID++ %u.%u", dev, r[0], r[1]);
    d.online = true;

    if (uint8_t fi = GetFeatureIndex(ch, dev, 0x0005)) {      // DEVICE_NAME
        d.name = ReadName(ch, dev, fi);
        if (ch.Request(dev, fi, 2, nullptr, 0, r) == Channel::OK) d.deviceType = r[0];
    }
    if (uint8_t fi = GetFeatureIndex(ch, dev, 0x0003)) {      // DEVICE_INFORMATION: unit id (serial)
        if (ch.Request(dev, fi, 0, nullptr, 0, r) == Channel::OK) {
            uint32_t unit = (uint32_t)r[1] << 24 | r[2] << 16 | r[3] << 8 | r[4];
            if (unit && unit != 0xFFFFFFFF) {
                wchar_t u[32];
                swprintf_s(u, L"hidpp:%08X", unit);
                d.unit = u;
            }
        }
    }
    Log(L"    name=\"%s\" type=%d unit=%s container=%s", d.name.c_str(), d.deviceType,
        d.unit.empty() ? L"-" : d.unit.c_str() + 6, d.container.c_str());
    ReadBattery(ch, dev, d);
    out.push_back(d);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
std::vector<HidppDeviceInfo> HidppScan(void (*log)(const wchar_t*)) {
    g_log = log;
    std::vector<HidppDeviceInfo> out;

    for (const Interface& itf : EnumerateInterfaces()) {
        Log(L"interface %s (short=%s)", itf.parentId.c_str(), itf.shortPath.empty() ? L"no" : L"yes");
        Channel ch(itf);
        if (!ch.Ok()) { Log(L"  cannot open (error %lu)", GetLastError()); continue; }

        // A HID++ 2.0 device answers ping on index 0xFF directly; a receiver answers with a 1.0 error.
        uint8_t ping[3] = { 0, 0, 0xAA }, r[16], err = 0;
        auto res = ch.Request(0xFF, 0x00, 1, ping, 3, r, &err);
        if (res == Channel::OK && r[0] >= 2) {
            Log(L"  direct device");
            ProbeDevice(ch, itf, 0xFF, out);
        } else {
            Log(L"  receiver (ping result=%d err=0x%02X)", (int)res, err);
            for (uint8_t dev = 1; dev <= 6; ++dev) ProbeDevice(ch, itf, dev, out);
        }
    }
    g_log = nullptr;
    return out;
}
