// JBL control protocol over Bluetooth LE (legacy "AA" frames, used by the Flip 7 and most models):
//   service 65786365-6C70-6F69-6E74-2E636F6D0000, notify ...0001, write ...0002
//   request  AA 11 00                       -> speaker info, answered by a burst of notifications
//   response AA 12 <len> 00 <token> <value> -> one per token; tokens with the high bit are
//                                              length-prefixed strings (C1 name, C2 serial)
//   token 44: battery, low 7 bits = percent, high bit = charging
//   token 48: Bluetooth address of the classic (audio) connection, 6 bytes
// Speakers are found through their advertisement (manufacturer data, company 0x0057 = Harman).
// No pairing is needed; the speaker accepts one control connection at a time (the phone app
// holding it makes the read fail until it lets go).
//
// Apple "proximity pairing" advertisement (AirPods, Beats), manufacturer data of company 0x004C,
// layout as used by OpenPods and AirPodsDesktop:
//   [0] 0x07 type  [1] 0x19 length  [2] 0x01  [3..4] model id  [5] status
//   [6] earbud levels: two nibbles; which one is the left earbud depends on status bit 5
//   [7] high nibble charging flags (left/right as above, bit 2 = case), low nibble case level
//   levels are 0..10 (x10%), 15 = unknown (earbud in the closed case or out of range)
// Anyone's AirPods nearby advertise the same way, so only strong signals count (close to the PC) and
// the strongest one per model wins. Untested: no hardware available.

#include "ble.h"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;

namespace {

constexpr uint16_t HARMAN_COMPANY_ID = 0x0057;
constexpr uint16_t APPLE_COMPANY_ID  = 0x004C;
constexpr int16_t  APPLE_MIN_RSSI    = -65;    // dBm: roughly "on the desk", not the next room
constexpr DWORD    ADVERT_WINDOW_MS  = 2500;
constexpr DWORD    STEP_TIMEOUT_MS   = 4000;
constexpr DWORD    INFO_TIMEOUT_MS   = 3000;

const guid SVC_CONTROL{ 0x65786365, 0x6C70, 0x6F69, { 0x6E, 0x74, 0x2E, 0x63, 0x6F, 0x6D, 0x00, 0x00 } };
const guid CHR_NOTIFY { 0x65786365, 0x6C70, 0x6F69, { 0x6E, 0x74, 0x2E, 0x63, 0x6F, 0x6D, 0x00, 0x01 } };
const guid CHR_WRITE  { 0x65786365, 0x6C70, 0x6F69, { 0x6E, 0x74, 0x2E, 0x63, 0x6F, 0x6D, 0x00, 0x02 } };

void (*g_log)(const wchar_t*) = nullptr;

void Log(const wchar_t* fmt, ...) {
    if (!g_log) return;
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log(buf);
}

// Waits for an async operation with a timeout; cancels it and returns a default value on timeout.
template <class T>
T Await(IAsyncOperation<T> const& op, DWORD ms, T fallback) {
    if (op.wait_for(std::chrono::milliseconds(ms)) != AsyncStatus::Completed) {
        op.Cancel();
        return fallback;
    }
    return op.GetResults();
}

std::vector<uint8_t> Bytes(IBuffer const& b) {
    std::vector<uint8_t> v(b.Length());
    if (!v.empty()) DataReader::FromBuffer(b).ReadBytes(v);
    return v;
}

std::wstring Utf8(const uint8_t* p, size_t n) {
    int wl = MultiByteToWideChar(CP_UTF8, 0, (const char*)p, (int)n, nullptr, 0);
    std::wstring w(wl, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, (const char*)p, (int)n, w.data(), wl);
    while (!w.empty() && (w.back() == L' ' || w.back() == L'\0')) w.pop_back();
    return w;
}

// Accumulates notification bytes and decodes the info frames as they complete.
struct InfoParser {
    std::vector<uint8_t> buf;
    BleDeviceInfo info;
    bool haveBattery = false, haveAddress = false;

    void Feed(const std::vector<uint8_t>& b) {
        buf.insert(buf.end(), b.begin(), b.end());
        size_t i = 0;
        while (i + 3 <= buf.size()) {
            if (buf[i] != 0xAA) { ++i; continue; }
            const size_t len = buf[i + 2];
            if (i + 3 + len > buf.size()) break;                  // wait for the rest
            if (buf[i + 1] == 0x12 && len >= 2) Token(buf[i + 4], &buf[i + 5], len - 2);
            i += 3 + len;
        }
        buf.erase(buf.begin(), buf.begin() + i);
    }

