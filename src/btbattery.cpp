#include "btbattery.h"
#include "galaxy.h"

#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>
#include <cstdio>
#include <map>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2 : battery level (BYTE, 0..100) on Bluetooth device nodes.
const DEVPROPKEY PKEY_BtBattery = { { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } }, 2 };
// {83DA6326-97A6-4088-9453-A1923F573B29}, 15 : DEVPKEY_Device_IsConnected (BOOLEAN).
const DEVPROPKEY PKEY_IsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };
// {2BD67D8B-8BEB-48D5-87E0-6CDA3428040A}, 10 : DEVPKEY_Bluetooth_ClassOfDevice (UINT32).
const DEVPROPKEY PKEY_BtCod = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 10 };

template <class T>
bool GetProp(HDEVINFO set, SP_DEVINFO_DATA& dev, const DEVPROPKEY& key, T& out) {
    DEVPROPTYPE type;
    return SetupDiGetDevicePropertyW(set, &dev, &key, &type, (PBYTE)&out, sizeof(T), nullptr, 0) != FALSE;
}

std::wstring GetStringProp(HDEVINFO set, SP_DEVINFO_DATA& dev, const DEVPROPKEY& key) {
    wchar_t buf[256]{};
    DEVPROPTYPE type;
    if (!SetupDiGetDevicePropertyW(set, &dev, &key, &type, (PBYTE)buf, sizeof(buf) - sizeof(wchar_t), nullptr, 0))
        return L"";
    return buf;
}

std::wstring GuidStr(const GUID& g) {
    wchar_t s[64];
    StringFromGUID2(g, s, 64);
    return s;
}

constexpr ULONGLONG GALAXY_MIN_GAP_MS = 10ull * 60 * 1000;
constexpr ULONGLONG GALAXY_MAX_AGE_MS = 30ull * 60 * 1000;

}  // namespace

std::vector<BtDeviceInfo> BtBatteryScan(void (*log)(const wchar_t*), bool galaxy) {
    struct Acc { BtDeviceInfo info; bool hasBattery = false; bool isBt = false; };
    std::map<std::wstring, Acc> byContainer;

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return {};

    SP_DEVINFO_DATA dev{ sizeof(dev) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); ++i) {
        wchar_t id[512]{};
        if (!SetupDiGetDeviceInstanceIdW(set, &dev, id, 512, nullptr)) continue;
        bool btNode = _wcsnicmp(id, L"BTHENUM\\", 8) == 0 || _wcsnicmp(id, L"BTHLE\\", 6) == 0 ||
                      _wcsnicmp(id, L"BTHHFENUM\\", 10) == 0 || _wcsnicmp(id, L"BTHLEDEVICE\\", 12) == 0;
        if (!btNode) continue;

        GUID container{};
        if (!GetProp(set, dev, DEVPKEY_Device_ContainerId, container)) continue;
        Acc& a = byContainer[GuidStr(container)];
        if (_wcsnicmp(id, GALAXY_SERVICE_PREFIX, wcslen(GALAXY_SERVICE_PREFIX)) == 0) a.info.galaxy = true;
        if (_wcsnicmp(id, L"BTHENUM\\DEV_", 12) == 0) a.info.address = wcstoull(id + 12, nullptr, 16);

        BYTE level = 0;
        if (GetProp(set, dev, PKEY_BtBattery, level) && level <= 100) {
            a.hasBattery = true;
            a.info.percent = level;
        }
        // The device node itself (BTHENUM\DEV_xxx or BTHLE\DEV_xxx) carries name, class and connection state.
        if (_wcsnicmp(id, L"BTHENUM\\DEV_", 12) == 0 || _wcsnicmp(id, L"BTHLE\\DEV_", 10) == 0) {
            a.isBt = true;
            a.info.name = GetStringProp(set, dev, DEVPKEY_Device_FriendlyName);
            DEVPROP_BOOLEAN c = DEVPROP_FALSE;
            if (GetProp(set, dev, PKEY_IsConnected, c)) a.info.connected = c == DEVPROP_TRUE;
            UINT32 cod = 0;
            if (GetProp(set, dev, PKEY_BtCod, cod)) {
                a.info.majorClass = (cod >> 8) & 0x1F;
                a.info.minorClass = (cod >> 2) & 0x3F;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(set);

    std::vector<BtDeviceInfo> out;
    for (auto& [guid, a] : byContainer) {
        if (!a.isBt || !a.hasBattery) continue;
        a.info.key = L"bt:" + guid;
        // Galaxy Buds: opening their channel over and over while they play audio froze a Buds2 once, so
        // it is opened only when the level Windows reports changes, or every 30 minutes, and never twice
        // within 10 minutes; in between the last reading is reused.
        struct GalaxyCache { int winPercent = -1; ULONGLONG tick = 0; bool ok = false; GalaxyInfo info; };
        static std::map<unsigned long long, GalaxyCache> cache;   // scans run one at a time
        GalaxyInfo g;
        bool haveGalaxy = false;
        if (galaxy && a.info.galaxy && a.info.address) {
            if (!a.info.connected) {
                cache.erase(a.info.address);
            } else {
                const ULONGLONG now = GetTickCount64();
                auto it = cache.find(a.info.address);
                const bool due = it == cache.end() ||
                                 (now - it->second.tick >= GALAXY_MIN_GAP_MS &&
                                  (it->second.winPercent != a.info.percent || now - it->second.tick >= GALAXY_MAX_AGE_MS));
                if (due) {
                    GalaxyCache& c = cache[a.info.address];
                    c.winPercent = a.info.percent;
                    c.tick = now;
                    c.ok = GalaxyRead(a.info.address, c.info, log);
                    it = cache.find(a.info.address);
                }
                if (it->second.ok) { g = it->second.info; haveGalaxy = true; }
            }
        }
        if (haveGalaxy) {
            // One value for the row: the earbud that runs out first (as with AirPods).
            int low = -1;
            bool allCharging = true;
            for (int i = 0; i < 3; ++i) {
                a.info.parts[i] = g.parts[i];
                a.info.partCharging[i] = g.partCharging[i];
                if (i < 2 && g.parts[i] >= 0) {
                    if (low < 0 || g.parts[i] < low) low = g.parts[i];
                    allCharging &= g.partCharging[i];
                }
            }
            if (low >= 0) { a.info.percent = low; a.info.charging = allCharging; }
        }
        if (log) {
            wchar_t line[400];
            swprintf_s(line, L"bt %s \"%s\" connected=%d battery=%d%% class=%d/%d", guid.c_str(),
                       a.info.name.c_str(), a.info.connected, a.info.percent, a.info.majorClass, a.info.minorClass);
            log(line);
        }
        out.push_back(a.info);
    }
    return out;
}
