#include "settings/Settings.h"

#include <shlobj.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

std::wstring toWide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

bool dirExists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// True if the directory exists afterwards (creating it if needed).
bool ensureDir(const std::wstring& path) {
    if (dirExists(path)) return true;
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && dirExists(path);
}

std::wstring joinPath(const std::wstring& a, const wchar_t* b) {
    std::wstring r = a;
    if (!r.empty() && r.back() != L'\\' && r.back() != L'/') r += L'\\';
    r += b;
    return r;
}

std::wstring appDataDir() {
    std::wstring result;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p) result = p;
    if (p) CoTaskMemFree(p);
    if (result.empty()) {
        wchar_t buf[MAX_PATH] = {};
        const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) result.assign(buf, n);
    }
    return result;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

std::string lowerKey(std::string_view s) {
    std::string r(s);
    for (auto& c : r) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return r;
}

bool parseBool(std::string_view v, bool& out) {
    const std::string l = lowerKey(v);
    if (l == "1" || l == "true" || l == "yes" || l == "on") { out = true; return true; }
    if (l == "0" || l == "false" || l == "no" || l == "off") { out = false; return true; }
    return false;
}

bool parseFloat(std::string_view v, float& out) {
    float f = 0.0f;
    const char* b = v.data();
    const char* e = v.data() + v.size();
    if (b != e && *b == '+') ++b;  // from_chars rejects a leading '+'
    const auto res = std::from_chars(b, e, f);
    if (res.ec != std::errc() || !std::isfinite(f)) return false;
    out = f;
    return true;
}

bool parseInt(std::string_view v, int& out) {
    int i = 0;
    const char* b = v.data();
    const char* e = v.data() + v.size();
    if (b != e && *b == '+') ++b;
    const auto res = std::from_chars(b, e, i);
    if (res.ec != std::errc()) return false;
    out = i;
    return true;
}

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

std::string fmtFloat(float v) {
    char buf[32];
    const auto res = std::to_chars(buf, buf + sizeof buf, v);
    return res.ec == std::errc() ? std::string(buf, res.ptr) : std::string("0");
}

// Strip line breaks so a value can never inject extra keys.
std::string oneLine(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '\r' || c == '\n'; }), s.end());
    return s;
}

// Keep a saved window position only if part of the window would still be on a monitor.
bool positionLooksSane(int x, int y) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return true;  // cannot tell; trust it
    constexpr int kMargin = 50;
    return x > vx - 275 + kMargin && x < vx + vw - kMargin && y > vy - 14 + 0 && y < vy + vh - kMargin;
}

bool readFile(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 && size.QuadPart <= (1 << 20);
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        size_t got = 0;
        while (got < out.size()) {
            DWORD n = 0;
            if (!ReadFile(h, out.data() + got, static_cast<DWORD>(out.size() - got), &n, nullptr) || n == 0) break;
            got += n;
        }
        out.resize(got);
    }
    CloseHandle(h);
    return ok;
}

bool writeFileAtomic(const std::wstring& path, const std::string& bytes) {
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD n = 0;
        if (!WriteFile(h, bytes.data() + done, static_cast<DWORD>(bytes.size() - done), &n, nullptr) || n == 0) {
            ok = false;
            break;
        }
        done += n;
    }
    if (ok) ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp.c_str());
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
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); break; }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
    const size_t slash = buf.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : buf.substr(0, slash);
}

std::vector<std::wstring> Settings::skinDirs() {
    std::vector<std::wstring> dirs;
    const std::wstring exe = exeDir();
    if (!exe.empty()) {
        const std::wstring d = joinPath(exe, L"skins");
        if (ensureDir(d)) dirs.push_back(d);
    }
    const std::wstring cfg = configDir();
    if (!cfg.empty()) {
        const std::wstring d = joinPath(cfg, L"Skins");
        if (ensureDir(d)) dirs.push_back(d);
    }
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

    SettingsData d = data;
    bool gotX = false, gotY = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string_view line = trim(std::string_view(text).substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string key = lowerKey(trim(line.substr(0, eq)));
        const std::string_view val = trim(line.substr(eq + 1));

        float f = 0;
        int i = 0;
        bool b = false;
        if (key == "skin") d.skinPath = toWide(val);
        else if (key == "lastdir") d.lastDir = toWide(val);
        else if (key == "volume") { if (parseFloat(val, f)) d.volume = clampf(f, 0.0f, 1.0f); }
        else if (key == "balance") { if (parseFloat(val, f)) d.balance = clampf(f, -1.0f, 1.0f); }
        else if (key == "shuffle") { if (parseBool(val, b)) d.shuffle = b; }
        else if (key == "repeat") { if (parseBool(val, b)) d.repeat = b; }
        else if (key == "shade") { if (parseBool(val, b)) d.shade = b; }
        else if (key == "eqvisible") { if (parseBool(val, b)) d.eqVisible = b; }
        else if (key == "plvisible") { if (parseBool(val, b)) d.plVisible = b; }
        else if (key == "eqenabled") { if (parseBool(val, b)) d.eqEnabled = b; }
        else if (key == "alwaysontop") { if (parseBool(val, b)) d.alwaysOnTop = b; }
        else if (key == "mainx") { if (parseInt(val, i)) { d.mainX = std::clamp(i, -32000, 32000); gotX = true; } }
        else if (key == "mainy") { if (parseInt(val, i)) { d.mainY = std::clamp(i, -32000, 32000); gotY = true; } }
        else if (key == "eqpreamp") { if (parseFloat(val, f)) d.eqPreamp = clampf(f, -12.0f, 12.0f); }
        else if (key.size() == 3 && key.compare(0, 2, "eq") == 0 && key[2] >= '0' && key[2] <= '9') {
            if (parseFloat(val, f)) d.eqBands[key[2] - '0'] = clampf(f, -12.0f, 12.0f);
        }
        // unknown keys are ignored
    }

    if (gotX && gotY) d.hasMainPos = positionLooksSane(d.mainX, d.mainY);
    data = std::move(d);
    return true;
}

bool Settings::save() const {
    const std::wstring dir = configDir();
    if (dir.empty()) return false;

    const SettingsData& d = data;
    std::string out;
    auto put = [&out](const char* key, const std::string& v) {
        out += key;
        out += '=';
        out += oneLine(v);
        out += "\r\n";
    };
    auto putBool = [&put](const char* key, bool v) { put(key, v ? "1" : "0"); };

    out += "# wnamp settings (UTF-8)\r\n";
    put("skin", toUtf8(d.skinPath));
    put("lastdir", toUtf8(d.lastDir));
    put("volume", fmtFloat(clampf(d.volume, 0.0f, 1.0f)));
    put("balance", fmtFloat(clampf(d.balance, -1.0f, 1.0f)));
    putBool("shuffle", d.shuffle);
    putBool("repeat", d.repeat);
    putBool("shade", d.shade);
    putBool("eqvisible", d.eqVisible);
    putBool("plvisible", d.plVisible);
    putBool("eqenabled", d.eqEnabled);
    putBool("alwaysontop", d.alwaysOnTop);
    if (d.hasMainPos) {
        put("mainx", std::to_string(d.mainX));
        put("mainy", std::to_string(d.mainY));
    }
    put("eqpreamp", fmtFloat(clampf(d.eqPreamp, -12.0f, 12.0f)));
    for (int i = 0; i < 10; ++i) {
        const std::string key = "eq" + std::to_string(i);
        put(key.c_str(), fmtFloat(clampf(d.eqBands[i], -12.0f, 12.0f)));
    }
    return writeFileAtomic(joinPath(dir, L"settings.ini"), out);
}
