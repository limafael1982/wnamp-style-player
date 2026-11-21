#include "ui/SettingsWindow.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <memory>

#include "skin/Skin.h"

namespace {

constexpr wchar_t kClassName[] = L"WnampSettingsWindow";

constexpr int kIdSidebar = 1;
constexpr int kIdSkinList = 10;
constexpr int kIdSkinApplyBtn = 11;
constexpr int kIdSkinBrowseBtn = 12;
constexpr int kIdTrayCheck = 20;
constexpr int kIdBackgroundCheck = 21;
constexpr int kIdStationList = 30;
constexpr int kIdStationName = 31;
constexpr int kIdStationUrl = 32;
constexpr int kIdStationAdd = 33;
constexpr int kIdStationUpdate = 34;
constexpr int kIdStationRemove = 35;

// main.bmp is 275px wide; titlebar.bmp's active strip starts at x=27 (see loadSkinPreview).
constexpr int kSkinMainW = 275;

enum class Page { Skin = 0, Behavior = 1, Radios = 2 };

struct State {
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    SettingsWindow::Callbacks callbacks;
    Page page = Page::Skin;

    HWND sidebar = nullptr;

    // Skin page: a list of skins found via callbacks.getSkinDirs(), with a live preview of
    // the selected one (the skin's main.bmp plus its title bar strip) painted in WM_PAINT.
    HWND skinLabel = nullptr;
    HWND skinName = nullptr;
    HWND skinList = nullptr;
    HWND skinApplyBtn = nullptr;
    HWND skinBrowseBtn = nullptr;
    RECT skinPreviewRc{};
    std::vector<std::wstring> skinDirs;
    std::vector<SkinEntry> skinEntries;
    std::vector<uint32_t> previewPx;
    int previewW = 0, previewH = 0;
    std::wstring previewErr;

    // Behavior page.
    HWND trayCheck = nullptr;
    HWND backgroundCheck = nullptr;

