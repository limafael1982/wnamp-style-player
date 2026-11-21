#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Opaque-by-default RGBA image. Pixels are premultiplied BGRA, top-down,
// alpha forced to 255 (Winamp 2 skins have no alpha; transparency comes
// from region.txt).
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;  // 0xAARRGGBB, premultiplied
    bool valid() const { return w > 0 && h > 0 && px.size() == size_t(w) * size_t(h); }
};

using SkinPolygon = std::vector<POINT>;

// Colours/font from pledit.txt (playlist editor). Defaults match the classic Winamp skin.
struct PlaylistStyle {
    COLORREF normal = RGB(0x00, 0xFF, 0x00);
    COLORREF current = RGB(0xFF, 0xFF, 0xFF);
    COLORREF normalBg = RGB(0x00, 0x00, 0x00);
    COLORREF selectedBg = RGB(0x00, 0x00, 0xFF);
    std::wstring font = L"Arial";
};

class Skin {
public:
    Skin();

    // Loads a .wsz (zip). Returns false and fills `err` on failure.
    bool loadFromFile(const std::wstring& path, std::wstring& err);

    // Loads a .wsz/.zip file OR an unpacked skin directory (top folder plus one
    // level of subfolders, case-insensitive file names, shallowest copy wins).
    bool loadFromPath(const std::wstring& path, std::wstring& err);

    // Look up a bitmap by lowercase stem, e.g. L"main", L"cbuttons". nullptr if absent.
    const Image* image(const std::wstring& stem) const;

    // Region polygons for a section of region.txt, lowercase name ("normal",
    // "windowshade", "equalizer", ...). nullptr if the skin has none.
    const std::vector<SkinPolygon>* region(const std::string& section) const;

    // Playlist colours/font (pledit.txt); defaults if the skin has none.
    const PlaylistStyle& playlistStyle() const { return plStyle_; }

    // viscolor.txt: 24 colours (0 = background, 1 = dots, 2..17 = spectrum bottom->top,
    // 18..22 = oscilloscope, 23 = peaks). Classic-looking defaults if the skin has none.
    const std::array<COLORREF, 24>& visColors() const { return visColors_; }

private:
    void reset();
    // Takes one file from the skin (lowercase base name) and stores what we understand.
    void ingest(const std::wstring& base, const uint8_t* data, size_t size);
    bool finish(std::wstring& err, const wchar_t* what);
    bool loadFromDirectory(const std::wstring& path, std::wstring& err);

    PlaylistStyle plStyle_;
    std::array<COLORREF, 24> visColors_{};
    std::map<std::wstring, Image> images_;
    std::map<std::string, std::vector<SkinPolygon>> regions_;
};

// A skin found on disk (archive or unpacked folder).
struct SkinEntry {
    std::wstring name, path;
};

// Lists .wsz/.zip files and sub-folders containing main.bmp inside `dirs`, sorted by
// name (case-insensitive); duplicate names (case-insensitive) keep the first one found.
std::vector<SkinEntry> listSkins(const std::vector<std::wstring>& dirs);
