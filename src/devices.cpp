#include "devices.h"

#include <algorithm>
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

// Kind names stored in the ini ([kinds] section); Other is never stored.
const wchar_t* const KIND_NAMES[] = { L"mouse", L"keyboard", L"headset" };

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
    d.kind = KnownKind(key);
    devices_.push_back(d);
    return devices_.back();
}

bool DeviceManager::IsHidden(const std::wstring& key) const {
    auto it = cfg_.hidden.find(key);
    return it != cfg_.hidden.end() && it->second;
}

bool DeviceManager::Adopt(const std::wstring& from, const std::wstring& to) {
    bool changed = false;
    auto move = [&](auto& m) {
        auto it = m.find(from);
        if (it == m.end()) return;
        if (m.find(to) == m.end()) m[to] = it->second;
        m.erase(it);
        changed = true;
    };
    move(cfg_.known);
    move(cfg_.kinds);
    move(cfg_.hidden);
    if (cfg_.barDevice == from) { cfg_.barDevice = to; changed = true; }
    devices_.erase(std::remove_if(devices_.begin(), devices_.end(),
                                  [&](const Device& d) { return d.key == from; }), devices_.end());
    return changed;
}

Kind DeviceManager::KnownKind(const std::wstring& key) const {
    auto it = cfg_.kinds.find(key);
    if (it != cfg_.kinds.end())
        for (int i = 0; i < 3; i++) if (it->second == KIND_NAMES[i]) return (Kind)i;
    return Kind::Other;
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
        if (d.kind != Kind::Other && cfg_.kinds[d.key] != KIND_NAMES[(int)d.kind]) {
            cfg_.kinds[d.key] = KIND_NAMES[(int)d.kind];
            knownChanged = true;
        }
    };

    // A device is keyed by its serial when it reports one, so the same mouse is one row whether it is
    // on its dongle or on Bluetooth, and two dongles of the same model never mix their devices up.
    // A slot that does not answer (asleep) is resolved through the serial last seen there.
    auto idOf = [&](const HidppDeviceInfo& h) -> std::wstring {
        if (!h.unit.empty()) {
            if (cfg_.slots[h.key] != h.unit) { cfg_.slots[h.key] = h.unit; knownChanged = true; }
            if (h.key != h.unit && Adopt(h.key, h.unit)) knownChanged = true;
            // Receiver keys before 1.1 had no receiver hash ("hidpp:C547:1"); take that entry over only
            // if it carries this device's name, since two dongles of the same model shared it.
            if (std::count(h.key.begin(), h.key.end(), L':') == 3) {
                std::wstring legacy = h.key.substr(0, h.key.rfind(L':'));
                auto it = cfg_.known.find(legacy);
                if (it != cfg_.known.end() && it->second == h.name && Adopt(legacy, h.unit)) knownChanged = true;
            }
            // The Bluetooth entry of the same physical device (BLE mice) shares the container id.
            if (!h.container.empty())
                for (auto& b : r->bt)
                    if (b.key == L"bt:" + h.container && cfg_.aliases[b.key] != h.unit) {
                        cfg_.aliases[b.key] = h.unit;
                        Adopt(b.key, h.unit);
                        knownChanged = true;
                    }
            return h.unit;
        }
        auto it = cfg_.slots.find(h.key);
        return it != cfg_.slots.end() && !it->second.empty() ? it->second : h.key;
    };

    // Answers first: an asleep dongle slot must not override the same mouse answering over Bluetooth.
    std::vector<const HidppDeviceInfo*> order;
    for (auto& h : r->hid) if (h.online && h.battery.percent >= 0) order.push_back(&h);
    for (auto& h : r->hid) if (!(h.online && h.battery.percent >= 0)) order.push_back(&h);
    std::vector<std::wstring> answered;
    auto in = [](const std::vector<std::wstring>& v, const std::wstring& k) {
        return std::find(v.begin(), v.end(), k) != v.end();
    };

    for (const HidppDeviceInfo* hp : order) {
        const HidppDeviceInfo& h = *hp;
        std::wstring id = idOf(h);
        if (in(answered, id)) continue;
        // A slot never seen answering has no name to show; wait until it wakes up.
        if (!Find(id) && cfg_.known.find(id) == cfg_.known.end() && !(h.online && h.battery.percent >= 0)) continue;
        Device& d = FindOrAdd(id);
        seen.push_back(id);
        d.asleep = false;
        // Name and type come from the device even when its battery cannot be read (headset turned off).
        if (h.online && !h.name.empty()) d.name = h.name;
        if (Kind k = KindFromHidpp(h.deviceType); h.online && k != Kind::Other) d.kind = k;
        if (h.online && h.battery.percent >= 0) {
            d.percent = h.battery.percent;
            d.charging = h.battery.charging;
            d.lastGood = now;
            d.present = true;
            answered.push_back(id);
        } else if (h.noResponse) {
            d.present = d.lastGood && now - d.lastGood < SLEEP_GRACE_MS;
            d.asleep = d.present;
        } else {
            d.present = false;
        }
        remember(d);
    }
    for (auto& b : r->bt) {
        if (auto al = cfg_.aliases.find(b.key); al != cfg_.aliases.end() && !al->second.empty()) {
            // Same device as a HID++ one: HID++ wins (it knows the charging state); the Windows
            // Bluetooth value only fills in when HID++ did not read it this time.
            if (in(answered, al->second) || !b.connected || b.percent < 0) continue;
            Device& d = FindOrAdd(al->second);
            seen.push_back(d.key);
            d.asleep = false;
            d.present = true;
            d.percent = b.percent;
            d.charging = false;
            d.lastGood = now;
            continue;
        }
        Device& d = FindOrAdd(b.key);
        seen.push_back(b.key);
        if (!b.name.empty()) d.name = b.name;
        if (Kind k = KindFromBt(b.majorClass, b.minorClass); k != Kind::Other) d.kind = k;
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
