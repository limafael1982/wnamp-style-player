#pragma once
#include <string>
#include <vector>

#include "playlist/Playlist.h"

// Reads .m3u / .m3u8 / .pls (chosen by extension; unknown extensions are tried as m3u).
// Relative entries are resolved against the playlist file's folder. Returns false if the
// file can't be read.
bool loadPlaylistFile(const std::wstring& path, std::vector<std::wstring>& outPaths);

// Writes .pls if the extension is .pls, otherwise .m3u8 (UTF-8, with #EXTINF when the
// duration is known). Returns false on I/O failure.
bool savePlaylistFile(const std::wstring& path, const Playlist& pl);
