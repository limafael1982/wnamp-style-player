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

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    UINT cp = CP_UTF8;
    if (n == 0) {  // not valid UTF-8 (old zips use the OEM code page)
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    }
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::string lowerA(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
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

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;

    UINT w = 0, h = 0;
    if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    const UINT stride = w * 3;
    std::vector<BYTE> bgr(size_t(stride) * h);
    if (FAILED(conv->CopyPixels(nullptr, stride, (UINT)bgr.size(), bgr.data()))) return false;

    out.w = (int)w;
    out.h = (int)h;
    out.px.resize(size_t(w) * h);
    for (size_t i = 0; i < out.px.size(); ++i) {
        const BYTE* p = &bgr[i * 3];
        out.px[i] = 0xFF000000u | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]);
    }
    return true;
}

// Extracts every integer (optionally negative) from a string.
std::vector<int> parseInts(const std::string& s) {
    std::vector<int> v;
    for (size_t i = 0; i < s.size();) {
        bool neg = s[i] == '-' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]);
        if (neg || std::isdigit((unsigned char)s[i])) {
            size_t j = i + (neg ? 1 : 0);
            long val = 0;
            while (j < s.size() && std::isdigit((unsigned char)s[j])) {
                val = val * 10 + (s[j] - '0');
                if (val > 100000) val = 100000;
                ++j;
            }
            v.push_back(int(neg ? -val : val));
            i = j;
        } else {
            ++i;
        }
    }
    return v;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// region.txt: [Section] / NumPoints=n1,n2,... / PointList=x,y x,y ...
void parseRegion(const std::string& text, std::map<std::string, std::vector<SkinPolygon>>& out) {
    struct Tmp { std::vector<int> counts, nums; };
    std::map<std::string, Tmp> tmp;
    std::string section;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') {
            size_t e = line.find(']');
            section = lowerA(trim(line.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) continue;
        std::string key = lowerA(trim(line.substr(0, eq)));
        std::string val = line.substr(eq + 1);
        if (key == "numpoints") tmp[section].counts = parseInts(val);
        else if (key == "pointlist") tmp[section].nums = parseInts(val);
    }
    for (auto& [name, t] : tmp) {
        std::vector<SkinPolygon> polys;
        size_t k = 0;
        for (int c : t.counts) {
            SkinPolygon poly;
            for (int j = 0; j < c && k + 1 < t.nums.size(); ++j, k += 2)
                poly.push_back(POINT{t.nums[k], t.nums[k + 1]});
            if (poly.size() >= 3) polys.push_back(std::move(poly));
        }
        if (!polys.empty()) out[name] = std::move(polys);
    }
}

bool parseHexColor(std::string v, COLORREF& out) {
    v = trim(v);
    if (!v.empty() && v[0] == '#') v.erase(0, 1);
    if (v.size() != 6) return false;
    unsigned long n = 0;
    for (char c : v) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        n = n * 16 + static_cast<unsigned long>(d);
    }
    out = RGB((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF);
    return true;
}

// pledit.txt: [Text] / Normal=#00FF00 / Current / NormalBG / SelectedBG / Font=Arial
void parsePlaylistStyle(const std::string& text, PlaylistStyle& st) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '[' || line[0] == ';' || eq == std::string::npos) continue;
        const std::string key = lowerA(trim(line.substr(0, eq)));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "normal") parseHexColor(val, st.normal);
        else if (key == "current") parseHexColor(val, st.current);
        else if (key == "normalbg") parseHexColor(val, st.normalBg);
        else if (key == "selectedbg") parseHexColor(val, st.selectedBg);
        else if (key == "font" && !val.empty()) st.font = toWide(val);
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
    size_t n = 0;
    while (n < out.size() && std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line.compare(0, 2, "//") == 0) continue;
        const std::vector<int> v = parseInts(line);
        if (v.size() < 3) continue;
        auto c = [](int x) { return static_cast<BYTE>(std::min(255, std::max(0, x))); };
        out[n++] = RGB(c(v[0]), c(v[1]), c(v[2]));
    }
}

bool readFileBytes(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool hasMainBmp(const std::filesystem::path& dir) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
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
    std::filesystem::path p(base);
    const std::wstring ext = p.extension().wstring();
    if (ext == L".bmp" || ext == L".png") {
        Image img;
        if (decodeImage(data, size, img)) images_.emplace(p.stem().wstring(), std::move(img));
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
    struct Entry { mz_uint idx; int depth; };
    std::map<std::wstring, Entry> best;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        char name[512];
        if (!mz_zip_reader_get_filename(&zip, i, name, sizeof name)) continue;
        std::string s = name;
        int depth = int(std::count_if(s.begin(), s.end(), [](char c) { return c == '/' || c == '\\'; }));
        size_t sep = s.find_last_of("/\\");
        std::wstring base = lower(toWide(sep == std::string::npos ? s : s.substr(sep + 1)));
        auto it = best.find(base);
        if (it == best.end() || depth < it->second.depth) best[base] = Entry{i, depth};
    }

    for (const auto& [base, e] : best) {
        size_t sz = 0;
        void* heap = mz_zip_reader_extract_to_heap(&zip, e.idx, &sz, 0);
        if (!heap) continue;
        ingest(base, static_cast<const uint8_t*>(heap), sz);
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
        std::error_code ec2;
        if (it->is_regular_file(ec2)) {
            consider(it->path(), 0);
        } else if (it->is_directory(ec2)) {
            std::error_code ec3;
            for (fs::directory_iterator sub(it->path(), ec3), subEnd; !ec3 && sub != subEnd; sub.increment(ec3)) {
                std::error_code ec4;
                if (sub->is_regular_file(ec4)) consider(sub->path(), 1);
            }
        }
    }

    for (const auto& [base, f] : best) {
        const std::wstring ext = fs::path(base).extension().wstring();
        const bool wanted = ext == L".bmp" || ext == L".png" || base == L"region.txt" ||
                            base == L"pledit.txt" || base == L"viscolor.txt";
        if (!wanted) continue;
        std::vector<uint8_t> bytes;
        if (readFileBytes(f.path, bytes) && !bytes.empty()) ingest(base, bytes.data(), bytes.size());
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
    for (const auto& d : dirs) {
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(d), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            const fs::path p = it->path();
            std::wstring name;
            if (it->is_regular_file(ec2)) {
                const std::wstring ext = lower(p.extension().wstring());
                if (ext != L".wsz" && ext != L".zip") continue;
                name = p.stem().wstring();
            } else if (it->is_directory(ec2)) {
                if (!hasMainBmp(p)) continue;
                name = p.filename().wstring();
            } else {
                continue;
            }
            if (!seen.emplace(lower(name), true).second) continue;
            out.push_back(SkinEntry{name, p.wstring()});
        }
    }
    std::sort(out.begin(), out.end(), [](const SkinEntry& a, const SkinEntry& b) {
        return lower(a.name) < lower(b.name);
    });
    return out;
}
