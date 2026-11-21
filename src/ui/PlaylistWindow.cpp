#include "ui/PlaylistWindow.h"

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <set>
#include <vector>
#include <string>

#include "playlist/PlaylistIo.h"
#include "skin/SpritesPl.h"

using namespace sprites_pl;

namespace {

constexpr wchar_t kClassName[] = L"WnampStylePlaylistWindow";
constexpr UINT_PTR kDirtyTimer = 1;

enum MenuId : UINT {
    kMenuSortTitle = 1, kMenuSortPath, kMenuSortFilename, kMenuReverse, kMenuRandomize,
    kMenuSelectAll, kMenuSelectNone, kMenuInvertSel, kMenuSaveList, kMenuClear,
    kMenuRowPlay, kMenuRowRemove,
};

int scaleForDpi(UINT dpi) { return std::max(1, static_cast<int>((dpi + 48) / 96)); }

int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

uint32_t pixelOf(COLORREF c) {
    return 0xFF000000u | (uint32_t(GetRValue(c)) << 16) | (uint32_t(GetGValue(c)) << 8) | uint32_t(GetBValue(c));
}

std::wstring fmtTime(double sec) {
    const int t = std::max(0, static_cast<int>(sec + 0.5));
    wchar_t buf[32];
    swprintf_s(buf, L"%d:%02d", t / 60, t % 60);
    return buf;
}

std::wstring lowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

}  // namespace

PlaylistWindow::PlaylistWindow(Playlist& playlist) : playlist_(playlist) {}

PlaylistWindow::~PlaylistWindow() {
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    if (font_) DeleteObject(font_);
    if (memDC_) {
        if (oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteDC(memDC_);
    }
    if (dib_) DeleteObject(dib_);
}

bool PlaylistWindow::create(HINSTANCE hinst, HWND owner) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &PlaylistWindow::ThunkProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    memDC_ = CreateCompatibleDC(nullptr);
    if (!memDC_) return false;

    scale_ = scaleForDpi(GetDpiForSystem());
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kClassName, L"Playlist", WS_POPUP,
                            100, 300, widthSkin_ * scale_, heightSkin_ * scale_, owner, nullptr, hinst, this);
    if (!hwnd_) return false;

    scale_ = scaleForDpi(GetDpiForWindow(hwnd_));
    DragAcceptFiles(hwnd_, TRUE);
    rebuildFont();
    resizeWindowAndSurface();
    render();
    return true;
}

void PlaylistWindow::setSkin(std::shared_ptr<Skin> skin) {
    skin_ = std::move(skin);
    rebuildFont();
    invalidate();
}

void PlaylistWindow::setScale(int scale) {
    scale = std::max(1, scale);
    if (scale == scale_) return;
    scale_ = scale;
    rebuildFont();
    resizeWindowAndSurface();
    invalidate();
}

