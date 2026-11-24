#include "btbattery.h"

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

}  // namespace

std::vector<BtDeviceInfo> BtBatteryScan(void (*log)(const wchar_t*)) {
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