    void Token(uint8_t t, const uint8_t* v, size_t n) {
        if (t == 0x44 && n >= 1) {
            info.percent = v[0] & 0x7F;
            info.charging = (v[0] & 0x80) != 0;
            haveBattery = true;
        } else if (t == 0x48 && n >= 6) {
            wchar_t k[32];
            swprintf_s(k, L"jbl:%02X%02X%02X%02X%02X%02X", v[0], v[1], v[2], v[3], v[4], v[5]);
            info.key = k;
            haveAddress = true;
        } else if (t == 0xC1 && n >= 1 && v[0] <= n - 1) {
            info.name = Utf8(v + 1, v[0]);
        }
    }
};

bool ReadSpeaker(uint64_t address, BleDeviceInfo& out) {
    BluetoothLEDevice dev = Await(BluetoothLEDevice::FromBluetoothAddressAsync(address), STEP_TIMEOUT_MS,
                                  BluetoothLEDevice{ nullptr });
    if (!dev) { Log(L"  %012llX: cannot open", address); return false; }

    bool ok = false;
    try {
        auto sr = Await(dev.GetGattServicesForUuidAsync(SVC_CONTROL, BluetoothCacheMode::Uncached), STEP_TIMEOUT_MS,
                        GattDeviceServicesResult{ nullptr });
        if (!sr || sr.Status() != GattCommunicationStatus::Success || sr.Services().Size() == 0) {
            Log(L"  %012llX: control service not reachable (status=%d)", address, sr ? (int)sr.Status() : -1);
        } else {
            GattDeviceService svc = sr.Services().GetAt(0);
            auto nr = Await(svc.GetCharacteristicsForUuidAsync(CHR_NOTIFY, BluetoothCacheMode::Uncached), STEP_TIMEOUT_MS,
                            GattCharacteristicsResult{ nullptr });
            auto wr = Await(svc.GetCharacteristicsForUuidAsync(CHR_WRITE, BluetoothCacheMode::Uncached), STEP_TIMEOUT_MS,
                            GattCharacteristicsResult{ nullptr });
            if (nr && wr && nr.Characteristics().Size() && wr.Characteristics().Size()) {
                GattCharacteristic ntf = nr.Characteristics().GetAt(0), wrt = wr.Characteristics().GetAt(0);

                std::mutex m;
                InfoParser parser;
                HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                auto token = ntf.ValueChanged([&](auto&&, GattValueChangedEventArgs const& e) {
                    std::lock_guard<std::mutex> g(m);
                    parser.Feed(Bytes(e.CharacteristicValue()));
                    if (parser.haveBattery && parser.haveAddress) SetEvent(done);
                });
                auto cccd = Await(ntf.WriteClientCharacteristicConfigurationDescriptorAsync(
                                      GattClientCharacteristicConfigurationDescriptorValue::Notify),
                                  STEP_TIMEOUT_MS, GattCommunicationStatus::Unreachable);
                if (cccd == GattCommunicationStatus::Success) {
                    DataWriter w;
                    const uint8_t req[3] = { 0xAA, 0x11, 0x00 };
                    w.WriteBytes(req);
                    const auto opt = (wrt.CharacteristicProperties() & GattCharacteristicProperties::WriteWithoutResponse) ==
                                             GattCharacteristicProperties::WriteWithoutResponse
                                         ? GattWriteOption::WriteWithoutResponse : GattWriteOption::WriteWithResponse;
                    auto ws = Await(wrt.WriteValueAsync(w.DetachBuffer(), opt), STEP_TIMEOUT_MS,
                                    GattCommunicationStatus::Unreachable);
                    if (ws == GattCommunicationStatus::Success) WaitForSingleObject(done, INFO_TIMEOUT_MS);
                    else Log(L"  %012llX: write failed (%d)", address, (int)ws);
                } else {
                    Log(L"  %012llX: notifications refused (%d)", address, (int)cccd);
                }
                ntf.ValueChanged(token);
                CloseHandle(done);

                std::lock_guard<std::mutex> g(m);
                if (parser.haveBattery) {
                    out = parser.info;
                    out.speaker = true;
                    if (out.key.empty()) {                          // no classic address: use the BLE one
                        wchar_t k[32];
                        swprintf_s(k, L"jbl:%012llX", address);
                        out.key = k;
                    }
                    if (out.name.empty()) out.name = dev.Name().c_str();
                    ok = true;
                }
            }
        }
    } catch (const hresult_error& e) {
        Log(L"  %012llX: error 0x%08X", address, (unsigned)e.code());
    }
    dev.Close();
    return ok;
}

const wchar_t* AppleModelName(uint16_t model) {
    switch (model) {   // ids as published by community projects (OpenPods, AirPodsDesktop)
        case 0x0220: return L"AirPods";
        case 0x0F20: return L"AirPods (2nd gen)";
        case 0x1320: return L"AirPods (3rd gen)";
        case 0x1920: case 0x1B20: return L"AirPods 4";
        case 0x0E20: return L"AirPods Pro";
        case 0x1420: case 0x2420: return L"AirPods Pro 2";
        case 0x0A20: case 0x1F20: return L"AirPods Max";
        case 0x0320: return L"Powerbeats3";
        case 0x0B20: return L"Powerbeats Pro";
        case 0x0520: return L"BeatsX";
        case 0x0620: return L"Beats Solo3";
        case 0x0920: return L"Beats Studio3";
        case 0x0C20: return L"Beats Solo Pro";
        case 0x1020: return L"Beats Flex";
        case 0x1120: return L"Beats Studio Buds";
        case 0x1220: return L"Beats Fit Pro";
        case 0x1620: return L"Beats Studio Buds+";
        case 0x1720: return L"Beats Studio Pro";
    }
    return nullptr;
}

// Decodes a proximity pairing advertisement. Returns false if it is something else.
bool DecodeApple(const std::vector<uint8_t>& d, BleDeviceInfo& out) {
    if (d.size() < 8 || d[0] != 0x07 || d[1] < 0x19) return false;
    const uint16_t model = (uint16_t)(d[3] << 8 | d[4]);
    const wchar_t* name = AppleModelName(model);
    if (!name) return false;
    const bool flip = (d[5] & 0x20) == 0;
    const int hi = d[6] >> 4, lo = d[6] & 0x0F, flags = d[7] >> 4;
    const int left = flip ? hi : lo, right = flip ? lo : hi, box = d[7] & 0x0F;
    auto pct = [](int v) { return v <= 10 ? v * 10 : -1; };
    out.parts[0] = pct(left);
    out.parts[1] = pct(right);
    out.parts[2] = pct(box);
    out.partCharging[0] = (flags & (flip ? 0x02 : 0x01)) != 0;
    out.partCharging[1] = (flags & (flip ? 0x01 : 0x02)) != 0;
    out.partCharging[2] = (flags & 0x04) != 0;
    // The row shows the earbud that runs out first; charging only when every earbud in use charges.
    out.percent = -1;
    bool allCharging = true;
    for (int i = 0; i < 2; ++i)
        if (out.parts[i] >= 0) {
            if (out.percent < 0 || out.parts[i] < out.percent) out.percent = out.parts[i];
            allCharging &= out.partCharging[i];
        }
    if (out.percent < 0) return false;   // only the case is known: nothing to show
    out.charging = allCharging;
    wchar_t key[32];
    swprintf_s(key, L"apple:%04X", model);
    out.key = key;
    out.name = name;
    return true;
}

}  // namespace

