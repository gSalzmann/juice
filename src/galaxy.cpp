// Samsung SPP framing (Buds+ and later):
//   FD <size lo> <size hi | flags> <id> <payload...> <crc16 lo> <crc16 hi> DD
//   size (10 bits) = id + payload + crc
// Right after the channel opens the earbuds send EXTENDED_STATUS_UPDATED (0x61), and STATUS_UPDATED
// (0x60) whenever something changes (an earbud goes into or out of the case):
//   0x61: [0] revision [1] ear type [2] left % [3] right % [4] coupled [5] main side
//         [6] placement (left high nibble, right low nibble) [7] case %
//   0x60: [0] revision [1] left % [2] right % [3] coupled [4] main side [5] placement [6] case %
//         [7] charging (bit 4 left, bit 2 right, bit 0 case), when the firmware has it
//   placement: 0 disconnected, 1 wearing, 2 out of the ear, 3 in the case
// The case level is only known while an earbud is in it; otherwise it reads 0.

#include "galaxy.h"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Rfcomm.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <cstdio>
#include <vector>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Rfcomm;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

namespace {

const guid SPP_SERVICE{ 0x2E73A4AD, 0x332D, 0x41FC, { 0x90, 0xE2, 0x16, 0xBE, 0xF0, 0x65, 0x23, 0xF2 } };
constexpr DWORD STEP_TIMEOUT_MS   = 3000;
constexpr DWORD STATUS_TIMEOUT_MS = 1500;   // the status arrives ~50 ms after connecting

template <class Op>
bool Done(Op const& op, DWORD ms) {
    if (op.wait_for(std::chrono::milliseconds(ms)) == AsyncStatus::Completed) return true;
    op.Cancel();
    return false;
}

void Apply(uint8_t id, const uint8_t* p, size_t n, GalaxyInfo& out) {
    int left, right, placement, box, charging = -1;
    if (id == 0x61 && n >= 8) {
        left = p[2]; right = p[3]; placement = p[6]; box = p[7];
    } else if (id == 0x60 && n >= 7) {
        left = p[1]; right = p[2]; placement = p[5]; box = p[6];
        if (n >= 8) charging = p[7];
    } else {
        return;
    }
    const int where[2] = { placement >> 4, placement & 0x0F };
    const int level[2] = { left, right };
    for (int i = 0; i < 2; ++i) {
        out.parts[i] = where[i] != 0 && level[i] <= 100 ? level[i] : -1;   // 0 = earbud not connected
        out.partCharging[i] = charging >= 0 ? (charging & (i ? 0x04 : 0x10)) != 0 : where[i] == 3;
    }
    out.parts[2] = box > 0 && box <= 100 ? box : -1;
    out.partCharging[2] = charging >= 0 && (charging & 0x01) != 0;
}

}  // namespace

bool GalaxyRead(uint64_t address, GalaxyInfo& out, void (*log)(const wchar_t*)) {
    wchar_t line[200];
    bool initialized = false, ok = false;
    try {
        init_apartment(apartment_type::multi_threaded);
        initialized = true;
    } catch (const hresult_error&) {
    }
    try {
        auto devOp = BluetoothDevice::FromBluetoothAddressAsync(address);
        if (!Done(devOp, STEP_TIMEOUT_MS) || !devOp.GetResults()) throw hresult_error(E_FAIL);
        BluetoothDevice dev = devOp.GetResults();
        auto svcOp = dev.GetRfcommServicesForIdAsync(RfcommServiceId::FromUuid(SPP_SERVICE), BluetoothCacheMode::Cached);
        if (!Done(svcOp, STEP_TIMEOUT_MS) || svcOp.GetResults().Services().Size() == 0) throw hresult_error(E_FAIL);
        RfcommDeviceService svc = svcOp.GetResults().Services().GetAt(0);

        StreamSocket socket;
        auto conn = socket.ConnectAsync(svc.ConnectionHostName(), svc.ConnectionServiceName());
        if (!Done(conn, STEP_TIMEOUT_MS)) throw hresult_error(E_FAIL);
        conn.GetResults();

        DataReader reader(socket.InputStream());
        reader.InputStreamOptions(InputStreamOptions::Partial);
        std::vector<uint8_t> buf;
        const ULONGLONG end = GetTickCount64() + STATUS_TIMEOUT_MS;
        while (!ok && GetTickCount64() < end) {
            auto load = reader.LoadAsync(256);
            if (!Done(load, (DWORD)(end - GetTickCount64()))) break;
            const uint32_t n = load.GetResults();
            if (!n) break;
            std::vector<uint8_t> chunk(n);
            reader.ReadBytes(chunk);
            buf.insert(buf.end(), chunk.begin(), chunk.end());

            size_t i = 0;
            while (i + 4 <= buf.size()) {
                if (buf[i] != 0xFD) { ++i; continue; }
                const size_t size = (buf[i + 1] | (buf[i + 2] << 8)) & 0x3FF;
                if (size < 3) { ++i; continue; }
                if (i + 4 + size > buf.size()) break;               // wait for the rest
                if (buf[i + 3 + size] != 0xDD) { ++i; continue; }   // EOM after id + payload + crc
                const uint8_t id = buf[i + 3];
                if (id == 0x61 || id == 0x60) {
                    Apply(id, &buf[i + 4], size - 3, out);
                    ok = out.parts[0] >= 0 || out.parts[1] >= 0;
                }
                i += 4 + size;
            }
            buf.erase(buf.begin(), buf.begin() + i);
        }
        socket.Close();
    } catch (const hresult_error&) {
    }
    if (log) {
        if (ok) swprintf_s(line, L"galaxy %012llX L=%d%s R=%d%s case=%d%s", address, out.parts[0],
                           out.partCharging[0] ? L"c" : L"", out.parts[1], out.partCharging[1] ? L"c" : L"",
                           out.parts[2], out.partCharging[2] ? L"c" : L"");
        else swprintf_s(line, L"galaxy %012llX: no status (not connected or channel busy)", address);
        log(line);
    }
    if (initialized) uninit_apartment();
    return ok;
}
