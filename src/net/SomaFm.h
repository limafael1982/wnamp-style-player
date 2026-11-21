#pragma once
#include <string>
#include <vector>

namespace net {
struct RadioChannel {
    std::wstring id, title, description, genre;
    std::wstring playlistUrl;  // .pls (best-quality mp3 when available)
};

// Both are blocking: call from a worker thread.
bool fetchSomaChannels(std::vector<RadioChannel>& out);
bool resolveStreamUrl(const std::wstring& playlistUrl, std::wstring& streamUrl);  // .pls/.m3u -> first stream URL
}
