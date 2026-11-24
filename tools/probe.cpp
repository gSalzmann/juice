// Console diagnostic: lists every HID++ / Bluetooth device found and its battery.
#include "../src/btbattery.h"
#include "../src/hidpp.h"
#include <cstdio>
#include <fcntl.h>
#include <io.h>

static void LogLine(const wchar_t* s) { wprintf(L"%s\n", s); }

int wmain() {
    _setmode(_fileno(stdout), _O_U8TEXT);
    auto devs = HidppScan(LogLine);
    auto bts = BtBatteryScan(LogLine);
    wprintf(L"\n=== %zu HID++ device(s), %zu Bluetooth device(s) ===\n", devs.size(), bts.size());
    for (auto& d : devs) {
        wprintf(L"%-20s %-38s online=%d asleep=%d type=%d feature=0x%04X battery=%d%% charging=%d\n",
                d.key.c_str(), d.name.c_str(), d.online, d.noResponse, d.deviceType, d.batteryFeature,
                d.battery.percent, d.battery.charging);
    }
    for (auto& b : bts) {
        wprintf(L"%-20s %-38s connected=%d battery=%d%%\n", b.key.c_str(), b.name.c_str(), b.connected, b.percent);
    }
    return 0;
}
