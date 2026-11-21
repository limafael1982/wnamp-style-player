#include "ui/SkinBrowser.h"

#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <memory>

#include "skin/Skin.h"

namespace {

constexpr wchar_t kClassName[] = L"WnampSkinBrowser";
constexpr int kIdList = 101;
constexpr int kIdApply = 102;
constexpr int kIdClose = 103;
constexpr int kSkinW = 275;

struct State {
    HWND hwnd = nullptr;
    HWND list = nullptr, apply = nullptr, close = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    RECT previewRc{};

    std::vector<std::wstring> dirs;
    std::vector<SkinEntry> entries;
    std::function<void(const std::wstring&)> onChoose;

    // Preview image: the skin's main.bmp with its title bar strip drawn on top.
    std::vector<uint32_t> px;
    int pw = 0, ph = 0;
    std::wstring previewErr;
};

std::unique_ptr<State> g_state;

int S(const State& st, int v) { return MulDiv(v, static_cast<int>(st.dpi), 96); }

std::wstring lowerStr(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

void loadPreview(State& st, int index) {
    st.px.clear();
    st.pw = st.ph = 0;
    st.previewErr.clear();
    if (index < 0 || size_t(index) >= st.entries.size()) return;

    Skin skin;
    std::wstring err;
    if (!skin.loadFromPath(st.entries[size_t(index)].path, err)) {
        st.previewErr = err.empty() ? L"Cannot load this skin." : err;
        return;
    }
    const Image* main = skin.image(L"main");
    if (!main || !main->valid()) {
        st.previewErr = L"Skin has no main.bmp.";
        return;
    }
    st.pw = main->w;
    st.ph = main->h;
    st.px = main->px;

    // main.bmp has no title bar; it comes from titlebar.bmp (active strip at x=27, y=0).
    const Image* tb = skin.image(L"titlebar");
    if (tb && tb->valid()) {
        for (int y = 0; y < 14 && y < st.ph && y < tb->h; ++y)
            for (int x = 0; x < kSkinW && x < st.pw && x + 27 < tb->w; ++x)
                st.px[size_t(y) * size_t(st.pw) + size_t(x)] = tb->px[size_t(y) * size_t(tb->w) + size_t(x + 27)];
    }
}

void applyFont(State& st) {
    if (st.font) DeleteObject(st.font);
    st.font = CreateFontW(-MulDiv(9, static_cast<int>(st.dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (HWND h : {st.list, st.apply, st.close})
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
}

void layout(State& st) {
    RECT rc;
    GetClientRect(st.hwnd, &rc);
    const int m = S(st, 8), btnW = S(st, 84), btnH = S(st, 26), listW = S(st, 210);
    const int btnY = rc.bottom - m - btnH;
    MoveWindow(st.list, m, m, listW, std::max(1, int(btnY - 2 * m)), TRUE);
    MoveWindow(st.close, rc.right - m - btnW, btnY, btnW, btnH, TRUE);
    MoveWindow(st.apply, rc.right - 2 * (m + btnW), btnY, btnW, btnH, TRUE);
    st.previewRc = RECT{2 * m + listW, m, std::max<LONG>(2 * m + listW + 1, rc.right - m), btnY - m};
    InvalidateRect(st.hwnd, nullptr, TRUE);
}

void fillList(State& st, const std::wstring& currentPath) {
    SendMessageW(st.list, LB_RESETCONTENT, 0, 0);
    st.entries = listSkins(st.dirs);
    int sel = -1;
    const std::wstring cur = lowerStr(currentPath);
    for (size_t i = 0; i < st.entries.size(); ++i) {
        SendMessageW(st.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(st.entries[i].name.c_str()));
        if (!cur.empty() && lowerStr(st.entries[i].path) == cur) sel = static_cast<int>(i);
    }
    if (sel < 0 && !st.entries.empty()) sel = 0;
    if (sel >= 0) SendMessageW(st.list, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
    loadPreview(st, sel);
    EnableWindow(st.apply, !st.entries.empty());
    InvalidateRect(st.hwnd, nullptr, TRUE);
}

void applySelected(State& st) {
    const int sel = static_cast<int>(SendMessageW(st.list, LB_GETCURSEL, 0, 0));
    if (sel < 0 || size_t(sel) >= st.entries.size() || !st.onChoose) return;
    const std::wstring path = st.entries[size_t(sel)].path;
    auto cb = st.onChoose;  // the callback may re-enter show()
    cb(path);
}

void paintPreview(State& st, HDC dc) {
    const RECT& r = st.previewRc;
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(dc, &r, bg);
    DeleteObject(bg);

    const int areaW = r.right - r.left, areaH = r.bottom - r.top;
    if (st.px.empty() || st.pw <= 0 || st.ph <= 0) {
        std::wstring msg = st.previewErr;
        if (msg.empty()) {
            msg = L"No skins found.\nPut .wsz files or unpacked skin folders in:\n";
            for (const auto& d : st.dirs) msg += L"\n" + d;
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220, 220, 220));
        HGDIOBJ old = SelectObject(dc, st.font);
        RECT t = r;
        InflateRect(&t, -S(st, 8), -S(st, 8));
        DrawTextW(dc, msg.c_str(), -1, &t, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        return;
    }

    // Largest integer scale that fits, never below 1 (crops if the panel is tiny).
    const int scale = std::max(1, std::min(areaW / st.pw, areaH / st.ph));
    const int w = st.pw * scale, h = st.ph * scale;
    const int x = r.left + (areaW - w) / 2, y = r.top + (areaH - h) / 2;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = st.pw;
    bi.bmiHeader.biHeight = -st.ph;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    const int oldMode = SetStretchBltMode(dc, COLORONCOLOR);  // nearest neighbour
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    StretchDIBits(dc, x, y, w, h, 0, 0, st.pw, st.ph, st.px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(dc, oldMode);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    State* st = g_state.get();
    switch (m) {
        case WM_CREATE: {
            if (!st) return -1;
            st->hwnd = h;
            st->dpi = GetDpiForWindow(h);
            const HINSTANCE hi = reinterpret_cast<const CREATESTRUCTW*>(l)->hInstance;
            st->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                       LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, h,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdList)), hi, nullptr);
            st->apply = CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                        0, 0, 10, 10, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApply)), hi, nullptr);
            st->close = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                        0, 0, 10, 10, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClose)), hi, nullptr);
            if (!st->list || !st->apply || !st->close) return -1;
            applyFont(*st);
            return 0;
        }
        case WM_SIZE:
            if (st && st->list && static_cast<int>(w) != SIZE_MINIMIZED) layout(*st);
            return 0;
        case WM_GETMINMAXINFO: {
            if (!st) break;
            auto* mm = reinterpret_cast<MINMAXINFO*>(l);
            mm->ptMinTrackSize.x = S(*st, 520);
            mm->ptMinTrackSize.y = S(*st, 300);
            return 0;
        }
        case WM_DPICHANGED: {
            if (!st) break;
            st->dpi = HIWORD(w);
            const RECT* r = reinterpret_cast<const RECT*>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            applyFont(*st);
            layout(*st);
            return 0;
        }
        case WM_COMMAND: {
            if (!st) break;
            const int id = LOWORD(w), code = HIWORD(w);
            if (id == kIdList && code == LBN_SELCHANGE) {
                loadPreview(*st, static_cast<int>(SendMessageW(st->list, LB_GETCURSEL, 0, 0)));
                InvalidateRect(h, &st->previewRc, TRUE);
            } else if (id == kIdList && code == LBN_DBLCLK) {
                applySelected(*st);
            } else if (id == kIdApply && code == BN_CLICKED) {
                applySelected(*st);
            } else if (id == kIdClose && code == BN_CLICKED) {
                DestroyWindow(h);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) { DestroyWindow(h); return 0; }
            break;
        case WM_ERASEBKGND: {
            // Let the default fill the dialog colour; the preview panel is painted in WM_PAINT.
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            if (st) paintPreview(*st, dc);
            EndPaint(h, &ps);
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

void SkinBrowser::show(HWND owner, HINSTANCE hinst, const std::vector<std::wstring>& dirs,
                       const std::wstring& currentSkinPath, std::function<void(const std::wstring&)> onChoose) {
    if (g_state && g_state->hwnd && IsWindow(g_state->hwnd)) {
        // Already open: refresh and bring to front.
        g_state->dirs = dirs;
        g_state->onChoose = std::move(onChoose);
        fillList(*g_state, currentSkinPath);
        if (IsIconic(g_state->hwnd)) ShowWindow(g_state->hwnd, SW_RESTORE);
        SetForegroundWindow(g_state->hwnd);
        return;
    }
    if (!ensureClass(hinst)) return;

    g_state = std::make_unique<State>();
    g_state->dirs = dirs;
    g_state->onChoose = std::move(onChoose);

    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX;
    RECT rc{0, 0, MulDiv(680, static_cast<int>(dpi), 96), MulDiv(360, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT orc;
    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + MulDiv(40, static_cast<int>(dpi), 96);
        y = orc.top + MulDiv(40, static_cast<int>(dpi), 96);
    }

    HWND hwnd = CreateWindowExW(0, kClassName, L"Skin Browser", style, x, y, rc.right - rc.left, rc.bottom - rc.top,
                                owner, nullptr, hinst, nullptr);
    if (!hwnd) {
        g_state.reset();
        return;
    }
    fillList(*g_state, currentSkinPath);
    layout(*g_state);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}