void PlaylistWindow::show(bool v) {
    if (!hwnd_) return;
    ShowWindow(hwnd_, v ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (v) render();
}

bool PlaylistWindow::visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

void PlaylistWindow::moveTo(int x, int y) {
    if (hwnd_) SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void PlaylistWindow::getRect(RECT& rc) const {
    if (hwnd_) GetWindowRect(hwnd_, &rc);
    else rc = RECT{0, 0, 0, 0};
}

void PlaylistWindow::refresh() { invalidate(); }

// ---------------------------------------------------------------- layout

int PlaylistWindow::visibleRows() const {
    return std::max(1, (heightSkin_ - kTopH - kBottomH) / kRowH);
}

bool PlaylistWindow::inList(int sx, int sy) const {
    return sx >= kLeftW && sx < widthSkin_ - kRightW && sy >= kTopH && sy < heightSkin_ - kBottomH;
}

int PlaylistWindow::rowAt(int sx, int sy) const {
    if (!inList(sx, sy)) return -1;
    const int idx = scroll_ + (sy - kTopH) / kRowH;
    return idx < static_cast<int>(playlist_.size()) ? idx : -1;
}

PlaylistWindow::Btn PlaylistWindow::hitButton(int sx, int sy) const {
    if (sx >= widthSkin_ - kCloseFromRight && sx < widthSkin_ - kCloseFromRight + kCloseSize &&
        sy >= kCloseY && sy < kCloseY + kCloseSize)
        return Btn::Close;
    if (sx >= widthSkin_ - kGripSize && sy >= heightSkin_ - kGripSize) return Btn::Grip;
    const int by = heightSkin_ - kBtnFromBottom;
    if (sy >= by && sy < by + kBtnH) {
        auto in = [&](int x) { return sx >= x && sx < x + kBtnW; };
        if (in(widthSkin_ - kLoadFromRight)) return Btn::Load;
        if (in(kAddX)) return Btn::Add;
        if (in(kRemX)) return Btn::Rem;
        if (in(kSelX)) return Btn::Sel;
        if (in(kMiscX)) return Btn::Misc;
    }
    return Btn::None;
}

void PlaylistWindow::activateButton(Btn b) {
    switch (b) {
        case Btn::Close:
            show(false);
            if (onClosed_) onClosed_();
            break;
        case Btn::Add:
            addFilesDialog();
            break;
        case Btn::Rem:
            removeSelected();
            break;
        case Btn::Sel:  // select all
            selected_.clear();
            for (int i = 0; i < static_cast<int>(playlist_.size()); ++i) selected_.insert(i);
            invalidate();
            break;
        case Btn::Misc:
            showMiscMenu();
            break;
        case Btn::Load:
            loadListDialog();
            break;
        default:  // Grip: handled while dragging
            break;
    }
}

// ------------------------------------------------------- selection / list

void PlaylistWindow::selectRow(int row, bool ctrl, bool shift) {
    if (shift && anchor_ >= 0) {
        selected_.clear();
        for (int i = std::min(anchor_, row); i <= std::max(anchor_, row); ++i) selected_.insert(i);
    } else if (ctrl) {
        if (!selected_.erase(row)) selected_.insert(row);
        anchor_ = row;
    } else {
        selected_.clear();
        selected_.insert(row);
        anchor_ = row;
    }
    invalidate();
}

void PlaylistWindow::clampScroll() {
    const int maxScroll = std::max(0, static_cast<int>(playlist_.size()) - visibleRows());
    scroll_ = std::clamp(scroll_, 0, maxScroll);
}

void PlaylistWindow::ensureVisible(int index) {
    const int rows = visibleRows();
    if (index < scroll_) scroll_ = index;
    else if (index >= scroll_ + rows) scroll_ = index - rows + 1;
    clampScroll();
}

void PlaylistWindow::removeSelected() {
    for (auto it = selected_.rbegin(); it != selected_.rend(); ++it)
        playlist_.removeAt(static_cast<size_t>(*it));
    selected_.clear();
    anchor_ = -1;
    clampScroll();
    invalidate();
    notifyChanged();
}

void PlaylistWindow::notifyChanged() {
    if (onChanged_) onChanged_();
}

void PlaylistWindow::resetListView() {
    selected_.clear();
    anchor_ = -1;
    scroll_ = 0;
    invalidate();
}

bool PlaylistWindow::hasSupportedExt(const std::wstring& path) const {
    if (exts_.empty()) return true;
    const std::wstring ext = lowerW(std::filesystem::path(path).extension().wstring());
    return std::find(exts_.begin(), exts_.end(), ext) != exts_.end();
}

void PlaylistWindow::collectPath(const std::wstring& path, std::vector<std::wstring>& out) const {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        std::vector<std::wstring> found;
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            if (it->is_regular_file(ec2) && hasSupportedExt(it->path().wstring()))
                found.push_back(it->path().wstring());
        }
        std::sort(found.begin(), found.end(),
                  [](const std::wstring& a, const std::wstring& b) { return lowerW(a) < lowerW(b); });
        out.insert(out.end(), found.begin(), found.end());
    } else {
        const std::wstring ext = lowerW(std::filesystem::path(path).extension().wstring());
        if (ext != L".wsz") out.push_back(path);  // skins are handled by the main window
    }
}

