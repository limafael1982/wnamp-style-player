#include "playlist/PlaylistIo.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace {

std::wstring lowerW(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

std::wstring utf8ToWide(const std::string& narrow, bool* valid = nullptr) {
    if (valid) *valid = true;
    if (narrow.empty()) return {};
    int wideLen =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, narrow.data(), static_cast<int>(narrow.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (wideLen == 0) {  // not valid UTF-8: old .m3u files use the ANSI code page
        if (valid) *valid = false;
        codePage = CP_ACP;
        flags = 0;
        wideLen = MultiByteToWideChar(codePage, flags, narrow.data(), static_cast<int>(narrow.size()), nullptr, 0);
        if (wideLen == 0) return {};
    }
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(codePage, flags, narrow.data(), static_cast<int>(narrow.size()), wide.data(), wideLen);
    return wide;
}

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int byteLen =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(byteLen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), byteLen, nullptr,
                         nullptr);
    return narrow;
}

std::wstring trim(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
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
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);  // UTF-8 BOM
    return true;
}

// Resolves an entry against the playlist's folder unless it is absolute or a URL.
std::wstring resolveEntry(const std::wstring& entry, const std::filesystem::path& baseDir) {
    if (entry.find(L"://") != std::wstring::npos) return entry;  // stream URL
    std::wstring normalized = entry;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    std::filesystem::path entryPath(normalized);
    if (entryPath.is_absolute()) return entryPath.wstring();
    if (entryPath.has_root_name() || entryPath.has_root_directory()) return entryPath.wstring();  // e.g. "\music\a.mp3"
    return (baseDir / entryPath).lexically_normal().wstring();
}

void parseM3u(const std::wstring& text, const std::filesystem::path& baseDir, std::vector<std::wstring>& out) {
    for (const auto& line : splitLines(text)) {
        if (line.empty() || line[0] == L'#') continue;  // #EXTM3U / #EXTINF / comments
        out.push_back(resolveEntry(line, baseDir));
    }
}

void parsePls(const std::wstring& text, const std::filesystem::path& baseDir, std::vector<std::wstring>& out) {
    std::map<int, std::wstring> filesByIndex;  // ordered by entry number
    for (const auto& line : splitLines(text)) {
        const size_t eqPos = line.find(L'=');
        if (eqPos == std::wstring::npos) continue;
        const std::wstring key = lowerW(trim(line.substr(0, eqPos)));
        if (key.size() <= 4 || key.compare(0, 4, L"file") != 0) continue;
        int entryNumber = 0;
        bool validNumber = true;
        for (size_t i = 4; i < key.size(); ++i) {
            if (key[i] < L'0' || key[i] > L'9') { validNumber = false; break; }
            entryNumber = entryNumber * 10 + (key[i] - L'0');
            if (entryNumber > 1000000) { validNumber = false; break; }
        }
        if (!validNumber) continue;
        const std::wstring value = trim(line.substr(eqPos + 1));
        if (!value.empty()) filesByIndex[entryNumber] = resolveEntry(value, baseDir);
    }
    for (auto& entry : filesByIndex) out.push_back(std::move(entry.second));
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

bool savePlaylistFile(const std::wstring& path, const Playlist& playlist) {
    std::string out;
    const size_t count = playlist.size();
    if (isPls(path)) {
        out += "[playlist]\r\n";
        for (size_t i = 0; i < count; ++i) {
            const PlaylistEntry& entry = playlist.at(i);
            const std::string entryIndex = std::to_string(i + 1);
            out += "File" + entryIndex + "=" + wideToUtf8(entry.path) + "\r\n";
            out += "Title" + entryIndex + "=" + wideToUtf8(entry.title) + "\r\n";
            out += "Length" + entryIndex + "=" +
                   std::to_string(entry.durationSec >= 0 ? static_cast<long long>(entry.durationSec + 0.5) : -1LL) +
                   "\r\n";
        }
        out += "NumberOfEntries=" + std::to_string(count) + "\r\nVersion=2\r\n";
    } else {
        out += "#EXTM3U\r\n";
        for (size_t i = 0; i < count; ++i) {
            const PlaylistEntry& entry = playlist.at(i);
            if (entry.durationSec >= 0)
                out += "#EXTINF:" + std::to_string(static_cast<long long>(entry.durationSec + 0.5)) + "," +
                       wideToUtf8(entry.title) + "\r\n";
            out += wideToUtf8(entry.path) + "\r\n";
        }
    }
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}
