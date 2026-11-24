// Battery of Xbox Wireless Controllers connected over Bluetooth (classic HID). The controller has no
// battery property in Windows and cannot be asked for it: it sends an input report (ID 0x04,
// Generic Device Controls / Battery Strength) on its own about every 20 s, so a background thread
// keeps a read pending on each controller and remembers the last value.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

struct XboxDeviceInfo {
    std::wstring key;       // "xbox:<Bluetooth address>"
    std::wstring name;
    int  percent = -1;      // 10 / 40 / 70 / 100 (the controller reports 4 levels); -1 until the first report
    bool charging = false;  // on USB power
};

// Starts the background reader. When a controller appears, disappears or changes level,
// `msg` is posted to `notify` (no parameters).
void XboxStart(HWND notify, UINT msg);
// Looks for new controllers now (call on device arrival).
void XboxRescan();
// Controllers currently connected, with their last known level. Thread-safe.
std::vector<XboxDeviceInfo> XboxSnapshot();
// One-shot read for diagnostics: waits up to `timeoutMs` for each controller's report.
std::vector<XboxDeviceInfo> XboxProbe(DWORD timeoutMs, void (*log)(const wchar_t*) = nullptr);