void PlaylistWindow::addFilesDialog() {
    std::wstring patterns;
    for (const auto& e : exts_) {
        if (!patterns.empty()) patterns += L';';
        patterns += L'*' + e;
    }
    if (patterns.empty()) patterns = L"*.*";
    std::wstring filter = L"Audio files";
    filter.push_back(L'\0');
    filter += patterns;
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');  // plus the string's own terminating null

    std::vector<wchar_t> buf(65536, L'\0');
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return;

    // Single file: "full\path". Multiple: "dir\0name1\0name2\0\0".
    const wchar_t* p = buf.data();
    const std::wstring first = p;
    p += first.size() + 1;
    std::vector<std::wstring> files;
    if (*p == L'\0') {
        files.push_back(first);
    } else {
        while (*p) {
            std::wstring name = p;
            files.push_back((std::filesystem::path(first) / name).wstring());
            p += name.size() + 1;
        }
    }
    playlist_.addMany(files);
    invalidate();
    notifyChanged();
}

// Popup above the MISC OPT button.
void PlaylistWindow::showMiscMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kMenuSortTitle, L"Sort list by title");
    AppendMenuW(menu, MF_STRING, kMenuSortPath, L"Sort list by path");
    AppendMenuW(menu, MF_STRING, kMenuSortFilename, L"Sort list by filename");
    AppendMenuW(menu, MF_STRING, kMenuReverse, L"Reverse list");
    AppendMenuW(menu, MF_STRING, kMenuRandomize, L"Randomize list");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSelectAll, L"Select all");
    AppendMenuW(menu, MF_STRING, kMenuSelectNone, L"Select none");
    AppendMenuW(menu, MF_STRING, kMenuInvertSel, L"Invert selection");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSaveList, L"Save list...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuClear, L"Remove all");

    POINT pt{kMiscX * scale_, (heightSkin_ - kBtnFromBottom) * scale_};
    ClientToScreen(hwnd_, &pt);
    SetForegroundWindow(hwnd_);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                                                       pt.x, pt.y, 0, hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    const int count = static_cast<int>(playlist_.size());
    switch (cmd) {
        case kMenuSortTitle:    playlist_.sortByTitle();    resetListView(); notifyChanged(); break;
        case kMenuSortPath:     playlist_.sortByPath();     resetListView(); notifyChanged(); break;
        case kMenuSortFilename: playlist_.sortByFilename(); resetListView(); notifyChanged(); break;
        case kMenuReverse:      playlist_.reverse();        resetListView(); notifyChanged(); break;
        case kMenuRandomize:    playlist_.randomize();      resetListView(); notifyChanged(); break;
        case kMenuSelectAll:
            selected_.clear();
            for (int i = 0; i < count; ++i) selected_.insert(i);
            invalidate();
            break;
        case kMenuSelectNone:
            selected_.clear();
            invalidate();
            break;
        case kMenuInvertSel: {
            std::set<int> inv;
            for (int i = 0; i < count; ++i)
                if (!selected_.count(i)) inv.insert(i);
            selected_ = std::move(inv);
            invalidate();
            break;
        }
        case kMenuSaveList:
            saveListDialog();
            break;
        case kMenuClear:
            playlist_.clear();
            resetListView();
            notifyChanged();
            break;
        default:
            break;
    }
}

// Right-click on a row: Play / Remove.
void PlaylistWindow::showRowMenu(int row) {
    if (row < 0) return;
    if (!selected_.count(row)) {
        selected_.clear();
        selected_.insert(row);
        anchor_ = row;
        invalidate();
    }
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kMenuRowPlay, L"Play");
    AppendMenuW(menu, MF_STRING, kMenuRowRemove, L"Remove");
    SetMenuDefaultItem(menu, kMenuRowPlay, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                                       pt.x, pt.y, 0, hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd == kMenuRowPlay) {
        if (onPlayIndex_) onPlayIndex_(row);
    } else if (cmd == kMenuRowRemove) {
        removeSelected();
    }
}

// LOAD LIST: replaces the whole playlist with the entries of a .m3u/.m3u8/.pls file.
void PlaylistWindow::loadListDialog() {
    std::wstring filter = L"Playlists (*.m3u;*.m3u8;*.pls)";
    filter.push_back(L'\0');
    filter += L"*.m3u;*.m3u8;*.pls";
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');

    std::vector<wchar_t> buf(MAX_PATH * 4, L'\0');
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrTitle = L"Load playlist";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;

    std::vector<std::wstring> paths;
    if (!loadPlaylistFile(buf.data(), paths)) {
        MessageBoxW(hwnd_, L"Could not read the playlist file.", L"Load playlist", MB_OK | MB_ICONWARNING);
        return;
    }
    playlist_.clear();
    playlist_.addMany(paths);
    resetListView();
    if (onReplaced_) onReplaced_();
    notifyChanged();
}

