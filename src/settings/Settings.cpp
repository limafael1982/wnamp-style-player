#include "settings/Settings.h"

#include <shlobj.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::wstring toWide(std::string_view narrow) {
    if (narrow.empty()) return {};
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()), wide.data(), wideLen);
    return wide;
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int byteLen =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (byteLen <= 0) return {};
    std::string narrow(static_cast<size_t>(byteLen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), byteLen, nullptr,
                         nullptr);
    return narrow;
}

bool dirExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// True if the directory exists afterwards (creating it if needed).
bool ensureDir(const std::wstring& path) {
    if (dirExists(path)) return true;
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && dirExists(path);
}

std::wstring joinPath(const std::wstring& base, const wchar_t* leaf) {
    std::wstring result = base;
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') result += L'\\';
    result += leaf;
    return result;
}

std::wstring appDataDir() {
    std::wstring result;
    PWSTR knownFolderPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &knownFolderPath)) && knownFolderPath)
        result = knownFolderPath;
    if (knownFolderPath) CoTaskMemFree(knownFolderPath);
    if (result.empty()) {
        wchar_t buf[MAX_PATH] = {};
        const DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) result.assign(buf, len);
    }
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

std::string lowerKey(std::string_view text) {
    std::string result(text);
    for (auto& ch : result) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return result;
}

bool parseBool(std::string_view text, bool& out) {
    const std::string lower = lowerKey(text);
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") { out = true; return true; }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") { out = false; return true; }
    return false;
}

bool parseFloat(std::string_view text, float& out) {
    float value = 0.0f;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    if (begin != end && *begin == '+') ++begin;  // from_chars rejects a leading '+'
    const auto parseResult = std::from_chars(begin, end, value);
    if (parseResult.ec != std::errc() || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool parseInt(std::string_view text, int& out) {
    int value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    if (begin != end && *begin == '+') ++begin;
    const auto parseResult = std::from_chars(begin, end, value);
    if (parseResult.ec != std::errc()) return false;
    out = value;
    return true;
}

float clampf(float value, float lo, float hi) { return std::min(hi, std::max(lo, value)); }

std::string fmtFloat(float value) {
    char buf[32];
    const auto result = std::to_chars(buf, buf + sizeof buf, value);
    return result.ec == std::errc() ? std::string(buf, result.ptr) : std::string("0");
}

// Strip line breaks so a value can never inject extra keys.
std::string oneLine(std::string text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](char ch) { return ch == '\r' || ch == '\n'; }), text.end());
    return text;
}

// Keep a saved window position only if part of the window would still be on a monitor.
bool positionLooksSane(int x, int y) {
    const int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (virtualWidth <= 0 || virtualHeight <= 0) return true;  // cannot tell; trust it
    constexpr int kMargin = 50;
    return x > virtualLeft - 275 + kMargin && x < virtualLeft + virtualWidth - kMargin && y > virtualTop - 14 + 0 &&
           y < virtualTop + virtualHeight - kMargin;
}

bool readFile(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER fileSize{};
    bool ok = GetFileSizeEx(file, &fileSize) && fileSize.QuadPart >= 0 && fileSize.QuadPart <= (1 << 20);
    if (ok) {
        out.resize(static_cast<size_t>(fileSize.QuadPart));
        size_t bytesRead = 0;
        while (bytesRead < out.size()) {
            DWORD chunk = 0;
            if (!ReadFile(file, out.data() + bytesRead, static_cast<DWORD>(out.size() - bytesRead), &chunk, nullptr) ||
                chunk == 0)
                break;
            bytesRead += chunk;
        }
        out.resize(bytesRead);
    }
    CloseHandle(file);
    return ok;
}

bool writeFileAtomic(const std::wstring& path, const std::string& bytes) {
    const std::wstring tempPath = path + L".tmp";
    HANDLE file =
        CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    size_t bytesWritten = 0;
    while (bytesWritten < bytes.size()) {
        DWORD chunk = 0;
        if (!WriteFile(file, bytes.data() + bytesWritten, static_cast<DWORD>(bytes.size() - bytesWritten), &chunk,
                        nullptr) ||
            chunk == 0) {
            ok = false;
            break;
        }
        bytesWritten += chunk;
    }
    if (ok) ok = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    if (ok) ok = MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tempPath.c_str());
    return ok;
}

}  // namespace

