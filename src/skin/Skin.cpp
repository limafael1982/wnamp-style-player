#include "skin/Skin.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <array>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "miniz.h"

using Microsoft::WRL::ComPtr;

namespace {

std::wstring toWide(const std::string& narrow) {
    if (narrow.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, narrow.data(), (int)narrow.size(), nullptr, 0);
    UINT codePage = CP_UTF8;
    if (wideLen == 0) {  // not valid UTF-8 (old zips use the OEM code page)
        codePage = CP_ACP;
        wideLen = MultiByteToWideChar(codePage, 0, narrow.data(), (int)narrow.size(), nullptr, 0);
    }
    std::wstring wide(size_t(wideLen), L'\0');
    MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, narrow.data(), (int)narrow.size(),
                         wide.data(), wideLen);
    return wide;
}

std::wstring lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

std::string lowerA(std::string text) {
    for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

// Decodes BMP/PNG bytes with WIC into opaque premultiplied BGRA.
// We convert to 24bpp BGR first: 32bpp BMPs without a real alpha channel
// decode with alpha=0, which would wipe the colours on a premultiplied convert.
bool decodeImage(const uint8_t* data, size_t size, Image& out) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return false;

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size))))
        return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                WICDecodeMetadataCacheOnDemand, &decoder)))
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;

    UINT width = 0, height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 || width > 8192 || height > 8192)
        return false;

    const UINT stride = width * 3;
    std::vector<BYTE> bgr(size_t(stride) * height);
    if (FAILED(converter->CopyPixels(nullptr, stride, (UINT)bgr.size(), bgr.data()))) return false;

    out.w = (int)width;
    out.h = (int)height;
    out.px.resize(size_t(width) * height);
    for (size_t i = 0; i < out.px.size(); ++i) {
        const BYTE* pixel = &bgr[i * 3];
        out.px[i] = 0xFF000000u | (uint32_t(pixel[2]) << 16) | (uint32_t(pixel[1]) << 8) | uint32_t(pixel[0]);
    }
    return true;
}

// Extracts every integer (optionally negative) from a string.
std::vector<int> parseInts(const std::string& text) {
    std::vector<int> out;
    for (size_t i = 0; i < text.size();) {
        bool negative = text[i] == '-' && i + 1 < text.size() && std::isdigit((unsigned char)text[i + 1]);
        if (negative || std::isdigit((unsigned char)text[i])) {
            size_t j = i + (negative ? 1 : 0);
            long value = 0;
            while (j < text.size() && std::isdigit((unsigned char)text[j])) {
                value = value * 10 + (text[j] - '0');
                if (value > 100000) value = 100000;
                ++j;
            }
            out.push_back(int(negative ? -value : value));
            i = j;
        } else {
            ++i;
        }
    }
    return out;
}

std::string trim(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// region.txt: [Section] / NumPoints=n1,n2,... / PointList=x,y x,y ...
void parseRegion(const std::string& text, std::map<std::string, std::vector<SkinPolygon>>& out) {
    struct SectionData { std::vector<int> counts, coords; };
    std::map<std::string, SectionData> sections;
    std::string section;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') {
            size_t closeBracket = line.find(']');
            section = lowerA(trim(line.substr(1, closeBracket == std::string::npos ? std::string::npos
                                                                                    : closeBracket - 1)));
            continue;
        }
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos || section.empty()) continue;
        std::string key = lowerA(trim(line.substr(0, eqPos)));
        std::string val = line.substr(eqPos + 1);
        if (key == "numpoints") sections[section].counts = parseInts(val);
        else if (key == "pointlist") sections[section].coords = parseInts(val);
    }
    for (auto& [name, sectionData] : sections) {
        std::vector<SkinPolygon> polys;
        size_t coordIndex = 0;
        for (int pointCount : sectionData.counts) {
            SkinPolygon poly;
            for (int j = 0; j < pointCount && coordIndex + 1 < sectionData.coords.size(); ++j, coordIndex += 2)
                poly.push_back(POINT{sectionData.coords[coordIndex], sectionData.coords[coordIndex + 1]});
            if (poly.size() >= 3) polys.push_back(std::move(poly));
        }
        if (!polys.empty()) out[name] = std::move(polys);
    }
}

