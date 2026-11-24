// Settings stored in %APPDATA%\WirelessTray\config.ini
#pragma once
#include <map>
#include <string>

struct Config {
    int  intervalMin  = 5;      // polling interval in minutes
    bool lowWarning   = true;
    int  lowThreshold = 15;     // percent
    std::map<std::wstring, bool>         hidden;  // device key -> hidden
    std::map<std::wstring, std::wstring> known;   // device key -> last known name (for the settings list)

    void Load();
    void Save() const;
};

bool AutostartEnabled();
void SetAutostart(bool on);
