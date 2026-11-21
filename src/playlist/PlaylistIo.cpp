#include "playlist/PlaylistIo.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace {

std::wstring lowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring utf8ToWide(const std::string& s, bool* valid = nullptr) {
    if (valid) *valid = true;
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (n == 0) {  // not valid UTF-8: old .m3u files use the ANSI code page
        if (valid) *valid = false;
        cp = CP_ACP;
        flags = 0;
        n = MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (n == 0) return {};
    }
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    const size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::wstring> splitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        lines.push_back(trim(text.substr(start, end - start)));
        start = end + 1;
    }
    return lines;
}

bool readFile(const std::wstring& path, std::string& out) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);  // UTF-8 BOM
    return true;
}

// Resolves an entry against the playlist's folder unless it is absolute or a URL.
std::wstring resolveEntry(const std::wstring& entry, const std::filesystem::path& baseDir) {
    if (entry.find(L"://") != std::wstring::npos) return entry;  // stream URL
    std::wstring e = entry;
    std::replace(e.begin(), e.end(), L'/', L'\\');
    std::filesystem::path p(e);
    if (p.is_absolute()) return p.wstring();
    if (p.has_root_name() || p.has_root_directory()) return p.wstring();  // e.g. "\music\a.mp3"
    return (baseDir / p).lexically_normal().wstring();
}

void parseM3u(const std::wstring& text, const std::filesystem::path& baseDir, std::vector<std::wstring>& out) {
    for (const auto& line : splitLines(text)) {
        if (line.empty() || line[0] == L'#') continue;  // #EXTM3U / #EXTINF / comments
        out.push_back(resolveEntry(line, baseDir));
    }
}

void parsePls(const std::wstring& text, const std::filesystem::path& baseDir, std::vector<std::wstring>& out) {
    std::map<int, std::wstring> files;  // ordered by entry number
    for (const auto& line : splitLines(text)) {
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key = lowerW(trim(line.substr(0, eq)));
        if (key.size() <= 4 || key.compare(0, 4, L"file") != 0) continue;
        int num = 0;
        bool ok = true;
        for (size_t i = 4; i < key.size(); ++i) {
            if (key[i] < L'0' || key[i] > L'9') { ok = false; break; }
            num = num * 10 + (key[i] - L'0');
            if (num > 1000000) { ok = false; break; }
        }
        if (!ok) continue;
        const std::wstring value = trim(line.substr(eq + 1));
        if (!value.empty()) files[num] = resolveEntry(value, baseDir);
    }
    for (auto& kv : files) out.push_back(std::move(kv.second));
}

bool isPls(const std::wstring& path) {
    return lowerW(std::filesystem::path(path).extension().wstring()) == L".pls";
}

}  // namespace

bool loadPlaylistFile(const std::wstring& path, std::vector<std::wstring>& outPaths) {
    std::string bytes;
    if (!readFile(path, bytes)) return false;
    const std::wstring text = utf8ToWide(bytes);
    const std::filesystem::path baseDir = std::filesystem::path(path).parent_path();
    if (isPls(path)) parsePls(text, baseDir, outPaths);
    else parseM3u(text, baseDir, outPaths);
    return true;
}

bool savePlaylistFile(const std::wstring& path, const Playlist& pl) {
    std::string out;
    const size_t n = pl.size();
    if (isPls(path)) {
        out += "[playlist]\r\n";
        for (size_t i = 0; i < n; ++i) {
            const PlaylistEntry& e = pl.at(i);
            const std::string idx = std::to_string(i + 1);
            out += "File" + idx + "=" + wideToUtf8(e.path) + "\r\n";
            out += "Title" + idx + "=" + wideToUtf8(e.title) + "\r\n";
            out += "Length" + idx + "=" + std::to_string(e.durationSec >= 0 ? static_cast<long long>(e.durationSec + 0.5) : -1LL) + "\r\n";
        }
        out += "NumberOfEntries=" + std::to_string(n) + "\r\nVersion=2\r\n";
    } else {
        out += "#EXTM3U\r\n";
        for (size_t i = 0; i < n; ++i) {
            const PlaylistEntry& e = pl.at(i);
            if (e.durationSec >= 0)
                out += "#EXTINF:" + std::to_string(static_cast<long long>(e.durationSec + 0.5)) + "," +
                       wideToUtf8(e.title) + "\r\n";
            out += wideToUtf8(e.path) + "\r\n";
        }
    }
    std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}