std::wstring Settings::configDir() {
    std::wstring base = appDataDir();
    if (base.empty()) return {};
    std::wstring dir = joinPath(base, L"wnamp");
    ensureDir(dir);
    return dir;
}

std::wstring Settings::exeDir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return {};
        if (len < buf.size()) { buf.resize(len); break; }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
    const size_t slash = buf.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : buf.substr(0, slash);
}

std::wstring Settings::userSkinsDir() {
    const std::wstring cfg = configDir();
    if (cfg.empty()) return {};
    const std::wstring dir = joinPath(cfg, L"Skins");
    return ensureDir(dir) ? dir : std::wstring();
}

std::vector<std::wstring> Settings::skinDirs() {
    std::vector<std::wstring> dirs;
    const std::wstring exe = exeDir();
    if (!exe.empty()) {
        const std::wstring exeSkins = joinPath(exe, L"skins");
        if (ensureDir(exeSkins)) dirs.push_back(exeSkins);
    }
    const std::wstring userDir = userSkinsDir();
    if (!userDir.empty()) dirs.push_back(userDir);
    return dirs;
}

std::wstring Settings::playlistPath() {
    const std::wstring cfg = configDir();
    return cfg.empty() ? std::wstring() : joinPath(cfg, L"playlist.m3u8");
}

bool Settings::load() {
    const std::wstring dir = configDir();
    if (dir.empty()) return false;
    std::string text;
    if (!readFile(joinPath(dir, L"settings.ini"), text)) return false;

    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);

    SettingsData loaded = data;
    bool gotX = false, gotY = false;
    bool sawRadioCount = false;
    std::vector<std::pair<std::wstring, std::wstring>> stationFields;  // (title, url) by index
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t lineEnd = text.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        const std::string_view line = trim(std::string_view(text).substr(pos, lineEnd - pos));
        pos = lineEnd + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eqPos = line.find('=');
        if (eqPos == std::string_view::npos) continue;
        const std::string key = lowerKey(trim(line.substr(0, eqPos)));
        const std::string_view val = trim(line.substr(eqPos + 1));

        float floatVal = 0;
        int intVal = 0;
        bool boolVal = false;
        if (key == "skin") loaded.skinPath = toWide(val);
        else if (key == "lastdir") loaded.lastDir = toWide(val);
        else if (key == "volume") { if (parseFloat(val, floatVal)) loaded.volume = clampf(floatVal, 0.0f, 1.0f); }
        else if (key == "balance") { if (parseFloat(val, floatVal)) loaded.balance = clampf(floatVal, -1.0f, 1.0f); }
        else if (key == "shuffle") { if (parseBool(val, boolVal)) loaded.shuffle = boolVal; }
        else if (key == "repeat") { if (parseBool(val, boolVal)) loaded.repeat = boolVal; }
        else if (key == "shade") { if (parseBool(val, boolVal)) loaded.shade = boolVal; }
        else if (key == "eqvisible") { if (parseBool(val, boolVal)) loaded.eqVisible = boolVal; }
        else if (key == "plvisible") { if (parseBool(val, boolVal)) loaded.plVisible = boolVal; }
        else if (key == "eqenabled") { if (parseBool(val, boolVal)) loaded.eqEnabled = boolVal; }
        else if (key == "alwaysontop") { if (parseBool(val, boolVal)) loaded.alwaysOnTop = boolVal; }
        else if (key == "mainx") {
            if (parseInt(val, intVal)) { loaded.mainX = std::clamp(intVal, -32000, 32000); gotX = true; }
        }
        else if (key == "mainy") {
            if (parseInt(val, intVal)) { loaded.mainY = std::clamp(intVal, -32000, 32000); gotY = true; }
        }
        else if (key == "eqpreamp") { if (parseFloat(val, floatVal)) loaded.eqPreamp = clampf(floatVal, -12.0f, 12.0f); }
        else if (key.size() == 3 && key.compare(0, 2, "eq") == 0 && key[2] >= '0' && key[2] <= '9') {
            if (parseFloat(val, floatVal)) loaded.eqBands[key[2] - '0'] = clampf(floatVal, -12.0f, 12.0f);
        }
        else if (key == "showtrayicon") { if (parseBool(val, boolVal)) loaded.showTrayIcon = boolVal; }
        else if (key == "keeprunningbackground") { if (parseBool(val, boolVal)) loaded.keepRunningInBackground = boolVal; }
        else if (key == "radiocount") {
            sawRadioCount = true;
            if (parseInt(val, intVal)) stationFields.resize(static_cast<size_t>(std::max(0, intVal)));
        }
        else if (key.rfind("radio", 0) == 0 && key.size() > 5) {
            // radio<N>name / radio<N>url
            size_t digitsStart = 5;
            size_t digitsEnd = digitsStart;
            while (digitsEnd < key.size() && key[digitsEnd] >= '0' && key[digitsEnd] <= '9') ++digitsEnd;
            if (digitsEnd > digitsStart) {
                const int index = std::atoi(key.substr(digitsStart, digitsEnd - digitsStart).c_str());
                const std::string field = key.substr(digitsEnd);
                if (index >= 0) {
                    if (static_cast<size_t>(index) >= stationFields.size())
                        stationFields.resize(static_cast<size_t>(index) + 1);
                    if (field == "name") stationFields[static_cast<size_t>(index)].first = toWide(val);
                    else if (field == "url") stationFields[static_cast<size_t>(index)].second = toWide(val);
                }
            }
        }
        // unknown keys are ignored
    }

    if (gotX && gotY) loaded.hasMainPos = positionLooksSane(loaded.mainX, loaded.mainY);
    if (sawRadioCount) {
        loaded.customStations.clear();
        for (const auto& [title, url] : stationFields)
            if (!title.empty() && !url.empty()) loaded.customStations.push_back(radio::Station{title, url});
    }
    data = std::move(loaded);
    return true;
}

