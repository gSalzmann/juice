// Device list shared by the tray and taskbar apps: background scanning (HID++ and Bluetooth)
// and merging of results into a stable list of devices.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "btbattery.h"
#include "config.h"
#include "hidpp.h"
#include "icon.h"   // Kind

struct Device {
    std::wstring key, name;
    Kind      kind = Kind::Other;
    int       percent = -1;
    bool      charging = false;
    bool      present = false;    // online, or sleeping within the grace period
    bool      asleep = false;     // last scan got no answer (value shown is the last known one)
    ULONGLONG lastGood = 0;       // tick of the last successful reading
    bool      lowNotified = false;

    const std::wstring& Label() const { return name.empty() ? key : name; }
};

struct ScanResult {
    std::vector<HidppDeviceInfo> hid;
    std::vector<BtDeviceInfo>    bt;
};

class DeviceManager {
public:
    explicit DeviceManager(Config& cfg) : cfg_(cfg) {}

    // Starts a scan on a worker thread; when done, `msg` is posted to `notify` with a
    // ScanResult* in lParam that must be handed to OnScanDone().
    void StartScan(HWND notify, UINT msg);
    void OnScanDone(ScanResult* r);
    bool Scanning() const { return scanning_; }

    std::vector<Device>& Devices() { return devices_; }
    Device* Find(const std::wstring& key);
    bool IsHidden(const std::wstring& key) const;
    Kind KnownKind(const std::wstring& key) const;   // kind remembered in the ini (Other if never seen)
    // Drops a device from the list and the ini; it comes back only when it connects again.
    void Forget(const std::wstring& key);

    // Returns devices that crossed the low-battery threshold since the last call (one per discharge).
    std::vector<Device*> TakeLowBatteryAlerts();

private:
    Device& FindOrAdd(const std::wstring& key);
    // Moves everything stored under an old key (name, kind, hidden, taskbar choice) to the device's
    // real key and drops the old entry. Returns true if the config changed.
    bool Adopt(const std::wstring& from, const std::wstring& to);

    Config& cfg_;
    std::vector<Device> devices_;
    bool scanning_ = false, rescan_ = false;
    HWND notify_ = nullptr;
    UINT msg_ = 0;
};