void PlaylistWindow::saveListDialog() {
    std::wstring filter = L"M3U playlist (*.m3u8)";
    filter.push_back(L'\0');
    filter += L"*.m3u8";
    filter.push_back(L'\0');
    filter += L"PLS playlist (*.pls)";
    filter.push_back(L'\0');
    filter += L"*.pls";
    filter.push_back(L'\0');

    std::vector<wchar_t> buf(MAX_PATH * 4, L'\0');
    wcscpy_s(buf.data(), buf.size(), L"playlist");
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrTitle = L"Save playlist";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return;

    std::wstring path = buf.data();
    if (std::filesystem::path(path).extension().empty())
        path += (ofn.nFilterIndex == 2) ? L".pls" : L".m3u8";
    if (!savePlaylistFile(path, playlist_))
        MessageBoxW(hwnd_, L"Could not write the playlist file.", L"Save playlist", MB_OK | MB_ICONWARNING);
}

// Resizes in whole classic steps while the grip is dragged.
void PlaylistWindow::resizeByDrag(int skinX, int skinY) {
    auto snap = [](int delta, int step) { return ((delta + (delta >= 0 ? step / 2 : -(step / 2))) / step) * step; };
    const int w = std::clamp(gripStartW_ + snap(skinX - gripStartX_, kStepX), kMinW, kMaxW);
    const int h = std::clamp(gripStartH_ + snap(skinY - gripStartY_, kStepY), kMinH, kMaxH);
    if (w == widthSkin_ && h == heightSkin_) return;
    widthSkin_ = w;
    heightSkin_ = h;
    resizeWindowAndSurface();
    clampScroll();
    invalidate();
}

// ---------------------------------------------------------------- drawing

void PlaylistWindow::invalidate() {
    if (!hwnd_ || dirty_) return;
    dirty_ = true;
    SetTimer(hwnd_, kDirtyTimer, 16, nullptr);  // coalesce bursts of changes into one redraw
}