std::vector<BleDeviceInfo> BleScan(bool jbl, bool apple, void (*log)(const wchar_t*)) {
    g_log = log;
    std::vector<BleDeviceInfo> out;
    bool initialized = false;
    try {
        init_apartment(apartment_type::multi_threaded);
        initialized = true;
    } catch (const hresult_error&) {
        // already initialized on this thread with another model: use it as is
    }
    try {
        std::mutex m;
        std::vector<uint64_t> found;               // JBL speakers to connect to
        std::vector<std::pair<BleDeviceInfo, int>> pods;   // Apple earbuds and their signal strength
        BluetoothLEAdvertisementWatcher watcher;
        watcher.ScanningMode(BluetoothLEScanningMode::Active);
        auto token = watcher.Received([&](auto&&, BluetoothLEAdvertisementReceivedEventArgs const& a) {
            for (auto md : a.Advertisement().ManufacturerData()) {
                if (jbl && md.CompanyId() == HARMAN_COMPANY_ID) {
                    std::lock_guard<std::mutex> g(m);
                    const uint64_t addr = a.BluetoothAddress();
                    if (std::find(found.begin(), found.end(), addr) == found.end()) found.push_back(addr);
                } else if (apple && md.CompanyId() == APPLE_COMPANY_ID && a.RawSignalStrengthInDBm() >= APPLE_MIN_RSSI) {
                    BleDeviceInfo info;
                    if (!DecodeApple(Bytes(md.Data()), info)) continue;
                    const int rssi = a.RawSignalStrengthInDBm();
                    std::lock_guard<std::mutex> g(m);
                    bool seen = false;
                    for (auto& [p, r] : pods)
                        if (p.key == info.key) { seen = true; if (rssi > r) { p = info; r = rssi; } }
                    if (!seen) pods.emplace_back(info, rssi);
                }
            }
        });
        watcher.Start();
        Sleep(ADVERT_WINDOW_MS);
        watcher.Stop();
        watcher.Received(token);

        std::vector<uint64_t> list;
        { std::lock_guard<std::mutex> g(m); list = found; }
        for (uint64_t addr : list) {
            BleDeviceInfo info;
            if (ReadSpeaker(addr, info)) {
                Log(L"jbl %s \"%s\" battery=%d%% charging=%d (ble %012llX)", info.key.c_str(), info.name.c_str(),
                    info.percent, info.charging, addr);
                bool dup = false;
                for (auto& o : out) dup |= o.key == info.key;
                if (!dup) out.push_back(info);
            }
        }
        if (jbl && list.empty()) Log(L"jbl: no speaker advertising");

        std::lock_guard<std::mutex> g(m);
        for (auto& [p, rssi] : pods) {
            Log(L"apple %s \"%s\" L=%d R=%d case=%d -> %d%%%s (rssi %d)", p.key.c_str(), p.name.c_str(), p.parts[0],
                p.parts[1], p.parts[2], p.percent, p.charging ? L" charging" : L"", rssi);
            out.push_back(p);
        }
        if (apple && pods.empty()) Log(L"apple: no AirPods/Beats advertising nearby");
    } catch (const hresult_error& e) {
        Log(L"ble: Bluetooth LE unavailable (0x%08X)", (unsigned)e.code());
    }
    if (initialized) uninit_apartment();
    g_log = nullptr;
    return out;
}
