#include "ui/EqWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr wchar_t kClassName[] = L"WnampStyleEqWindow";
constexpr UINT_PTR kRenderTimer = 1;
constexpr UINT kRenderMs = 16;

constexpr int kSliderCount = 11;  // preamp + 10 bands
constexpr int kPartNone = -1;
constexpr int kPartClose = 100;
constexpr int kPartOn = 101;
constexpr int kPartAuto = 102;
constexpr int kPartPresets = 103;
constexpr UINT kPresetMenuBase = 1000;

constexpr float kRangeDb = 12.0f;
constexpr float kSnapDb = 1.0f;
constexpr int kThumbTravel = sprites_eq::kSliderH - sprites_eq::kThumbH;

int sliderX(int i) {
    return i == 0 ? sprites_eq::kPreampX : sprites_eq::kBandX0 + (i - 1) * sprites_eq::kBandPitch;
}

bool inRect(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct Preset {
    const wchar_t* name;
    float bands[10];  // dB at 60, 170, 310, 600, 1k, 3k, 6k, 12k, 14k, 16k (classic Winamp bands)
};

const Preset kPresets[] = {
    {L"Flat",                {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {L"Classical",           {0, 0, 0, 0, 0, 0, -4, -4, -4, -6}},
    {L"Club",                {0, 0, 4, 3, 3, 3, 2, 0, 0, 0}},
    {L"Dance",               {6, 4, 2, 0, 0, -3, -4, -4, 0, 0}},
    {L"Full Bass",           {6, 6, 6, 3, 1, -3, -5, -6, -6, -6}},
    {L"Full Bass & Treble",  {5, 4, 0, -4, -3, 1, 5, 6, 7, 7}},
    {L"Full Treble",         {-6, -6, -6, -3, 1, 6, 9, 9, 9, 10}},
    {L"Laptop Speakers",     {3, 7, 4, -2, -1, 1, 3, 6, 8, 9}},
    {L"Large Hall",          {6, 6, 3, 3, 0, -3, -3, -3, 0, 0}},
    {L"Live",                {-3, 0, 2, 3, 3, 3, 2, 1, 1, 1}},
    {L"Party",               {4, 4, 0, 0, 0, 0, 0, 0, 4, 4}},
    {L"Pop",                 {-1, 2, 4, 5, 3, 0, -1, -1, -1, -1}},
    {L"Reggae",              {0, 0, -1, -4, 0, -3, -3, 0, 0, 0}},
    {L"Rock",                {5, 3, -3, -5, -2, 2, 5, 6, 6, 6}},
    {L"Ska",                 {-1, -3, -2, 0, 2, 3, 5, 5, 6, 5}},
    {L"Soft",                {3, 1, 0, -1, 0, 2, 4, 5, 6, 7}},
    {L"Soft Rock",           {2, 2, 1, 0, -2, -3, -2, 0, 1, 5}},
    {L"Techno",              {5, 4, 0, -4, -4, 0, 5, 6, 6, 5}},
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

float clampDb(float v) {
    if (!(v == v)) return 0.0f;  // NaN
    return std::clamp(v, -kRangeDb, kRangeDb);
}

}  // namespace

EqWindow::EqWindow(IEngine& engine) : engine_(engine) {}

EqWindow::~EqWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (memDC_) {
        if (oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteDC(memDC_);
    }
    if (dib_) DeleteObject(dib_);
}

bool EqWindow::create(HINSTANCE hinst, HWND owner) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &EqWindow::ThunkProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    memDC_ = CreateCompatibleDC(nullptr);
    if (!memDC_) return false;

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kClassName, L"Equalizer", WS_POPUP, 0, 0,
                            sprites_eq::kWidth * scale_, sprites_eq::kHeight * scale_, owner, nullptr,
                            hinst, this);
    if (!hwnd_) return false;

    resizeSurface();

    // Push the initial state so the engine matches the UI.
    applyAllToEngine();

    render();
    return true;
}

void EqWindow::setSkin(std::shared_ptr<Skin> skin) {
    skin_ = std::move(skin);
    markDirty();
}

void EqWindow::setScale(int scale) {
    scale = std::max(1, scale);
    if (scale == scale_) return;
    scale_ = scale;
    if (hwnd_) {
        resizeSurface();
        markDirty();
    }
}

void EqWindow::show(bool visible) {
    if (!hwnd_) return;
    ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (visible) render();
}

bool EqWindow::visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

void EqWindow::moveTo(int x, int y) {
    if (hwnd_) SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void EqWindow::getRect(RECT& out) const {
    if (hwnd_) GetWindowRect(hwnd_, &out);
    else out = RECT{0, 0, 0, 0};
}

void EqWindow::applyAllToEngine() {
    engine_.setEqEnabled(eqOn_);
    for (int b = 0; b < 10; ++b) engine_.setEqBand(b, gains_[b + 1]);
    engine_.setEqPreamp(gains_[0]);
}

void EqWindow::setPreset(const float bands[10], float preamp) {
    for (int b = 0; b < 10; ++b) gains_[b + 1] = clampDb(bands[b]);
    gains_[0] = clampDb(preamp);
    applyAllToEngine();
    markDirty();
}

void EqWindow::getState(float bands[10], float& preamp, bool& enabled) const {
    for (int b = 0; b < 10; ++b) bands[b] = gains_[b + 1];
    preamp = gains_[0];
    enabled = eqOn_;
}

void EqWindow::setState(const float bands[10], float preamp, bool enabled) {
    for (int b = 0; b < 10; ++b) gains_[b + 1] = clampDb(bands[b]);
    gains_[0] = clampDb(preamp);
    eqOn_ = enabled;
    applyAllToEngine();
    markDirty();
}

void EqWindow::showPresetMenu() {
    if (!hwnd_) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (int i = 0; i < kPresetCount; ++i)
        AppendMenuW(menu, MF_STRING, kPresetMenuBase + static_cast<UINT>(i), kPresets[i].name);

    // Pop up just below the PRESETS button.
    POINT pt{sprites_eq::kPresetsDst.x * scale_,
             (sprites_eq::kPresetsDst.y + sprites_eq::kPresetsDst.h) * scale_};
    ClientToScreen(hwnd_, &pt);
    SetForegroundWindow(hwnd_);  // needed so the menu closes when clicking elsewhere
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0,
                       hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd >= kPresetMenuBase && cmd < kPresetMenuBase + static_cast<UINT>(kPresetCount))
        setPreset(kPresets[cmd - kPresetMenuBase].bands, 0.0f);
}

void EqWindow::resizeSurface() {
    surfW_ = sprites_eq::kWidth * scale_;
    surfH_ = sprites_eq::kHeight * scale_;
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

// Redraws are coalesced: mark dirty, render once on the next timer tick (~16 ms).
void EqWindow::markDirty() {
    dirty_ = true;
    if (hwnd_ && !timerOn_) {
        SetTimer(hwnd_, kRenderTimer, kRenderMs, nullptr);
        timerOn_ = true;
    }
}

void EqWindow::blit(const Image& img, const Rect& src, int dx, int dy) {
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

float EqWindow::normOf(int index) const {
    return std::clamp((kRangeDb - gains_[index]) / (2.0f * kRangeDb), 0.0f, 1.0f);
}

void EqWindow::drawToggle(const Image& img, const Rect& dst, const Rect& offUp, const Rect& offDown,
                          const Rect& onUp, const Rect& onDown, bool state, int part) {
    const bool down = pressedPart_ == part && pressedInside_;
    const Rect& r = state ? (down ? onDown : onUp) : (down ? offDown : offUp);
    blit(img, r, dst.x, dst.y);
}

void EqWindow::fillRect(int x, int y, int w, int h, uint32_t px) {
    if (!bits_) return;
    const int S = scale_;
    const int x0 = std::max(0, x * S), x1 = std::min(surfW_, (x + w) * S);
    const int y0 = std::max(0, y * S), y1 = std::min(surfH_, (y + h) * S);
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = bits_ + size_t(yy) * size_t(surfW_);
        for (int xx = x0; xx < x1; ++xx) row[xx] = px;
    }
}

// Live response curve: Catmull-Rom through the 10 band gains (+ preamp offset), one column per skin
// pixel, vertical spans joining neighbouring columns so the line stays connected.
void EqWindow::drawCurve(const Image& eq) {
    using namespace sprites_eq;
    const Rect& area = kCurveArea;
    blit(eq, kGraphBg, area.x, area.y);

    const int cy = area.h / 2;
    blit(eq, kPreampLine, area.x, area.y + cy);  // zero line

    constexpr int N = 10;
    float pts[N];
    for (int i = 0; i < N; ++i)
        pts[i] = std::clamp((gains_[i + 1] + gains_[0]) / kRangeDb * float(cy), -float(cy), float(cy));

    auto colourAt = [&](int row) -> uint32_t {
        const int sy = kLineColorY + row;
        if (kLineColorX < eq.w && sy >= 0 && sy < eq.h) {
            const uint32_t px = eq.px[size_t(sy) * size_t(eq.w) + size_t(kLineColorX)];
            if ((px & 0x00FFFFFFu) != 0) return px;
        }
        return 0xFF5CFF5Cu;  // fallback: light green
    };

    int prevY = -1;
    for (int x = 0; x < area.w; ++x) {
        const float t = float(x) * float(N - 1) / float(area.w - 1);
        const int k = std::clamp(static_cast<int>(t), 0, N - 2);
        const float u = t - float(k);
        const float p0 = pts[std::max(k - 1, 0)], p1 = pts[k], p2 = pts[k + 1], p3 = pts[std::min(k + 2, N - 1)];
        const float v = 0.5f * ((2.0f * p1) + (-p0 + p2) * u + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u * u +
                                (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u * u * u);
        const int y = std::clamp(cy - static_cast<int>(std::lround(v)), 0, area.h - 1);
        const int lo = prevY < 0 ? y : std::min(prevY, y);
        const int hi = prevY < 0 ? y : std::max(prevY, y);
        for (int yy = lo; yy <= hi; ++yy) fillRect(area.x + x, area.y + yy, 1, 1, colourAt(yy));
        prevY = y;
    }
}

void EqWindow::render() {
    if (!hwnd_ || !bits_) return;
    dirty_ = false;
    std::fill_n(bits_, size_t(surfW_) * size_t(surfH_), 0u);  // fully transparent

    const Image* eq = skin_ ? skin_->image(L"eqmain") : nullptr;
    if (!eq && skin_) eq = skin_->image(L"eq");  // our earlier default-skin name
    if (eq) {
        using namespace sprites_eq;
        blit(*eq, kBackground, 0, 0);
        blit(*eq, active_ ? kTitleActive : kTitleInactive, 0, 0);

        const bool closeDown = pressedPart_ == kPartClose && pressedInside_;
        blit(*eq, closeDown ? kCloseDown : kCloseUp, kCloseDst.x, kCloseDst.y);

        drawToggle(*eq, kOnDst, kOnOffUp, kOnOffDown, kOnOnUp, kOnOnDown, eqOn_, kPartOn);
        drawToggle(*eq, kAutoDst, kAutoOffUp, kAutoOffDown, kAutoOnUp, kAutoOnDown, auto_, kPartAuto);

        drawCurve(*eq);
        const bool presetsDown = pressedPart_ == kPartPresets && pressedInside_;
        blit(*eq, presetsDown ? kPresetsDown : kPresetsUp, kPresetsDst.x, kPresetsDst.y);

        for (int i = 0; i < kSliderCount; ++i) {
            const float n = normOf(i);
            const int frame = std::clamp(static_cast<int>(std::lround(n * float(kFrameCount - 1))), 0,
                                         kFrameCount - 1);
            const int x = sliderX(i);
            blit(*eq, sliderFrame(frame), x, kSliderY);
            const int thumbY = kSliderY + static_cast<int>(std::lround(n * float(kThumbTravel)));
            blit(*eq, dragSlider_ == i ? kThumbDown : kThumbUp, x + kThumbDx, thumbY);
        }
    }

    SIZE size{surfW_, surfH_};
    POINT src{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, memDC_, &src, 0, &bf, ULW_ALPHA);
}

int EqWindow::partAt(int x, int y) const {
    using namespace sprites_eq;
    if (inRect(kCloseDst, x, y)) return kPartClose;
    if (inRect(kOnDst, x, y)) return kPartOn;
    if (inRect(kAutoDst, x, y)) return kPartAuto;
    if (inRect(kPresetsDst, x, y)) return kPartPresets;
    for (int i = 0; i < kSliderCount; ++i) {
        const Rect r{sliderX(i), kSliderY, kSliderW, kSliderH};
        if (inRect(r, x, y)) return i;
    }
    return kPartNone;
}

void EqWindow::updateSlider(int index, int skinY) {
    using namespace sprites_eq;
    const float pos = std::clamp(float(skinY - grabOffset_ - kSliderY) / float(kThumbTravel), 0.0f, 1.0f);
    float db = kRangeDb - 2.0f * kRangeDb * pos;
    if (std::fabs(db) < kSnapDb) db = 0.0f;  // snap to centre
    if (db == gains_[index]) return;
    gains_[index] = db;
    if (index == 0) engine_.setEqPreamp(db);
    else engine_.setEqBand(index - 1, db);
    markDirty();
}

LRESULT CALLBACK EqWindow::ThunkProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    EqWindow* self;
    if (m == WM_NCCREATE) {
        self = static_cast<EqWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = h;
    } else {
        self = reinterpret_cast<EqWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->proc(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT EqWindow::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &p);
            const int sx = floorDiv(p.x, scale_), sy = floorDiv(p.y, scale_);
            if (sy >= 0 && sy < sprites_eq::kTitleBarHeight && !inRect(sprites_eq::kCloseDst, sx, sy))
                return HTCAPTION;  // drag by the title bar
            return HTCLIENT;
        }
        case WM_LBUTTONDOWN: {
            const int sx = floorDiv(GET_X_LPARAM(l), scale_), sy = floorDiv(GET_Y_LPARAM(l), scale_);
            const int part = partAt(sx, sy);
            if (part >= 0 && part < kSliderCount) {
                const int thumbTop = sprites_eq::kSliderY +
                                     static_cast<int>(std::lround(normOf(part) * float(kThumbTravel)));
                grabOffset_ = (sy >= thumbTop && sy < thumbTop + sprites_eq::kThumbH)
                                  ? sy - thumbTop
                                  : sprites_eq::kThumbH / 2;
                dragSlider_ = part;
                SetCapture(h);
                updateSlider(part, sy);
                markDirty();
            } else if (part != kPartNone) {
                pressedPart_ = part;
                pressedInside_ = true;
                SetCapture(h);
                markDirty();
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int sx = floorDiv(GET_X_LPARAM(l), scale_), sy = floorDiv(GET_Y_LPARAM(l), scale_);
            if (dragSlider_ >= 0) {
                updateSlider(dragSlider_, sy);
            } else if (pressedPart_ != kPartNone) {
                const bool inside = partAt(sx, sy) == pressedPart_;
                if (inside != pressedInside_) {
                    pressedInside_ = inside;
                    markDirty();
                }
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const int part = pressedPart_;
            const bool inside = pressedInside_;
            const bool wasDragging = dragSlider_ >= 0;
            // Clear state first: ReleaseCapture sends WM_CAPTURECHANGED.
            pressedPart_ = kPartNone;
            pressedInside_ = false;
            dragSlider_ = -1;
            if (part != kPartNone || wasDragging) {
                ReleaseCapture();
                markDirty();
            }
            if (part != kPartNone && inside) {
                if (part == kPartClose) {
                    show(false);
                    if (onClosed_) onClosed_();
                } else if (part == kPartOn) {
                    eqOn_ = !eqOn_;
                    engine_.setEqEnabled(eqOn_);
                } else if (part == kPartAuto) {
                    auto_ = !auto_;  // state only; auto-loading presets is not implemented
                } else if (part == kPartPresets) {
                    showPresetMenu();
                }
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(l) != h && (pressedPart_ != kPartNone || dragSlider_ >= 0)) {
                pressedPart_ = kPartNone;
                pressedInside_ = false;
                dragSlider_ = -1;
                markDirty();
            }
            return 0;
        case WM_ACTIVATE:
            active_ = LOWORD(w) != WA_INACTIVE;
            markDirty();
            return 0;
        case WM_TIMER:
            if (w == kRenderTimer) {
                KillTimer(h, kRenderTimer);
                timerOn_ = false;
                if (dirty_) render();
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
        case WM_NCDESTROY:
            hwnd_ = nullptr;
            timerOn_ = false;
            break;
    }
    return DefWindowProcW(h, m, w, l);
}