bool parseHexColor(std::string text, COLORREF& out) {
    text = trim(text);
    if (!text.empty() && text[0] == '#') text.erase(0, 1);
    if (text.size() != 6) return false;
    unsigned long value = 0;
    for (char ch : text) {
        int digit;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else return false;
        value = value * 16 + static_cast<unsigned long>(digit);
    }
    out = RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return true;
}

// pledit.txt: [Text] / Normal=#00FF00 / Current / NormalBG / SelectedBG / Font=Arial
void parsePlaylistStyle(const std::string& text, PlaylistStyle& style) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        size_t eqPos = line.find('=');
        if (line.empty() || line[0] == '[' || line[0] == ';' || eqPos == std::string::npos) continue;
        const std::string key = lowerA(trim(line.substr(0, eqPos)));
        const std::string val = trim(line.substr(eqPos + 1));
        if (key == "normal") parseHexColor(val, style.normal);
        else if (key == "current") parseHexColor(val, style.current);
        else if (key == "normalbg") parseHexColor(val, style.normalBg);
        else if (key == "selectedbg") parseHexColor(val, style.selectedBg);
        else if (key == "font" && !val.empty()) style.font = toWide(val);
    }
}

const std::array<COLORREF, 24> kDefaultVisColors = {
    RGB(0, 0, 0),       RGB(24, 33, 41),    RGB(239, 49, 16),   RGB(206, 41, 16),
    RGB(214, 90, 0),    RGB(214, 102, 0),   RGB(214, 115, 0),   RGB(198, 123, 8),
    RGB(222, 165, 24),  RGB(214, 181, 33),  RGB(189, 222, 41),  RGB(148, 222, 33),
    RGB(41, 206, 16),   RGB(50, 190, 16),   RGB(57, 181, 16),   RGB(49, 156, 8),
    RGB(41, 148, 0),    RGB(24, 132, 8),    RGB(255, 255, 255), RGB(214, 214, 222),
    RGB(181, 189, 189), RGB(160, 170, 175), RGB(148, 156, 165), RGB(150, 150, 150)};

// viscolor.txt: one colour per line, "R,G,B" optionally followed by a comment.
void parseVisColors(const std::string& text, std::array<COLORREF, 24>& out) {
    std::istringstream in(text);
    std::string line;
    size_t count = 0;
    while (count < out.size() && std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line.compare(0, 2, "//") == 0) continue;
        const std::vector<int> components = parseInts(line);
        if (components.size() < 3) continue;
        auto clampByte = [](int x) { return static_cast<BYTE>(std::min(255, std::max(0, x))); };
        out[count++] = RGB(clampByte(components[0]), clampByte(components[1]), clampByte(components[2]));
    }
}

bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool hasMainBmp(const std::filesystem::path& dir) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc)) continue;
        if (lower(it->path().filename().wstring()) == L"main.bmp") return true;
    }
    return false;
}

}  // namespace

Skin::Skin() { reset(); }

void Skin::reset() {
    images_.clear();
    regions_.clear();
    plStyle_ = PlaylistStyle{};
    visColors_ = kDefaultVisColors;
}

void Skin::ingest(const std::wstring& base, const uint8_t* data, size_t size) {
    std::filesystem::path basePath(base);
    const std::wstring ext = basePath.extension().wstring();
    if (ext == L".bmp" || ext == L".png") {
        Image img;
        if (decodeImage(data, size, img)) images_.emplace(basePath.stem().wstring(), std::move(img));
    } else if (base == L"region.txt") {
        parseRegion(std::string(reinterpret_cast<const char*>(data), size), regions_);
    } else if (base == L"pledit.txt") {
        parsePlaylistStyle(std::string(reinterpret_cast<const char*>(data), size), plStyle_);
    } else if (base == L"viscolor.txt") {
        parseVisColors(std::string(reinterpret_cast<const char*>(data), size), visColors_);
    }
}

bool Skin::finish(std::wstring& err, const wchar_t* what) {
    if (!images_.count(L"main")) {
        err = std::wstring(what) + L" has no usable main.bmp.";
        return false;
    }
    return true;
}

