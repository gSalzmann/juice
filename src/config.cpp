#include "config.h"

#include <windows.h>
#include <shlobj.h>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring AppDataDir() {
    wchar_t* appdata = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        dir = appdata;
        CoTaskMemFree(appdata);
    }
    return dir;
}

std::wstring IniPath(const std::wstring& file) {
    std::wstring dir = AppDataDir() + L"\\Juice";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\" + file;
    // Settings from before the rename (Wireless Bar) are carried over once.
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        CopyFileW((AppDataDir() + L"\\WirelessTray\\bar.ini").c_str(), path.c_str(), TRUE);
    return path;
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Reads every key=value pair of a section.
std::vector<std::pair<std::wstring, std::wstring>> ReadSection(const std::wstring& ini, const wchar_t* section) {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetPrivateProfileSectionW(section, buf.data(), (DWORD)buf.size(), ini.c_str());
    std::vector<std::pair<std::wstring, std::wstring>> out;
    for (const wchar_t* p = buf.data(); p < buf.data() + n && *p; p += wcslen(p) + 1) {
        std::wstring line = p;
        size_t eq = line.find(L'=');
        if (eq != std::wstring::npos) out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    }
    return out;
}

void WriteSection(const std::wstring& ini, const wchar_t* section, const std::wstring& body) {
    // body is "k=v\0k=v\0\0"
    WritePrivateProfileSectionW(section, body.c_str(), ini.c_str());
}

}  // namespace

void Config::Load() {
    std::wstring ini = IniPath(file);
    intervalMin  = Clamp(GetPrivateProfileIntW(L"general", L"interval_min", 5, ini.c_str()), 1, 60);
    lowWarning   = GetPrivateProfileIntW(L"general", L"low_warning", 1, ini.c_str()) != 0;
    lowThreshold = Clamp(GetPrivateProfileIntW(L"general", L"low_threshold", 15, ini.c_str()), 5, 50);
    colorLevels  = GetPrivateProfileIntW(L"general", L"color_levels", 0, ini.c_str()) != 0;
    wchar_t dev[256]{};
    GetPrivateProfileStringW(L"general", L"bar_device", L"", dev, 256, ini.c_str());
    barDevice = dev;
    showNumber   = GetPrivateProfileIntW(L"general", L"show_number", 1, ini.c_str()) != 0;
    hidden.clear();
    known.clear();
    kinds.clear();
    slots.clear();
    aliases.clear();
    for (auto& [k, v] : ReadSection(ini, L"hidden")) hidden[k] = v == L"1";
    for (auto& [k, v] : ReadSection(ini, L"devices")) known[k] = v;
    for (auto& [k, v] : ReadSection(ini, L"kinds")) kinds[k] = v;
    for (auto& [k, v] : ReadSection(ini, L"slots")) slots[k] = v;
    for (auto& [k, v] : ReadSection(ini, L"aliases")) aliases[k] = v;
}

void Config::Save() const {
    std::wstring ini = IniPath(file);
    WritePrivateProfileStringW(L"general", L"interval_min", std::to_wstring(intervalMin).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"general", L"low_warning", lowWarning ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"general", L"low_threshold", std::to_wstring(lowThreshold).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"general", L"color_levels", colorLevels ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"general", L"bar_device", barDevice.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"general", L"show_number", showNumber ? L"1" : L"0", ini.c_str());

    std::wstring body;
    for (auto& [k, v] : hidden) if (v) { body += k + L"=1"; body.push_back(L'\0'); }
    body.push_back(L'\0');
    WriteSection(ini, L"hidden", body);

    auto writeMap = [&](const wchar_t* section, const std::map<std::wstring, std::wstring>& m) {
        std::wstring b;
        for (auto& [k, v] : m) if (!v.empty()) { b += k + L"=" + v; b.push_back(L'\0'); }
        b.push_back(L'\0');
        WriteSection(ini, section, b);
    };
    writeMap(L"devices", known);
    writeMap(L"kinds", kinds);
    writeMap(L"slots", slots);
    writeMap(L"aliases", aliases);
}

bool AutostartEnabled(const wchar_t* name) {
    wchar_t val[MAX_PATH * 2]{};
    DWORD size = sizeof(val);
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, name, RRF_RT_REG_SZ, nullptr, val, &size) == ERROR_SUCCESS;
}

void SetAutostart(const wchar_t* name, bool on, const wchar_t* args) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        if (args && *args) cmd += L" " + std::wstring(args);
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, name);
    }
    RegCloseKey(k);
}
