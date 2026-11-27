// Battery of classic Bluetooth audio devices (Galaxy Buds, AirPods, etc.) as reported by
// the Windows Bluetooth stack (HFP battery indicators) through a device property.
#pragma once
#include <string>
#include <vector>

struct BtDeviceInfo {
    std::wstring key;       // "bt:<container guid>"
    std::wstring name;
    int  majorClass = -1;   // Bluetooth Class-of-Device major: 2 phone, 4 audio, 5 peripheral
    int  minorClass = 0;
    bool connected = false;
    int  percent = -1;
    bool charging = false;
    unsigned long long address = 0;   // classic Bluetooth address
    bool galaxy = false;              // Samsung Galaxy Buds (has the GEARMANAGER service, galaxy.h)
    // Earbuds read per side (galaxy.h): left, right, case (-1 = unknown).
    int  parts[3] = { -1, -1, -1 };
    bool partCharging[3] = { false, false, false };
};

// galaxy: also read each earbud of connected Galaxy Buds over their own channel (galaxy.h).
std::vector<BtDeviceInfo> BtBatteryScan(void (*log)(const wchar_t*) = nullptr, bool galaxy = false);