bool Settings::save() const {
    const std::wstring dir = configDir();
    if (dir.empty()) return false;

    const SettingsData& current = data;
    std::string out;
    auto put = [&out](const char* key, const std::string& value) {
        out += key;
        out += '=';
        out += oneLine(value);
        out += "\r\n";
    };
    auto putBool = [&put](const char* key, bool value) { put(key, value ? "1" : "0"); };

    out += "# wnamp settings (UTF-8)\r\n";
    put("skin", toUtf8(current.skinPath));
    put("lastdir", toUtf8(current.lastDir));
    put("volume", fmtFloat(clampf(current.volume, 0.0f, 1.0f)));
    put("balance", fmtFloat(clampf(current.balance, -1.0f, 1.0f)));
    putBool("shuffle", current.shuffle);
    putBool("repeat", current.repeat);
    putBool("shade", current.shade);
    putBool("eqvisible", current.eqVisible);
    putBool("plvisible", current.plVisible);
    putBool("eqenabled", current.eqEnabled);
    putBool("alwaysontop", current.alwaysOnTop);
    if (current.hasMainPos) {
        put("mainx", std::to_string(current.mainX));
        put("mainy", std::to_string(current.mainY));
    }
    put("eqpreamp", fmtFloat(clampf(current.eqPreamp, -12.0f, 12.0f)));
    for (int i = 0; i < 10; ++i) {
        const std::string key = "eq" + std::to_string(i);
        put(key.c_str(), fmtFloat(clampf(current.eqBands[i], -12.0f, 12.0f)));
    }
    putBool("showtrayicon", current.showTrayIcon);
    putBool("keeprunningbackground", current.keepRunningInBackground);
    put("radiocount", std::to_string(current.customStations.size()));
    for (size_t i = 0; i < current.customStations.size(); ++i) {
        const std::string base = "radio" + std::to_string(i);
        put((base + "name").c_str(), toUtf8(current.customStations[i].title));
        put((base + "url").c_str(), toUtf8(current.customStations[i].url));
    }
    return writeFileAtomic(joinPath(dir, L"settings.ini"), out);
}
