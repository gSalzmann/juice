// Logitech HID++ 2.0 battery reader (receivers: Unifying/Bolt/LIGHTSPEED, and direct-connected devices).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct BatteryInfo {
    int  percent  = -1;    // 0..100, -1 = unknown
    bool charging = false;
    int  voltage_mV = 0;   // only for voltage-based features
};

struct HidppDeviceInfo {
    std::wstring key;      // stable id, e.g. "hidpp:046D:C547:1"
    std::wstring name;     // device marketing name (feature 0x0005)
    uint16_t     receiverPid = 0;
    uint8_t      index = 0;          // 1..6 on a receiver, 0xFF for direct devices
    uint16_t     batteryFeature = 0; // 0x1000 / 0x1001 / 0x1004 / 0 = none
    int          deviceType = -1;    // HID++ 0x0005 type: 0 keyboard, 3 mouse, 8 headset, ...
    bool         online = false;
    bool         noResponse = false; // timed out: usually the device is sleeping, keep last value
    BatteryInfo  battery;
};

// Scans every Logitech HID++ interface and returns the devices that answered.
// Safe to call from a worker thread. `log` receives diagnostic lines (may be null).
std::vector<HidppDeviceInfo> HidppScan(void (*log)(const wchar_t*) = nullptr);
