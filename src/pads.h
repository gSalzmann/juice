// Battery of game controllers connected over Bluetooth (classic HID): Xbox Wireless Controller,
// DualShock 4 and DualSense. They have no battery property in Windows and cannot be asked for it:
// they send it in their own input reports (Xbox about every 20 s; Sony continuously), so a background
// thread keeps a read pending on each controller and remembers the last value.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

struct PadInfo {
    std::wstring key;       // "xbox:<Bluetooth address>" or "ps:<Bluetooth address>"
    std::wstring name;
    int  percent = -1;      // Xbox: 10/40/70/100 (4 levels); PlayStation: 5..100 in steps of 10; -1 until the first report
    bool charging = false;  // on USB power
};

// Starts the background reader. When a controller appears, disappears or changes level,
// `msg` is posted to `notify` (no parameters).
void PadsStart(HWND notify, UINT msg);
// Looks for new controllers now (call on device arrival).
void PadsRescan();
// Controllers currently connected, with their last known level. Thread-safe.
std::vector<PadInfo> PadsSnapshot();
// One-shot read for diagnostics: waits up to `timeoutMs` for each controller's report.
std::vector<PadInfo> PadsProbe(DWORD timeoutMs, void (*log)(const wchar_t*) = nullptr);
