#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "playlist/Playlist.h"
#include "skin/Skin.h"
#include "skin/Sprites.h"  // Rect

// Playlist editor window: layered popup drawn from pledit.bmp (frame) plus GDI text
// for the rows. Owns no playlist data; it edits the Playlist it is given.
class PlaylistWindow {
public:
    explicit PlaylistWindow(Playlist& playlist);
    ~PlaylistWindow();
    PlaylistWindow(const PlaylistWindow&) = delete;
    PlaylistWindow& operator=(const PlaylistWindow&) = delete;

    bool create(HINSTANCE hinst, HWND owner);
    void setSkin(std::shared_ptr<Skin> skin);
    void setScale(int scale);
    void show(bool visible);
    bool visible() const;
    void moveTo(int x, int y);
    void getRect(RECT& rc) const;
    HWND hwnd() const { return hwnd_; }
    void refresh();  // playlist changed externally (current track, durations, ...)

    void setOnClosed(std::function<void()> cb) { onClosed_ = std::move(cb); }
    void setOnPlayIndex(std::function<void(int)> cb) { onPlayIndex_ = std::move(cb); }
    // Fired after any edit that changes the list (add/remove/sort/clear/load). Not fired for
    // selection, scrolling or resizing.
    void setOnChanged(std::function<void()> cb) { onChanged_ = std::move(cb); }
    // Fired when LOAD LIST replaced the whole playlist (before onChanged).
    void setOnPlaylistReplaced(std::function<void()> cb) { onReplaced_ = std::move(cb); }
    void setSupportedExtensions(std::vector<std::wstring> exts) { exts_ = std::move(exts); }

private:
    enum class Btn { None, Close, Add, Rem, Sel, Misc, Load, Grip };

    static LRESULT CALLBACK ThunkProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT proc(HWND, UINT, WPARAM, LPARAM);

    // layout (skin pixels)
    int visibleRows() const;
    bool inList(int sx, int sy) const;
    int rowAt(int sx, int sy) const;  // playlist index under the point, or -1
    Btn hitButton(int sx, int sy) const;
    void activateButton(Btn b);

    // selection / list
    void selectRow(int row, bool ctrl, bool shift);
    void clampScroll();
    void ensureVisible(int index);
    void removeSelected();
    void addFilesDialog();
    void showMiscMenu();
    void showRowMenu(int row);
    void loadListDialog();
    void saveListDialog();
    void notifyChanged();
    void resetListView();
    void resizeByDrag(int skinX, int skinY);
    void collectPath(const std::wstring& path, std::vector<std::wstring>& out) const;
    bool hasSupportedExt(const std::wstring& path) const;

    // drawing
    void invalidate();
    void resizeWindowAndSurface();
    void rebuildFont();
    void render();
    void blit(const Image& img, const Rect& src, int dx, int dy);
    void tileH(const Image& img, const Rect& tile, int x0, int x1, int y);
    void fillRect(int x, int y, int w, int h, uint32_t px);
    void drawFrame();
    void drawList();

    Playlist& playlist_;
    HWND hwnd_ = nullptr;
    std::shared_ptr<Skin> skin_;
    int scale_ = 1;
    int widthSkin_ = 275, heightSkin_ = 232;

    HDC memDC_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ oldBmp_ = nullptr;
    uint32_t* bits_ = nullptr;
    int surfW_ = 0, surfH_ = 0;
    HFONT font_ = nullptr;

    std::set<int> selected_;
    int anchor_ = -1;
    int scroll_ = 0;
    bool active_ = true;
    bool dirty_ = false;
    Btn pressed_ = Btn::None;

    // Row drag-to-reorder state.
    int dragRow_ = -1;          // row currently under the dragged selection, -1 = none
    bool dragMoved_ = false;
    bool dragCollapse_ = false;  // plain click on an already-selected row: collapse selection on release if not dragged

    // Grip drag state (skin pixels).
    int gripStartX_ = 0, gripStartY_ = 0, gripStartW_ = 0, gripStartH_ = 0;

    std::function<void()> onChanged_;
    std::function<void()> onReplaced_;
    std::function<void()> onClosed_;
    std::function<void(int)> onPlayIndex_;
    std::vector<std::wstring> exts_;  // lowercase, with leading dot
};
