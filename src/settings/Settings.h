#pragma once
#include <windows.h>

#include <string>
#include <vector>

// Everything that is remembered between runs.
struct SettingsData {
    std::wstring skinPath;
    float volume = 0.8f;    // 0..1
    float balance = 0.0f;   // -1..1
    bool shuffle = false;
    bool repeat = false;
    bool shade = false;
    bool eqVisible = false;
    bool plVisible = false;
    bool eqEnabled = true;
    bool alwaysOnTop = false;
    bool hasMainPos = false;  // false: let the app pick a default position
    int mainX = 100;
    int mainY = 100;
    float eqPreamp = 0.0f;    // dB, -12..12
    float eqBands[10] = {};   // dB, -12..12
    std::wstring lastDir;     // last folder used in file dialogs
};

// Stored as UTF-8 "key=value" lines in %APPDATA%\wnamp\settings.ini.
class Settings {
public:
    static std::wstring configDir();                 // %APPDATA%\wnamp (created on demand)
    static std::wstring exeDir();                    // folder containing the running exe
    static std::vector<std::wstring> skinDirs();     // {exeDir\skins, configDir\Skins} that exist / could be created
    static std::wstring playlistPath();              // configDir\playlist.m3u8

    bool load();         // false if there is no settings file yet; unspecified keys keep their current value
    bool save() const;   // atomic (temp file + replace)

    SettingsData data;
};