    // Radios page.
    HWND stationList = nullptr;
    HWND stationNameLabel = nullptr;
    HWND stationNameEdit = nullptr;
    HWND stationUrlLabel = nullptr;
    HWND stationUrlEdit = nullptr;
    HWND stationAddBtn = nullptr;
    HWND stationUpdateBtn = nullptr;
    HWND stationRemoveBtn = nullptr;
    std::vector<radio::Station> stations;
};

std::unique_ptr<State> g_state;

int scaled(const State& st, int value) { return MulDiv(value, static_cast<int>(st.dpi), 96); }

std::wstring lowerStr(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

void applyFont(State& st) {
    if (st.font) DeleteObject(st.font);
    st.font = CreateFontW(-MulDiv(9, static_cast<int>(st.dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (HWND h : {st.sidebar, st.skinLabel, st.skinName, st.skinList, st.skinApplyBtn, st.skinBrowseBtn, st.trayCheck,
                   st.backgroundCheck, st.stationList, st.stationNameLabel, st.stationNameEdit, st.stationUrlLabel,
                   st.stationUrlEdit, st.stationAddBtn, st.stationUpdateBtn, st.stationRemoveBtn})
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
}

// Decodes the selected skin's main.bmp (with the titlebar's active strip composited on top,
// the same way the real main window draws it) into st.previewPx for paintSkinPreview().
void loadSkinPreview(State& st, int index) {
    st.previewPx.clear();
    st.previewW = st.previewH = 0;
    st.previewErr.clear();
    if (index < 0 || static_cast<size_t>(index) >= st.skinEntries.size()) return;

    Skin skin;
    std::wstring err;
    if (!skin.loadFromPath(st.skinEntries[static_cast<size_t>(index)].path, err)) {
        st.previewErr = err.empty() ? L"Cannot load this skin." : err;
        return;
    }
    const Image* main = skin.image(L"main");
    if (!main || !main->valid()) {
        st.previewErr = L"Skin has no main.bmp.";
        return;
    }
    st.previewW = main->w;
    st.previewH = main->h;
    st.previewPx = main->px;

    const Image* tb = skin.image(L"titlebar");
    if (tb && tb->valid()) {
        for (int y = 0; y < 14 && y < st.previewH && y < tb->h; ++y)
            for (int x = 0; x < kSkinMainW && x < st.previewW && x + 27 < tb->w; ++x)
                st.previewPx[size_t(y) * size_t(st.previewW) + size_t(x)] =
                    tb->px[size_t(y) * size_t(tb->w) + size_t(x + 27)];
    }
}

void updateSkinNameLabel(State& st) {
    if (!st.callbacks.currentSkinPath) return;
    const std::wstring path = st.callbacks.currentSkinPath();
    const size_t slash = path.find_last_of(L"\\/");
    SetWindowTextW(st.skinName, slash == std::wstring::npos ? path.c_str() : path.c_str() + slash + 1);
}

void refreshSkinList(State& st) {
    SendMessageW(st.skinList, LB_RESETCONTENT, 0, 0);
    st.skinEntries = listSkins(st.skinDirs);
    int sel = -1;
    const std::wstring cur = st.callbacks.currentSkinPath ? lowerStr(st.callbacks.currentSkinPath()) : L"";
    for (size_t i = 0; i < st.skinEntries.size(); ++i) {
        SendMessageW(st.skinList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(st.skinEntries[i].name.c_str()));
        if (!cur.empty() && lowerStr(st.skinEntries[i].path) == cur) sel = static_cast<int>(i);
    }
    if (sel < 0 && !st.skinEntries.empty()) sel = 0;
    if (sel >= 0) SendMessageW(st.skinList, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
    loadSkinPreview(st, sel);
}

void applySelectedSkin(State& st) {
    const int sel = static_cast<int>(SendMessageW(st.skinList, LB_GETCURSEL, 0, 0));
    if (sel < 0 || static_cast<size_t>(sel) >= st.skinEntries.size() || !st.callbacks.applySkin) return;
    st.callbacks.applySkin(st.skinEntries[static_cast<size_t>(sel)].path);
    updateSkinNameLabel(st);  // reflect the switch without disturbing the list's selection/scroll
}

// Lets the user pick a .wsz/.zip from anywhere on disk, copies it into the skins folder via
// callbacks.importSkin (so it persists and shows up here on future opens), and applies it.
void browseForSkin(State& st) {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = st.hwnd;
    ofn.lpstrFilter = L"Winamp skins\0*.wsz;*.zip\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn) || !st.callbacks.importSkin) return;

    std::wstring err;
    if (!st.callbacks.importSkin(buf, err))
        MessageBoxW(st.hwnd, err.empty() ? L"Could not add this skin." : err.c_str(), L"Could not add skin",
                   MB_OK | MB_ICONWARNING);
    refreshSkinList(st);  // picks up the newly-copied file and selects it (it matches currentSkinPath now)
    InvalidateRect(st.hwnd, &st.skinPreviewRc, TRUE);
}

void refreshStationList(State& st) {
    SendMessageW(st.stationList, LB_RESETCONTENT, 0, 0);
    for (const auto& station : st.stations)
        SendMessageW(st.stationList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(station.title.c_str()));
}

void loadSelectedStation(State& st) {
    const int sel = static_cast<int>(SendMessageW(st.stationList, LB_GETCURSEL, 0, 0));
    if (sel < 0 || static_cast<size_t>(sel) >= st.stations.size()) return;
    SetWindowTextW(st.stationNameEdit, st.stations[static_cast<size_t>(sel)].title.c_str());
    SetWindowTextW(st.stationUrlEdit, st.stations[static_cast<size_t>(sel)].url.c_str());
}

void commitStations(State& st) {
    if (st.callbacks.setStations) st.callbacks.setStations(st.stations);
}

void loadFromCallbacks(State& st) {
    updateSkinNameLabel(st);
    if (st.callbacks.getSkinDirs) st.skinDirs = st.callbacks.getSkinDirs();
    if (st.skinList) refreshSkinList(st);
    if (st.callbacks.getShowTrayIcon)
        SendMessageW(st.trayCheck, BM_SETCHECK, st.callbacks.getShowTrayIcon() ? BST_CHECKED : BST_UNCHECKED, 0);
    if (st.callbacks.getKeepRunningInBackground)
        SendMessageW(st.backgroundCheck, BM_SETCHECK,
                     st.callbacks.getKeepRunningInBackground() ? BST_CHECKED : BST_UNCHECKED, 0);
    if (st.callbacks.getStations) {
        st.stations = st.callbacks.getStations();
        refreshStationList(st);
    }
    SetWindowTextW(st.stationNameEdit, L"");
    SetWindowTextW(st.stationUrlEdit, L"");
}

void showPage(State& st, Page page) {
    st.page = page;
    const int show[3] = {page == Page::Skin, page == Page::Behavior, page == Page::Radios};
    for (HWND h : {st.skinLabel, st.skinName, st.skinList, st.skinApplyBtn, st.skinBrowseBtn})
        ShowWindow(h, show[0] ? SW_SHOW : SW_HIDE);
    for (HWND h : {st.trayCheck, st.backgroundCheck}) ShowWindow(h, show[1] ? SW_SHOW : SW_HIDE);
    for (HWND h : {st.stationList, st.stationNameLabel, st.stationNameEdit, st.stationUrlLabel, st.stationUrlEdit,
                   st.stationAddBtn, st.stationUpdateBtn, st.stationRemoveBtn})
        ShowWindow(h, show[2] ? SW_SHOW : SW_HIDE);
    InvalidateRect(st.hwnd, nullptr, TRUE);  // the preview panel is custom-painted, not a child window
}

HWND makeStatic(HWND parent, HINSTANCE hi, const wchar_t* text, int x, int y, int w, int h, DWORD extraStyle = 0) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | extraStyle, x, y, w, h, parent, nullptr, hi,
                           nullptr);
}

HWND makeEdit(HWND parent, HINSTANCE hi, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hi, nullptr);
}

HWND makeButton(HWND parent, HINSTANCE hi, int id, const wchar_t* text, int x, int y, int w, int h,
               DWORD extraStyle = BS_PUSHBUTTON) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | extraStyle, x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hi, nullptr);
}

// Small line-art glyphs for the sidebar, drawn with GDI rather than borrowed from any
// icon set, so there is no asset or license to track: a picture frame for Skin, a gear
// for Behavior, a little radio for Radios.
void drawCategoryIcon(HDC dc, int cx, int cy, int size, Page page, COLORREF color) {
    const int half = size / 2;
    HPEN pen = CreatePen(PS_SOLID, std::max(1, size / 12), color);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    switch (page) {
        case Page::Skin: {
            RoundRect(dc, cx - half, cy - half, cx + half, cy + half, size / 6, size / 6);
            Ellipse(dc, cx - half + size / 6, cy - half + size / 6, cx - half + size / 3, cy - half + size / 3);
            const POINT mountain[3] = {{cx - half + size / 8, cy + half - size / 8},
                                       {cx - size / 10, cy - size / 8},
                                       {cx + half - size / 8, cy + half - size / 8}};
            HBRUSH fill = CreateSolidBrush(color);
            HBRUSH oldFill = static_cast<HBRUSH>(SelectObject(dc, fill));
            Polygon(dc, mountain, 3);
            SelectObject(dc, oldFill);
            DeleteObject(fill);
            break;
        }
        case Page::Behavior: {
            const int r = half - size / 8;
            Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
            const int toothLen = size / 6;
            for (int i = 0; i < 8; ++i) {
                const double angle = i * (3.14159265358979323846 * 2.0 / 8.0);
                const int x0 = cx + static_cast<int>(r * std::cos(angle));
                const int y0 = cy + static_cast<int>(r * std::sin(angle));
                const int x1 = cx + static_cast<int>((r + toothLen) * std::cos(angle));
                const int y1 = cy + static_cast<int>((r + toothLen) * std::sin(angle));
                MoveToEx(dc, x0, y0, nullptr);
                LineTo(dc, x1, y1);
            }
            const int holeR = r / 2;
            Ellipse(dc, cx - holeR, cy - holeR, cx + holeR, cy + holeR);
            break;
        }
        case Page::Radios: {
            RoundRect(dc, cx - half, cy - half / 2, cx + half, cy + half, size / 8, size / 8);
            MoveToEx(dc, cx + half / 2, cy - half / 2, nullptr);
            LineTo(dc, cx + half, cy - half - size / 6);
            const int knobR = std::max(1, size / 7);
            Ellipse(dc, cx - half / 2 - knobR, cy - knobR, cx - half / 2 + knobR, cy + knobR);
            Ellipse(dc, cx + half / 2 - 2 * knobR, cy - knobR, cx + half / 2, cy + knobR);
            break;
        }
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// Draws the selected skin's preview (or an error/empty-state message) into st.skinPreviewRc.
// Mirrors SkinBrowser's own preview painter: nearest-neighbour integer upscale, cropped to
// the panel if it's smaller than the skin's 275px width.
void paintSkinPreview(State& st, HDC dc) {
    const RECT& r = st.skinPreviewRc;
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(dc, &r, bg);
    DeleteObject(bg);

    const int areaW = r.right - r.left, areaH = r.bottom - r.top;
    if (st.previewPx.empty() || st.previewW <= 0 || st.previewH <= 0) {
        std::wstring msg = st.previewErr;
        if (msg.empty()) {
            msg = L"No skins found.\nPut .wsz files or unpacked skin folders in:\n";
            for (const auto& dir : st.skinDirs) msg += L"\n" + dir;
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220, 220, 220));
        HGDIOBJ old = SelectObject(dc, st.font);
        RECT t = r;
        InflateRect(&t, -scaled(st, 8), -scaled(st, 8));
        DrawTextW(dc, msg.c_str(), -1, &t, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        return;
    }

    const int scale = std::max(1, std::min(areaW / st.previewW, areaH / st.previewH));
    const int w = st.previewW * scale, h = st.previewH * scale;
    const int x = r.left + (areaW - w) / 2, y = r.top + (areaH - h) / 2;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = st.previewW;
    bi.bmiHeader.biHeight = -st.previewH;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    const int oldMode = SetStretchBltMode(dc, COLORONCOLOR);  // nearest neighbour
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    StretchDIBits(dc, x, y, w, h, 0, 0, st.previewW, st.previewH, st.previewPx.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(dc, oldMode);
}

void buildControls(State& st, HWND h, HINSTANCE hi) {
    const int pageX = scaled(st, 150), pageW = scaled(st, 450);

    st.sidebar = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                                     LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                                 scaled(st, 10), scaled(st, 10), scaled(st, 128), scaled(st, 410), h,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSidebar)), hi, nullptr);
    for (const wchar_t* name : {L"Skin", L"Behavior", L"Radios"})
        SendMessageW(st.sidebar, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(st.sidebar, LB_SETCURSEL, 0, 0);

    // Skin page: "Current skin" label, then a list of skins on the left of the page with a
    // live preview of the selected one on the right.
    st.skinLabel = makeStatic(h, hi, L"Current skin:", pageX, scaled(st, 14), pageW, scaled(st, 18));
    st.skinName = makeStatic(h, hi, L"", pageX, scaled(st, 34), pageW, scaled(st, 18));
    const int skinListW = scaled(st, 150), skinListY = scaled(st, 60), skinListH = scaled(st, 330);
    st.skinList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                  WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, pageX,
                                  skinListY, skinListW, skinListH, h,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSkinList)), hi, nullptr);
    st.skinPreviewRc = RECT{pageX + skinListW + scaled(st, 10), skinListY, pageX + pageW, skinListY + skinListH};
    st.skinBrowseBtn = makeButton(h, hi, kIdSkinBrowseBtn, L"Browse...", pageX, skinListY + skinListH + scaled(st, 10),
                                 skinListW, scaled(st, 26));
    st.skinApplyBtn = makeButton(h, hi, kIdSkinApplyBtn, L"Use this skin", st.skinPreviewRc.left,
                                 skinListY + skinListH + scaled(st, 10), scaled(st, 150), scaled(st, 26));

    // Behavior page.
    st.trayCheck = makeButton(h, hi, kIdTrayCheck, L"Show system tray icon", pageX, scaled(st, 14), pageW,
                             scaled(st, 20), BS_AUTOCHECKBOX);
    st.backgroundCheck =
        makeButton(h, hi, kIdBackgroundCheck, L"Keep running in the background when the window is closed", pageX,
                  scaled(st, 42), pageW, scaled(st, 40), BS_AUTOCHECKBOX | BS_MULTILINE);

    // Radios page.
    st.stationList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                     WS_CHILD | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                                     pageX, scaled(st, 14), pageW, scaled(st, 140), h,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStationList)), hi, nullptr);
    st.stationNameLabel = makeStatic(h, hi, L"Name:", pageX, scaled(st, 162), pageW, scaled(st, 16));
    st.stationNameEdit = makeEdit(h, hi, kIdStationName, pageX, scaled(st, 180), pageW, scaled(st, 22));
    st.stationUrlLabel = makeStatic(h, hi, L"URL:", pageX, scaled(st, 208), pageW, scaled(st, 16));
    st.stationUrlEdit = makeEdit(h, hi, kIdStationUrl, pageX, scaled(st, 226), pageW, scaled(st, 22));
    const int btnW = scaled(st, 104), btnY = scaled(st, 256);
    st.stationAddBtn = makeButton(h, hi, kIdStationAdd, L"Add", pageX, btnY, btnW, scaled(st, 26));
    st.stationUpdateBtn = makeButton(h, hi, kIdStationUpdate, L"Update", pageX + btnW + scaled(st, 8), btnY, btnW,
                                     scaled(st, 26));
    st.stationRemoveBtn = makeButton(h, hi, kIdStationRemove, L"Remove", pageX + 2 * (btnW + scaled(st, 8)), btnY,
                                     btnW, scaled(st, 26));

    applyFont(st);
    showPage(st, Page::Skin);  // sets the correct WS_VISIBLE state for every control above
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    State* st = g_state.get();
    switch (m) {
        case WM_CREATE: {
            if (!st) return -1;
            st->hwnd = h;
            st->dpi = GetDpiForWindow(h);
            const HINSTANCE hi = reinterpret_cast<const CREATESTRUCTW*>(l)->hInstance;
            buildControls(*st, h, hi);
            loadFromCallbacks(*st);
            return 0;
        }
        case WM_COMMAND: {
            if (!st) break;
            const int id = LOWORD(w), code = HIWORD(w);
            if (id == kIdSidebar && code == LBN_SELCHANGE) {
                const int sel = static_cast<int>(SendMessageW(st->sidebar, LB_GETCURSEL, 0, 0));
                if (sel >= 0) showPage(*st, static_cast<Page>(sel));
            } else if (id == kIdSkinList && code == LBN_SELCHANGE) {
                loadSkinPreview(*st, static_cast<int>(SendMessageW(st->skinList, LB_GETCURSEL, 0, 0)));
                InvalidateRect(h, &st->skinPreviewRc, TRUE);
            } else if (id == kIdSkinList && code == LBN_DBLCLK) {
                applySelectedSkin(*st);
            } else if (id == kIdSkinApplyBtn && code == BN_CLICKED) {
                applySelectedSkin(*st);
            } else if (id == kIdSkinBrowseBtn && code == BN_CLICKED) {
                browseForSkin(*st);
            } else if (id == kIdTrayCheck && code == BN_CLICKED) {
                const bool checked = SendMessageW(st->trayCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (st->callbacks.setShowTrayIcon) st->callbacks.setShowTrayIcon(checked);
            } else if (id == kIdBackgroundCheck && code == BN_CLICKED) {
                const bool checked = SendMessageW(st->backgroundCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (st->callbacks.setKeepRunningInBackground) st->callbacks.setKeepRunningInBackground(checked);
            } else if (id == kIdStationList && code == LBN_SELCHANGE) {
                loadSelectedStation(*st);
            } else if (id == kIdStationAdd && code == BN_CLICKED) {
                wchar_t nameBuf[256] = {}, urlBuf[2048] = {};
                GetWindowTextW(st->stationNameEdit, nameBuf, 256);
                GetWindowTextW(st->stationUrlEdit, urlBuf, 2048);
                if (nameBuf[0] && urlBuf[0]) {
                    st->stations.push_back(radio::Station{nameBuf, urlBuf});
                    commitStations(*st);
                    refreshStationList(*st);
                    SendMessageW(st->stationList, LB_SETCURSEL, static_cast<WPARAM>(st->stations.size() - 1), 0);
                }
            } else if (id == kIdStationUpdate && code == BN_CLICKED) {
                const int sel = static_cast<int>(SendMessageW(st->stationList, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && static_cast<size_t>(sel) < st->stations.size()) {
                    wchar_t nameBuf[256] = {}, urlBuf[2048] = {};
                    GetWindowTextW(st->stationNameEdit, nameBuf, 256);
                    GetWindowTextW(st->stationUrlEdit, urlBuf, 2048);
                    if (nameBuf[0] && urlBuf[0]) {
                        st->stations[static_cast<size_t>(sel)] = radio::Station{nameBuf, urlBuf};
                        commitStations(*st);
                        refreshStationList(*st);
                        SendMessageW(st->stationList, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
                    }
                }
            } else if (id == kIdStationRemove && code == BN_CLICKED) {
                const int sel = static_cast<int>(SendMessageW(st->stationList, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && static_cast<size_t>(sel) < st->stations.size()) {
                    st->stations.erase(st->stations.begin() + sel);
                    commitStations(*st);
                    refreshStationList(*st);
                    SetWindowTextW(st->stationNameEdit, L"");
                    SetWindowTextW(st->stationUrlEdit, L"");
                }
            }
            return 0;
        }
        case WM_MEASUREITEM: {
            if (!st) break;
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
            if (mis->CtlID == static_cast<UINT>(kIdSidebar)) mis->itemHeight = static_cast<UINT>(scaled(*st, 30));
            return TRUE;
        }
        case WM_DRAWITEM: {
            if (!st) break;
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (dis->CtlID != static_cast<UINT>(kIdSidebar)) break;
            const bool selected = (dis->itemState & ODS_SELECTED) != 0;
            const COLORREF bg = selected ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW);
            const COLORREF fg = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);
            HBRUSH bgBrush = CreateSolidBrush(bg);
            FillRect(dis->hDC, &dis->rcItem, bgBrush);
            DeleteObject(bgBrush);
            if (dis->itemID != static_cast<UINT>(-1)) {
                const int iconSize = scaled(*st, 18);
                const int pad = scaled(*st, 8);
                const int cy = (dis->rcItem.top + dis->rcItem.bottom) / 2;
                const int cx = dis->rcItem.left + pad + iconSize / 2;
                drawCategoryIcon(dis->hDC, cx, cy, iconSize, static_cast<Page>(dis->itemID), fg);

                wchar_t text[64] = {};
                SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));
                RECT textRc = dis->rcItem;
                textRc.left += pad * 2 + iconSize;
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, fg);
                HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, st->font));
                DrawTextW(dis->hDC, text, -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                SelectObject(dis->hDC, oldFont);
            }
            if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &dis->rcItem);
            return TRUE;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            if (st && st->page == Page::Skin) paintSkinPreview(*st, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_DPICHANGED: {
            if (!st) break;
            st->dpi = HIWORD(w);
            const RECT* r = reinterpret_cast<const RECT*>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
            applyFont(*st);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_NCDESTROY: {
            if (st && st->hwnd == h) {
                if (st->font) DeleteObject(st->font);
                g_state.reset();  // st is dangling after this line
            }
            break;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

bool ensureClass(HINSTANCE hinst) {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    registered = true;
    return true;
}

}  // namespace

void SettingsWindow::show(HWND owner, HINSTANCE hinst, Callbacks callbacks) {
    if (g_state && g_state->hwnd && IsWindow(g_state->hwnd)) {
        g_state->callbacks = std::move(callbacks);
        loadFromCallbacks(*g_state);
        if (IsIconic(g_state->hwnd)) ShowWindow(g_state->hwnd, SW_RESTORE);
        SetForegroundWindow(g_state->hwnd);
        return;
    }
    if (!ensureClass(hinst)) return;

    g_state = std::make_unique<State>();
    g_state->callbacks = std::move(callbacks);

    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, MulDiv(620, static_cast<int>(dpi), 96), MulDiv(460, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT ownerRc;
    if (owner && GetWindowRect(owner, &ownerRc)) {
        x = ownerRc.left + MulDiv(40, static_cast<int>(dpi), 96);
        y = ownerRc.top + MulDiv(40, static_cast<int>(dpi), 96);
    }

    HWND hwnd = CreateWindowExW(0, kClassName, L"Settings", style, x, y, rc.right - rc.left, rc.bottom - rc.top,
                                owner, nullptr, hinst, nullptr);
    if (!hwnd) {
        g_state.reset();
        return;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}
