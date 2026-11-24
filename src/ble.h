// Devices read over Bluetooth LE, in one short listening window per refresh:
//  - JBL speakers (Flip, Charge, Xtreme, Go, ...): found by their advertisement, then a connection to
//    JBL's control service, the same one the JBL Portable app uses (protocol documented by
//    github.com/ffrafat/JBLControl).
//  - AirPods and Beats: no connection; Apple's "proximity pairing" advertisement carries the level of
//    each earbud and of the case.
#pragma once
#include <string>
#include <vector>

struct BleDeviceInfo {
    std::wstring key;       // "jbl:<classic Bluetooth address>" or "apple:<model id>"
    std::wstring name;
    bool speaker = false;   // JBL speaker (else earbuds/headphones)
    int  percent = -1;
    bool charging = false;
    // Earbuds: left, right, case (-1 = unknown) and their charging flags.
    int  parts[3] = { -1, -1, -1 };
    bool partCharging[3] = { false, false, false };
};

// Listens for advertisements for a couple of seconds and reads the devices of the enabled kinds.
// Blocking; call from a worker thread. `log` receives diagnostic lines (may be null).
std::vector<BleDeviceInfo> BleScan(bool jbl, bool apple, void (*log)(const wchar_t*) = nullptr);
