// Samsung Galaxy Buds: level of each earbud and of the case, read from the earbuds' own channel
// (an RFCOMM service, "GEARMANAGER", that the Galaxy Wearable app uses). Right after a connection the
// earbuds send their status by themselves; Juice reads it and closes the channel. Protocol documented by
// github.com/timschneeb/GalaxyBudsClient.
#pragma once
#include <cstdint>

// Samsung SPP service of the Buds+ and later (Buds2, Buds3, Buds FE, Pro models).
// The Windows device node of this service ("BTHENUM\{2E73A4AD-...}") tells which devices are Galaxy Buds.
constexpr wchar_t GALAXY_SERVICE_PREFIX[] = L"BTHENUM\\{2E73A4AD-332D-41FC-90E2-16BEF06523F2}";

struct GalaxyInfo {
    int  parts[3] = { -1, -1, -1 };              // left, right, case (-1 = unknown)
    bool partCharging[3] = { false, false, false };
};

// Connects to the earbuds at `address` (classic Bluetooth address) and reads their status. Blocking
// (up to ~3 s when the earbuds do not answer); call from a worker thread. Returns false when nothing
// was read (earbuds not connected, channel busy, older model).
bool GalaxyRead(uint64_t address, GalaxyInfo& out, void (*log)(const wchar_t*) = nullptr);