bool Skin::loadFromFile(const std::wstring& path, std::wstring& err) {
    reset();

    std::vector<uint8_t> bytes;
    if (!readFileBytes(std::filesystem::path(path), bytes)) { err = L"Cannot open file."; return false; }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        err = L"Not a valid zip/.wsz archive.";
        return false;
    }

    // Skins sometimes nest files in a folder and filename case varies, so match by
    // lowercase basename and prefer the shallowest copy.
    struct Entry { mz_uint zipIndex; int depth; };
    std::map<std::wstring, Entry> best;
    const mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < fileCount; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        char name[512];
        if (!mz_zip_reader_get_filename(&zip, i, name, sizeof name)) continue;
        std::string fullName = name;
        int depth = int(std::count_if(fullName.begin(), fullName.end(),
                                       [](char ch) { return ch == '/' || ch == '\\'; }));
        size_t sep = fullName.find_last_of("/\\");
        std::wstring base = lower(toWide(sep == std::string::npos ? fullName : fullName.substr(sep + 1)));
        auto it = best.find(base);
        if (it == best.end() || depth < it->second.depth) best[base] = Entry{i, depth};
    }

    for (const auto& [base, entry] : best) {
        size_t extractedSize = 0;
        void* heap = mz_zip_reader_extract_to_heap(&zip, entry.zipIndex, &extractedSize, 0);
        if (!heap) continue;
        ingest(base, static_cast<const uint8_t*>(heap), extractedSize);
        mz_free(heap);
    }
    mz_zip_reader_end(&zip);

    return finish(err, L"Archive");
}

bool Skin::loadFromDirectory(const std::wstring& path, std::wstring& err) {
    reset();
    namespace fs = std::filesystem;

    // lowercase file name -> (depth, full path); depth 0 = top folder, 1 = a subfolder.
    struct Found { int depth; fs::path path; };
    std::map<std::wstring, Found> best;
    auto consider = [&](const fs::path& file, int depth) {
        const std::wstring base = lower(file.filename().wstring());
        auto it = best.find(base);
        if (it == best.end() || depth < it->second.depth) best[base] = Found{depth, file};
    };

    std::error_code ec;
    const fs::path root(path);
    if (!fs::is_directory(root, ec)) { err = L"Folder not found."; return false; }
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc)) {
            consider(it->path(), 0);
        } else if (it->is_directory(entryEc)) {
            std::error_code subEc;
            for (fs::directory_iterator sub(it->path(), subEc), subEnd; !subEc && sub != subEnd;
                 sub.increment(subEc)) {
                std::error_code subEntryEc;
                if (sub->is_regular_file(subEntryEc)) consider(sub->path(), 1);
            }
        }
    }

    for (const auto& [base, found] : best) {
        const std::wstring ext = fs::path(base).extension().wstring();
        const bool wanted = ext == L".bmp" || ext == L".png" || base == L"region.txt" ||
                            base == L"pledit.txt" || base == L"viscolor.txt";
        if (!wanted) continue;
        std::vector<uint8_t> bytes;
        if (readFileBytes(found.path, bytes) && !bytes.empty()) ingest(base, bytes.data(), bytes.size());
    }
    return finish(err, L"Folder");
}

bool Skin::loadFromPath(const std::wstring& path, std::wstring& err) {
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::path(path), ec)) return loadFromDirectory(path, err);
    return loadFromFile(path, err);
}

const Image* Skin::image(const std::wstring& stem) const {
    auto it = images_.find(stem);
    return it == images_.end() ? nullptr : &it->second;
}

const std::vector<SkinPolygon>* Skin::region(const std::string& section) const {
    auto it = regions_.find(section);
    return it == regions_.end() ? nullptr : &it->second;
}

std::vector<SkinEntry> listSkins(const std::vector<std::wstring>& dirs) {
    namespace fs = std::filesystem;
    std::vector<SkinEntry> out;
    std::map<std::wstring, bool> seen;
    for (const auto& dir : dirs) {
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code entryEc;
            const fs::path entryPath = it->path();
            std::wstring name;
            if (it->is_regular_file(entryEc)) {
                const std::wstring ext = lower(entryPath.extension().wstring());
                if (ext != L".wsz" && ext != L".zip") continue;
                name = entryPath.stem().wstring();
            } else if (it->is_directory(entryEc)) {
                if (!hasMainBmp(entryPath)) continue;
                name = entryPath.filename().wstring();
            } else {
                continue;
            }
            if (!seen.emplace(lower(name), true).second) continue;
            out.push_back(SkinEntry{name, entryPath.wstring()});
        }
    }
    std::sort(out.begin(), out.end(), [](const SkinEntry& lhs, const SkinEntry& rhs) {
        return lower(lhs.name) < lower(rhs.name);
    });
    return out;
}
