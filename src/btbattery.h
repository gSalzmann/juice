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
};

std::vector<BtDeviceInfo> BtBatteryScan(void (*log)(const wchar_t*) = nullptr);
