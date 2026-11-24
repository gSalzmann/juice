#include "devices.h"

#include <memory>

namespace {

constexpr ULONGLONG SLEEP_GRACE_MS = 60ull * 60 * 1000;  // keep a sleeping device visible for 1h

Kind KindFromHidpp(int t) {
    switch (t) {
        case 0: case 2:         return Kind::Keyboard;
        case 3: case 4: case 5: return Kind::Mouse;
        case 8:                 return Kind::Headset;
        default:                return Kind::Other;
    }
}

Kind KindFromBt(int major, int minor) {
    if (major == 4) return Kind::Headset;            // audio/video
    if (major == 5) {                                 // peripheral
        int sub = (minor >> 4) & 3;
        if (sub == 2) return Kind::Mouse;
        if (sub == 1 || sub == 3) return Kind::Keyboard;
    }
    return Kind::Other;
}

struct ThreadArgs { HWND hwnd; UINT msg; };

DWORD WINAPI ScanThread(LPVOID p) {
    std::unique_ptr<ThreadArgs> a((ThreadArgs*)p);
    auto* r = new ScanResult;
    r->hid = HidppScan();
    r->bt  = BtBatteryScan();
    if (!PostMessageW(a->hwnd, a->msg, 0, (LPARAM)r)) delete r;
    return 0;
}

}  // namespace

Device* DeviceManager::Find(const std::wstring& key) {
    for (auto& d : devices_) if (d.key == key) return &d;
    return nullptr;
}

Device& DeviceManager::FindOrAdd(const std::wstring& key) {
    if (Device* d = Find(key)) return *d;
    Device d;
    d.key = key;
    auto it = cfg_.known.find(key);
    if (it != cfg_.known.end()) d.name = it->second;
    devices_.push_back(d);
    return devices_.back();
}

bool DeviceManager::IsHidden(const std::wstring& key) const {
    auto it = cfg_.hidden.find(key);
    return it != cfg_.hidden.end() && it->second;
}

void DeviceManager::StartScan(HWND notify, UINT msg) {
    notify_ = notify;
    msg_ = msg;
    if (scanning_) { rescan_ = true; return; }
    scanning_ = true;
    auto* args = new ThreadArgs{ notify, msg };
    HANDLE t = CreateThread(nullptr, 64 * 1024, ScanThread, args, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (t) CloseHandle(t);
    else { delete args; scanning_ = false; }
}

void DeviceManager::OnScanDone(ScanResult* raw) {
    std::unique_ptr<ScanResult> r(raw);
    scanning_ = false;
    const ULONGLONG now = GetTickCount64();
    std::vector<std::wstring> seen;
    bool knownChanged = false;

    auto remember = [&](Device& d) {
        if (!d.name.empty() && cfg_.known[d.key] != d.name) { cfg_.known[d.key] = d.name; knownChanged = true; }
    };

    for (auto& h : r->hid) {
        Device& d = FindOrAdd(h.key);
        seen.push_back(h.key);
        d.asleep = false;
        if (h.online && h.battery.percent >= 0) {
            if (!h.name.empty()) d.name = h.name;
            d.kind = KindFromHidpp(h.deviceType);
            d.percent = h.battery.percent;
            d.charging = h.battery.charging;
            d.lastGood = now;
            d.present = true;
        } else if (h.noResponse) {
            d.present = d.lastGood && now - d.lastGood < SLEEP_GRACE_MS;
            d.asleep = d.present;
        } else {
            d.present = false;
        }
        remember(d);
    }
    for (auto& b : r->bt) {
        Device& d = FindOrAdd(b.key);
        seen.push_back(b.key);
        if (!b.name.empty()) d.name = b.name;
        d.kind = KindFromBt(b.majorClass, b.minorClass);
        d.asleep = false;
        d.present = b.connected && b.percent >= 0;
        if (d.present) { d.percent = b.percent; d.charging = false; d.lastGood = now; }
        remember(d);
    }
    // Devices whose receiver/adapter disappeared entirely.
    for (auto& d : devices_) {
        bool found = false;
        for (auto& k : seen) if (k == d.key) { found = true; break; }
        if (!found) d.present = false;
    }

    if (knownChanged) cfg_.Save();
    if (rescan_) { rescan_ = false; StartScan(notify_, msg_); }
}

std::vector<Device*> DeviceManager::TakeLowBatteryAlerts() {
    std::vector<Device*> out;
    for (auto& d : devices_) {
        if (!d.present || d.percent < 0 || IsHidden(d.key)) continue;
        if (d.charging || d.percent > cfg_.lowThreshold + 5) d.lowNotified = false;
        if (cfg_.lowWarning && !d.charging && d.percent <= cfg_.lowThreshold && !d.lowNotified) {
            d.lowNotified = true;
            out.push_back(&d);
        }
    }
    return out;
}