void PlaylistWindow::resizeWindowAndSurface() {
    surfW_ = widthSkin_ * scale_;
    surfH_ = heightSkin_ * scale_;
    if (hwnd_)
        SetWindowPos(hwnd_, nullptr, 0, 0, surfW_, surfH_, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (oldBmp_) { SelectObject(memDC_, oldBmp_); oldBmp_ = nullptr; }
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = surfW_;
    bi.bmiHeader.biHeight = -surfH_;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_ = CreateDIBSection(memDC_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bits_ = static_cast<uint32_t*>(bits);
    if (dib_) oldBmp_ = SelectObject(memDC_, dib_);
}

void PlaylistWindow::rebuildFont() {
    if (font_) { DeleteObject(font_); font_ = nullptr; }
    static const PlaylistStyle kDefault;
    const PlaylistStyle& st = skin_ ? skin_->playlistStyle() : kDefault;
    LOGFONTW lf{};
    lf.lfHeight = -11 * scale_;  // ~8 pt at 96 dpi, scaled with the window
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    const std::wstring face = st.font.empty() ? std::wstring(L"Arial") : st.font;
    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, face.c_str(), _TRUNCATE);
    font_ = CreateFontIndirectW(&lf);
    if (!font_) {  // unknown face: fall back to Arial
        wcscpy_s(lf.lfFaceName, L"Arial");
        font_ = CreateFontIndirectW(&lf);
    }
}

void PlaylistWindow::blit(const Image& img, const Rect& src, int dx, int dy) {
    if (!bits_ || !img.valid()) return;
    const int S = scale_;
    for (int sy = 0; sy < src.h; ++sy) {
        const int srcY = src.y + sy;
        if (srcY < 0 || srcY >= img.h) continue;
        for (int ky = 0; ky < S; ++ky) {
            const int outY = (dy + sy) * S + ky;
            if (outY < 0 || outY >= surfH_) continue;
            uint32_t* row = bits_ + size_t(outY) * size_t(surfW_);
            for (int sx = 0; sx < src.w; ++sx) {
                const int srcX = src.x + sx;
                if (srcX < 0 || srcX >= img.w) continue;
                const uint32_t px = img.px[size_t(srcY) * size_t(img.w) + size_t(srcX)];
                const int outX0 = (dx + sx) * S;
                for (int kx = 0; kx < S; ++kx) {
                    const int outX = outX0 + kx;
                    if (outX >= 0 && outX < surfW_) row[outX] = px;
                }
            }
        }
    }
}

// Repeats `tile` horizontally over [x0, x1), clipping the last one.
void PlaylistWindow::tileH(const Image& img, const Rect& tile, int x0, int x1, int y) {
    for (int x = x0; x < x1; x += tile.w) {
        Rect r = tile;
        r.w = std::min(tile.w, x1 - x);
        blit(img, r, x, y);
    }
}

// Surface-pixel rectangle fill.
void PlaylistWindow::fillRect(int x, int y, int w, int h, uint32_t px) {
    if (!bits_) return;
    const int x0 = std::max(0, x), x1 = std::min(surfW_, x + w);
    const int y0 = std::max(0, y), y1 = std::min(surfH_, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    for (int yy = y0; yy < y1; ++yy)
        std::fill(bits_ + size_t(yy) * size_t(surfW_) + size_t(x0), bits_ + size_t(yy) * size_t(surfW_) + size_t(x1), px);
}

void PlaylistWindow::drawFrame() {
    const Image* img = skin_ ? skin_->image(L"pledit") : nullptr;
    if (!img) return;
    const int W = widthSkin_, H = heightSkin_;
    const bool a = active_;
    const Rect tl = a ? kTopLeftActive : kTopLeftInactive;
    const Rect ti = a ? kTitleActive : kTitleInactive;
    const Rect tt = a ? kTopTileActive : kTopTileInactive;
    const Rect tr = a ? kTopRightActive : kTopRightInactive;

    blit(*img, tl, 0, 0);
    blit(*img, tr, W - tr.w, 0);
    const int titleX = (W - ti.w) / 2;
    blit(*img, ti, titleX, 0);
    tileH(*img, tt, tl.w, titleX, 0);
    tileH(*img, tt, titleX + ti.w, W - tr.w, 0);

    for (int y = kTopH; y < H - kBottomH; y += kLeftTile.h) {
        Rect l = kLeftTile, r = kRightTile;
        l.h = r.h = std::min(kLeftTile.h, H - kBottomH - y);
        blit(*img, l, 0, y);
        blit(*img, r, W - kRightW, y);
    }

    blit(*img, kBottomLeft, 0, H - kBottomH);
    blit(*img, kBottomRight, W - kBottomRight.w, H - kBottomH);
    tileH(*img, kBottomTile, kBottomLeft.w, W - kBottomRight.w, H - kBottomH);
}

void PlaylistWindow::drawList() {
    static const PlaylistStyle kDefault;
    const PlaylistStyle& st = skin_ ? skin_->playlistStyle() : kDefault;
    const int S = scale_;
    const int lx = kLeftW * S, ly = kTopH * S;
    const int lw = (widthSkin_ - kLeftW - kRightW) * S;
    const int lh = (heightSkin_ - kTopH - kBottomH) * S;
    const int rowH = kRowH * S;
    const int rows = visibleRows();
    const int count = static_cast<int>(playlist_.size());

    // drop selection entries for rows that no longer exist
    while (!selected_.empty() && *selected_.rbegin() >= count) selected_.erase(std::prev(selected_.end()));
    clampScroll();

    fillRect(lx, ly, lw, lh, pixelOf(st.normalBg));
    for (int r = 0; r < rows; ++r) {
        const int idx = scroll_ + r;
        if (idx >= count) break;
        if (selected_.count(idx)) fillRect(lx, ly + r * rowH, lw, rowH, pixelOf(st.selectedBg));
    }

    if (font_) {
        HGDIOBJ oldFont = SelectObject(memDC_, font_);
        SetBkMode(memDC_, TRANSPARENT);
        for (int r = 0; r < rows; ++r) {
            const int idx = scroll_ + r;
            if (idx >= count) break;
            const PlaylistEntry& e = playlist_.at(static_cast<size_t>(idx));
            const std::wstring label = std::to_wstring(idx + 1) + L". " + e.title;
            RECT rc{lx + 2 * S, ly + r * rowH, lx + lw - 2 * S, ly + (r + 1) * rowH};
            SetTextColor(memDC_, idx == playlist_.current() ? st.current : st.normal);
            if (e.durationSec >= 0) {
                const std::wstring d = fmtTime(e.durationSec);
                SIZE sz{};
                GetTextExtentPoint32W(memDC_, d.c_str(), static_cast<int>(d.size()), &sz);
                RECT drc = rc;
                drc.left = rc.right - sz.cx;
                DrawTextW(memDC_, d.c_str(), -1, &drc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_RIGHT);
                rc.right = drc.left - 4 * S;
            }
            DrawTextW(memDC_, label.c_str(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        SelectObject(memDC_, oldFont);
    }
    GdiFlush();

    // GDI leaves alpha undefined; the list area is opaque.
    for (int y = std::max(0, ly); y < std::min(surfH_, ly + lh); ++y) {
        uint32_t* row = bits_ + size_t(y) * size_t(surfW_);
        for (int x = std::max(0, lx); x < std::min(surfW_, lx + lw); ++x) row[x] |= 0xFF000000u;
    }
}

void PlaylistWindow::render() {
    if (!hwnd_ || !bits_ || !IsWindowVisible(hwnd_)) return;
    GdiFlush();
    fillRect(0, 0, surfW_, surfH_, pixelOf(RGB(0, 0, 0)));  // opaque base (frame covers it)
    drawFrame();
    drawList();

    SIZE size{surfW_, surfH_};
    POINT src{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, memDC_, &src, 0, &bf, ULW_ALPHA);
}

// ---------------------------------------------------------------- messages

LRESULT CALLBACK PlaylistWindow::ThunkProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    PlaylistWindow* self;
    if (m == WM_NCCREATE) {
        self = static_cast<PlaylistWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PlaylistWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(h, m, w, l);
    const LRESULT r = self->proc(h, m, w, l);
    if (m == WM_NCDESTROY) {
        self->hwnd_ = nullptr;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    }
    return r;
}

LRESULT PlaylistWindow::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &p);
            const int sx = floorDiv(p.x, scale_), sy = floorDiv(p.y, scale_);
            if (sy >= 0 && sy < kTopH && hitButton(sx, sy) == Btn::None) return HTCAPTION;  // drag by title bar
            return HTCLIENT;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(h);
            const int sx = floorDiv(GET_X_LPARAM(l), scale_), sy = floorDiv(GET_Y_LPARAM(l), scale_);
            const Btn b = hitButton(sx, sy);
            if (b != Btn::None) {
                pressed_ = b;
                if (b == Btn::Grip) {
                    gripStartX_ = sx;
                    gripStartY_ = sy;
                    gripStartW_ = widthSkin_;
                    gripStartH_ = heightSkin_;
                }
                SetCapture(h);
                return 0;
            }
            const bool ctrl = (w & MK_CONTROL) != 0, shift = (w & MK_SHIFT) != 0;
            const int row = rowAt(sx, sy);
            if (row >= 0) {
                if (!ctrl && !shift && selected_.count(row)) dragCollapse_ = true;  // keep a multi-selection for dragging
                else selectRow(row, ctrl, shift);
                if (!ctrl && !shift) {
                    dragRow_ = row;
                    dragMoved_ = false;
                    SetCapture(h);
                }
            } else if (inList(sx, sy) && !ctrl) {
                selected_.clear();
                invalidate();
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            const int sx = floorDiv(GET_X_LPARAM(l), scale_), sy = floorDiv(GET_Y_LPARAM(l), scale_);
            const int row = rowAt(sx, sy);
            if (row >= 0) {
                selected_.clear();
                selected_.insert(row);
                anchor_ = row;
                invalidate();
                if (onPlayIndex_) onPlayIndex_(row);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (pressed_ == Btn::Grip)
                resizeByDrag(floorDiv(GET_X_LPARAM(l), scale_), floorDiv(GET_Y_LPARAM(l), scale_));
            else if (dragRow_ >= 0 && (w & MK_LBUTTON)) {
                const int sy = floorDiv(GET_Y_LPARAM(l), scale_);
                const int count = static_cast<int>(playlist_.size());
                int target = scroll_ + floorDiv(sy - kTopH, kRowH);
                target = std::clamp(target, 0, count - 1);
                if (sy < kTopH) { --scroll_; clampScroll(); }  // dragging above the list scrolls up
                else if (sy >= heightSkin_ - kBottomH) { ++scroll_; clampScroll(); }  // below: scroll down
                while (dragRow_ != target) {
                    const int step = target > dragRow_ ? 1 : -1;
                    if (!playlist_.moveSelected(selected_, step)) break;
                    dragRow_ += step;
                    dragMoved_ = true;
                }
                if (dragMoved_) { anchor_ = dragRow_; ensureVisible(dragRow_); invalidate(); }
            }
            return 0;
        case WM_RBUTTONUP: {
            const int sx = floorDiv(GET_X_LPARAM(l), scale_), sy = floorDiv(GET_Y_LPARAM(l), scale_);
            showRowMenu(rowAt(sx, sy));
            return 0;
        }
        case WM_SETCURSOR: {
            if (LOWORD(l) == HTCLIENT) {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(h, &p);
                if (pressed_ == Btn::Grip || hitButton(floorDiv(p.x, scale_), floorDiv(p.y, scale_)) == Btn::Grip) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (dragRow_ >= 0) {
                const bool moved = dragMoved_;
                if (!moved && dragCollapse_) {
                    const int r = dragRow_;
                    selected_.clear();
                    selected_.insert(r);
                    anchor_ = r;
                    invalidate();
                }
                dragRow_ = -1;
                dragMoved_ = false;
                dragCollapse_ = false;
                ReleaseCapture();
                if (moved) notifyChanged();
                return 0;
            }
            if (pressed_ != Btn::None) {
                const Btn b = pressed_;
                pressed_ = Btn::None;
                ReleaseCapture();
                const int sx = floorDiv(GET_X_LPARAM(l), scale_), sy = floorDiv(GET_Y_LPARAM(l), scale_);
                if (hitButton(sx, sy) == b) activateButton(b);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressed_ = Btn::None;
            dragRow_ = -1;
            dragMoved_ = false;
            dragCollapse_ = false;
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(w);
            scroll_ -= (delta / WHEEL_DELTA) * 3;
            clampScroll();
            invalidate();
            return 0;
        }
        case WM_KEYDOWN: {
            const int count = static_cast<int>(playlist_.size());
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            switch (w) {
                case VK_DELETE:
                    removeSelected();
                    return 0;
                case VK_RETURN:
                    if (!selected_.empty() && onPlayIndex_) onPlayIndex_(*selected_.begin());
                    return 0;
                case VK_UP:
                case VK_DOWN:
                case VK_HOME:
                case VK_END:
                case VK_PRIOR:
                case VK_NEXT: {
                    if (count == 0) return 0;
                    int idx = anchor_ < 0 ? 0 : anchor_;
                    const int page = visibleRows();
                    if (w == VK_UP) idx -= 1;
                    else if (w == VK_DOWN) idx += 1;
                    else if (w == VK_HOME) idx = 0;
                    else if (w == VK_END) idx = count - 1;
                    else if (w == VK_PRIOR) idx -= page;
                    else idx += page;
                    idx = std::clamp(idx, 0, count - 1);
                    selected_.clear();
                    selected_.insert(idx);
                    anchor_ = idx;
                    ensureVisible(idx);
                    invalidate();
                    return 0;
                }
                case 'A':
                    if (ctrl) {
                        selected_.clear();
                        for (int i = 0; i < count; ++i) selected_.insert(i);
                        invalidate();
                        return 0;
                    }
                    break;
                default:
                    break;
            }
            break;
        }
        case WM_ACTIVATE:
            active_ = LOWORD(w) != WA_INACTIVE;
            invalidate();
            return 0;
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(l);
            scale_ = scaleForDpi(HIWORD(w));
            rebuildFont();
            resizeWindowAndSurface();
            SetWindowPos(h, nullptr, r->left, r->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            invalidate();
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(w);
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            std::vector<std::wstring> files;
            for (UINT i = 0; i < n; ++i) {
                const UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring s(len, L'\0');
                DragQueryFileW(drop, i, s.data(), len + 1);
                collectPath(s, files);
            }
            DragFinish(drop);
            playlist_.addMany(files);
            invalidate();
            notifyChanged();
            return 0;
        }
        case WM_TIMER:
            if (w == kDirtyTimer) {
                KillTimer(h, kDirtyTimer);
                dirty_ = false;
                render();
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(h, m, w, l);
}
