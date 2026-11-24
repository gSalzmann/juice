// Settings stored in %APPDATA%\Juice\<file>
#pragma once
#include <map>
#include <string>

struct Config {
    std::wstring file = L"juice.ini";   // ini file name inside %APPDATA%\Juice

    int  intervalMin  = 5;      // polling interval in minutes
    bool lowWarning   = true;
    int  lowThreshold = 15;     // percent
    bool colorLevels  = false;  // tray: blue / orange / red number by battery level
    std::wstring barDevice;     // taskbar app: device key shown on the button ("" = lowest battery, "*" = rotate)
    bool showNumber   = true;   // taskbar app: % badge on the button icon
    bool showLast     = true;   // disconnected devices keep showing their last level (greyed out)
    bool jbl          = false;  // read JBL speakers over Bluetooth LE (needs a short BLE scan per refresh)
    std::map<std::wstring, bool>         hidden;  // device key -> hidden
    std::map<std::wstring, std::wstring> known;   // device key -> last known name (for the settings list)
    std::map<std::wstring, std::wstring> kinds;   // device key -> last known kind ("mouse", "keyboard", "headset")
    std::map<std::wstring, std::wstring> slots;   // HID++ receiver slot / interface key -> device key (serial) seen there
    std::map<std::wstring, std::wstring> aliases;
    std::map<std::wstring, std::wstring> last;    // device key -> "percent,unix time" of the last reading // Bluetooth key -> HID++ device key of the same physical device

    void Load();
    void Save() const;
};

// Autostart through HKCU\...\Run. `name` is the registry value, `args` is appended to the exe path.
bool AutostartEnabled(const wchar_t* name);
void SetAutostart(const wchar_t* name, bool on, const wchar_t* args = L"");
