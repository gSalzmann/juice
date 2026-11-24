// Battery of JBL speakers (Flip, Charge, Xtreme, Go, ...) through JBL's Bluetooth LE control service,
// the same one the JBL Portable app uses. Protocol documented by github.com/ffrafat/JBLControl.
#pragma once
#include <string>
#include <vector>

struct JblDeviceInfo {
    std::wstring key;       // "jbl:<classic Bluetooth address>" (stable; the BLE address rotates)
    std::wstring name;
    int  percent = -1;
    bool charging = false;
};

// Listens for JBL advertisements for a couple of seconds, then connects to each speaker found and
// asks for its info. Blocking; call from a worker thread. `log` receives diagnostic lines (may be null).
std::vector<JblDeviceInfo> JblScan(void (*log)(const wchar_t*) = nullptr);
