#include "ui/MainWindow.h"

#include "playlist/PlaylistIo.h"
#include "ui/SkinBrowser.h"
#include "net/SomaFm.h"

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <thread>
#include <filesystem>
#include <cmath>
#include <cwctype>
#include <vector>

namespace {
constexpr wchar_t kClassName[] = L"WnampStyleMainWindow";
constexpr UINT kMsgNet = WM_APP + 2;  // lParam: heap std::function<void()>* run on the UI thread
constexpr UINT_PTR kTimerId = 1;

constexpr UINT_PTR kRenderTimerId = 2;   // render coalescing cooldown
constexpr UINT_PTR kMarqueeTimerId = 3;  // title scrolling
constexpr UINT_PTR kVisTimerId = 4;      // spectrum analyzer animation

// Adds `p` (a file, or every file below a folder whose extension is in `exts`) to `out`.
void collectMedia(const std::wstring& p, const std::vector<std::wstring>& exts, std::vector<std::wstring>& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        std::vector<std::wstring> found;
        for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::wstring ext = it->path().extension().wstring();
            for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
            if (std::find(exts.begin(), exts.end(), ext) != exts.end()) found.push_back(it->path().wstring());
        }
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    } else {
        out.push_back(p);
    }
}

int scaleForDpi(UINT dpi) { return std::max(1, int((dpi + 48) / 96)); }
int iround(float v) { return static_cast<int>(v + 0.5f); }
float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
bool inRect(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
}  // namespace

// One table drives drawing and hit-testing for every interactive control.
const MainWindow::ControlDef* MainWindow::controlTable(size_t& count) {
    using namespace sprites;
    constexpr Rect none{0, 0, 0, 0};
    static const ControlDef table[] = {
        // Title bar (both modes). In shade mode the shade button is "unshade".
        {Ctl::Menu,     Kind::Button, kBoth,   L"titlebar", kMenuDst,     kMenuUp,     kMenuDown,     none, none},
        {Ctl::Minimize, Kind::Button, kBoth,   L"titlebar", kMinimizeDst, kMinimizeUp, kMinimizeDown, none, none},
        {Ctl::Shade,    Kind::Button, kNormal, L"titlebar", kShadeDst,    kShadeUp,    kShadeDown,    none, none},
        {Ctl::Shade,    Kind::Button, kShade,  L"titlebar", kShadeDst,    kUnshadeUp,  kUnshadeDown,  none, none},
        {Ctl::Close,    Kind::Button, kBoth,   L"titlebar", kCloseDst,    kCloseUp,    kCloseDown,    none, none},
        // Transport.
        {Ctl::Prev,  Kind::Button, kNormal, L"cbuttons", kPrevDst,  kPrevUp,  kPrevDown,  none, none},
        {Ctl::Play,  Kind::Button, kNormal, L"cbuttons", kPlayDst,  kPlayUp,  kPlayDown,  none, none},
        {Ctl::Pause, Kind::Button, kNormal, L"cbuttons", kPauseDst, kPauseUp, kPauseDown, none, none},
        {Ctl::Stop,  Kind::Button, kNormal, L"cbuttons", kStopDst,  kStopUp,  kStopDown,  none, none},
        {Ctl::Next,  Kind::Button, kNormal, L"cbuttons", kNextDst,  kNextUp,  kNextDown,  none, none},
        {Ctl::Eject, Kind::Button, kNormal, L"cbuttons", kEjectDst, kEjectUp, kEjectDown, none, none},
        // Toggles (shufrep.bmp).
        {Ctl::Shuffle,  Kind::Toggle, kNormal, L"shufrep", kShuffleDst, kShuffleUp, kShuffleDown, kShuffleOnUp, kShuffleOnDown},
        {Ctl::Repeat,   Kind::Toggle, kNormal, L"shufrep", kRepeatDst,  kRepeatUp,  kRepeatDown,  kRepeatOnUp,  kRepeatOnDown},
        {Ctl::Eq,       Kind::Toggle, kNormal, L"shufrep", kEqDst,      kEqUp,      kEqDown,      kEqOnUp,      kEqOnDown},
        {Ctl::Playlist, Kind::Toggle, kNormal, L"shufrep", kPlDst,      kPlUp,      kPlDown,      kPlOnUp,      kPlOnDown},
        // Sliders (drawn by dedicated functions; sprite columns unused here).
        {Ctl::Position, Kind::Slider, kNormal, L"posbar",  kPosBarDst,  none, none, none, none},
        {Ctl::Volume,   Kind::Slider, kNormal, L"volume",  kVolumeDst,  none, none, none, none},
        {Ctl::Balance,  Kind::Slider, kNormal, L"balance", kBalanceDst, none, none, none, none},
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

const MainWindow::ControlDef* MainWindow::hitControl(int x, int y) const {
    size_t n = 0;
    const ControlDef* t = controlTable(n);
    for (size_t i = 0; i < n; ++i)
        if ((t[i].modes & currentMode()) && inRect(t[i].dst, x, y)) return &t[i];
    return nullptr;
}

const MainWindow::ControlDef* MainWindow::findDef(Ctl id) const {
    size_t n = 0;
    const ControlDef* t = controlTable(n);
    for (size_t i = 0; i < n; ++i)
        if (t[i].id == id && (t[i].modes & currentMode())) return &t[i];
    return nullptr;
}

bool MainWindow::toggleState(Ctl id) const {
    switch (id) {
        case Ctl::Shuffle:  return shuffle_;
        case Ctl::Repeat:   return repeat_;
        case Ctl::Eq:       return eqOn_;
        case Ctl::Playlist: return plOn_;
        default:            return false;
    }
}

MainWindow::MainWindow(IEngine& engine) : engine_(engine) {}

MainWindow::~MainWindow() {
    if (memDC_) {
        if (oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteDC(memDC_);
    }
    if (dib_) DeleteObject(dib_);
}

bool MainWindow::create(HINSTANCE hinst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &MainWindow::ThunkProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(101));    // resources/app.rc: IDI_APPICON
    wc.hIconSm = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, 0));
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    memDC_ = CreateCompatibleDC(nullptr);
    if (!memDC_) return false;

    setScaleFromDpi(GetDpiForSystem());
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, kClassName, L"Wnamp",
                            WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, 100, 100,
                            sprites::kMainWidth * scale_, sprites::kMainHeight * scale_,
                            nullptr, nullptr, hinst, this);
    if (!hwnd_) return false;

    // The window may sit on a monitor with a different DPI than the system's.
    setScaleFromDpi(GetDpiForWindow(hwnd_));
    resizeWindowAndSurface();
    DragAcceptFiles(hwnd_, TRUE);
    hinst_ = hinst;
    eqWin_ = std::make_unique<EqWindow>(engine_);
    if (eqWin_->create(hinst, hwnd_)) {
        eqWin_->setScale(scale_);
        eqWin_->setOnClosed([this] { eqOn_ = false; layoutDocked(); render(); });
    } else {
        eqWin_.reset();
    }
    plWin_ = std::make_unique<PlaylistWindow>(playlist_);
    if (plWin_->create(hinst, hwnd_)) {
        plWin_->setScale(scale_);
        plWin_->setSupportedExtensions(engine_.supportedExtensions());
        plWin_->setOnClosed([this] { plOn_ = false; layoutDocked(); render(); });
        plWin_->setOnPlayIndex([this](int i) { playIndex(i); });
        plWin_->setOnChanged([this] { requeueNext(); savePlaylist(); });
        plWin_->setOnPlaylistReplaced([this] { engine_.stop(); queuedIndex_ = -1; render(); });
    } else {
        plWin_.reset();
    }
    radioMenu_ = std::make_unique<RadioMenu>(
        [this](std::function<void()> work, std::function<void()> done) { runAsync(std::move(work), std::move(done)); },
        [this](const std::wstring& url, const std::wstring& title) { playUrl(url, title); });
    engine_.setNotifyWindow(hwnd_);
    engine_.setVolume(volume_);
    engine_.setBalance(balance_);
    SetTimer(hwnd_, kTimerId, 250, nullptr);  // refresh time/position while playing
    SetTimer(hwnd_, kVisTimerId, 25, nullptr);  // spectrum analyzer
    render();
    return true;
}

void MainWindow::show() {
    ShowWindow(hwnd_, SW_SHOW);
    if (startEqVisible_ && eqWin_) { eqOn_ = true; eqWin_->show(true); }
    if (startPlVisible_ && plWin_) { plOn_ = true; plWin_->show(true); }
    startEqVisible_ = startPlVisible_ = false;
    layoutDocked();
    render();
}

bool MainWindow::loadSkin(const std::wstring& path, std::wstring& err) {
    auto skin = std::make_shared<Skin>();
    if (!skin->loadFromPath(path, err)) return false;
    skin_ = std::move(skin);
    skinPath_ = path;
    if (eqWin_) eqWin_->setSkin(skin_);
    if (plWin_) plWin_->setSkin(skin_);
    applyRegion();
    render();
    return true;
}

void MainWindow::setScaleFromDpi(UINT dpi) { scale_ = scaleForDpi(dpi ? dpi : 96); }

// Resizes the window to the scaled size for the current mode (normal/shade)
// and (re)creates the DIB surface.
void MainWindow::resizeWindowAndSurface() {
    surfW_ = sprites::kMainWidth * scale_;
    surfH_ = (shade_ ? sprites::kShadeHeight : sprites::kMainHeight) * scale_;
    if (hwnd_)
        SetWindowPos(hwnd_, nullptr, 0, 0, surfW_, surfH_,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

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
    applyRegion();
    if (eqWin_) eqWin_->setScale(scale_);
    if (plWin_) plWin_->setScale(scale_);
    layoutDocked();
}

void MainWindow::applyRegion() {
    if (!hwnd_) return;
    const auto* polys = skin_ ? skin_->region(shade_ ? "windowshade" : "normal") : nullptr;
    if (!polys || polys->empty()) {
        SetWindowRgn(hwnd_, nullptr, TRUE);
        return;
    }
    std::vector<POINT> pts;
    std::vector<INT> counts;
    for (const auto& poly : *polys) {
        counts.push_back(static_cast<INT>(poly.size()));
        for (POINT p : poly) pts.push_back(POINT{p.x * scale_, p.y * scale_});
    }
    HRGN rgn = CreatePolyPolygonRgn(pts.data(), counts.data(), static_cast<int>(counts.size()), ALTERNATE);
    if (rgn) SetWindowRgn(hwnd_, rgn, TRUE);  // the system owns rgn after this
}

// Nearest-neighbour integer upscale of a skin sprite into the DIB.
void MainWindow::blit(const Image& img, const Rect& src, int dx, int dy) {
    if (!bits_ || !img.valid()) return;
    const int S = scale_;
    for (int sy = 0; sy < src.h; ++sy) {
        const int srcY = src.y + sy;
        if (srcY < 0 || srcY >= img.h) continue;
        for (int ky = 0; ky < S; ++ky) {
            const int outY = (dy + sy) * S + ky;
            if (outY < 0 || outY >= surfH_) continue;
            uint32_t* row = bits_ + size_t(outY) * surfW_;
            for (int sx = 0; sx < src.w; ++sx) {
                const int srcX = src.x + sx;
                if (srcX < 0 || srcX >= img.w) continue;
                const uint32_t px = img.px[size_t(srcY) * img.w + srcX];
                const int outX0 = (dx + sx) * S;
                for (int kx = 0; kx < S; ++kx) {
                    const int outX = outX0 + kx;
                    if (outX >= 0 && outX < surfW_) row[outX] = px;
                }
            }
        }
    }
}

void MainWindow::drawPositionBar(const ControlDef& d) {
    const Image* sheet = skin_->image(d.sheet);
    if (!sheet) return;
    const double dur = duration();
    const bool show = posDragging_ || (dur > 0 && engine_.state() != EngineState::Stopped);
    if (!show) return;
    blit(*sheet, sprites::kPosBarTrack, d.dst.x, d.dst.y);
    const float frac = posDragging_ ? posDrag_
                                    : clamp01(dur > 0 ? static_cast<float>(position() / dur) : 0.0f);
    const int x = iround(frac * float(d.dst.w - sprites::kPosThumbW));
    const bool down = pressed_ == Ctl::Position;
    blit(*sheet, down ? sprites::kPosThumbDown : sprites::kPosThumbUp, d.dst.x + x, d.dst.y);
}

void MainWindow::drawVolume(const ControlDef& d) {
    const Image* sheet = skin_->image(d.sheet);
    if (!sheet) return;
    const int frame = iround(volume_ * float(sprites::kSliderFrames - 1));
    blit(*sheet, Rect{0, frame * sprites::kSliderFramePitch, d.dst.w, d.dst.h}, d.dst.x, d.dst.y);
    const int x = iround(volume_ * float(d.dst.w - sprites::kSliderThumbW));
    const bool down = pressed_ == Ctl::Volume;
    blit(*sheet, down ? sprites::kSliderThumbDown : sprites::kSliderThumbUp,
         d.dst.x + x, d.dst.y + sprites::kSliderThumbY);
}

void MainWindow::drawBalance(const ControlDef& d) {
    const Image* sheet = skin_->image(d.sheet);
    if (!sheet) return;
    const int frame = iround(std::fabs(balance_) * float(sprites::kSliderFrames - 1));
    blit(*sheet, Rect{sprites::kBalanceSrcX, frame * sprites::kSliderFramePitch, d.dst.w, d.dst.h},
         d.dst.x, d.dst.y);
    const int x = iround((balance_ + 1.0f) * 0.5f * float(d.dst.w - sprites::kSliderThumbW));
    const bool down = pressed_ == Ctl::Balance;
    blit(*sheet, down ? sprites::kSliderThumbDown : sprites::kSliderThumbUp,
         d.dst.x + x, d.dst.y + sprites::kSliderThumbY);
}

void MainWindow::drawControls() {
    size_t n = 0;
    const ControlDef* t = controlTable(n);
    for (size_t i = 0; i < n; ++i) {
        const ControlDef& d = t[i];
        if (!(d.modes & currentMode())) continue;
        switch (d.kind) {
            case Kind::Button: {
                const Image* sheet = skin_->image(d.sheet);
                if (!sheet) break;
                const bool down = pressed_ == d.id && pressedInside_;
                blit(*sheet, down ? d.down : d.up, d.dst.x, d.dst.y);
                break;
            }
            case Kind::Toggle: {
                const Image* sheet = skin_->image(d.sheet);
                if (!sheet) break;
                const bool down = pressed_ == d.id && pressedInside_;
                const bool on = toggleState(d.id);
                const Rect& r = down ? (on ? d.downOn : d.down) : (on ? d.upOn : d.up);
                blit(*sheet, r, d.dst.x, d.dst.y);
                break;
            }
            case Kind::Slider:
                if (d.id == Ctl::Position) drawPositionBar(d);
                else if (d.id == Ctl::Volume) drawVolume(d);
                else if (d.id == Ctl::Balance) drawBalance(d);
                break;
        }
    }
}

void MainWindow::drawTime() {
    const Image* nums = skin_->image(L"numbers");
    if (!nums) nums = skin_->image(L"nums_ex");
    if (!nums) return;
    const int total = std::max(0, static_cast<int>(position()));
    const int mm = std::min(99, total / 60), ss = total % 60;
    const int d[4] = {mm / 10, mm % 10, ss / 10, ss % 10};
    for (int i = 0; i < 4; ++i)
        blit(*nums, sprites::digit(d[i]), sprites::kTimeDigitX[i], sprites::kTimeDigitY);
}

void MainWindow::drawStateIndicator() {
    const Image* img = skin_->image(L"playpaus");
    if (!img) return;
    Rect r = sprites::kStateStopped;
    switch (engine_.state()) {
        case EngineState::Playing: r = sprites::kStatePlaying; break;
        case EngineState::Paused:  r = sprites::kStatePaused; break;
        case EngineState::Stopped: break;
    }
    blit(*img, r, sprites::kStateDst.x, sprites::kStateDst.y);
}

void MainWindow::layoutDocked() {
    if (!hwnd_) return;
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    int y = rc.bottom;
    if (eqWin_ && eqWin_->visible()) {
        eqWin_->moveTo(rc.left, y);
        RECT r;
        eqWin_->getRect(r);
        y = r.bottom;
    }
    if (plWin_ && plWin_->visible()) plWin_->moveTo(rc.left, y);
}

// Hands the engine the track that follows the current one so it can switch gaplessly.
void MainWindow::requeueNext() {
    queuedIndex_ = playlist_.current() >= 0 ? playlist_.next(shuffle_, repeat_) : -1;
    if (queuedIndex_ >= 0) engine_.queueNext(playlist_.at(size_t(queuedIndex_)).path);
    else engine_.queueNext(std::wstring());
}

void MainWindow::playIndex(int index) {
    cacheTick_ = 0;
    if (index < 0 || size_t(index) >= playlist_.size()) return;
    playlist_.setCurrent(index);
    if (!engine_.open(playlist_.at(size_t(index)).path)) { render(); return; }
    refreshTrackInfo();
    if (trackInfo_.durationSec > 0) playlist_.setDuration(size_t(index), trackInfo_.durationSec);
    engine_.play();
    requeueNext();
    if (plWin_) plWin_->refresh();
    render();
}


void MainWindow::addToPlaylistAndPlay(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    const size_t first = playlist_.size();
    playlist_.addMany(paths);
    if (plWin_) plWin_->refresh();
    savePlaylist();
    playIndex(int(first));
}

void MainWindow::onEngineEvent(EngineEvent e) {
    cacheTick_ = 0;
    switch (e) {
        case EngineEvent::TagsReady:  // gapless switch to the queued track happened
            if (queuedIndex_ >= 0 && size_t(queuedIndex_) < playlist_.size()) {
                const int i = queuedIndex_;
                playlist_.setCurrent(i);
                engine_.updateInfoFor(playlist_.at(size_t(i)).path);
                            if (trackInfo_.durationSec > 0) playlist_.setDuration(size_t(i), trackInfo_.durationSec);
                requeueNext();
                if (plWin_) plWin_->refresh();
            }
            break;
        case EngineEvent::EndOfStream:  // no gapless switch happened but a next track exists
            if (queuedIndex_ >= 0) { playIndex(queuedIndex_); return; }
            break;
        default:
            refreshTrackInfo();
            break;
    }
}

void MainWindow::refreshTrackInfo() {
    trackInfo_ = engine_.trackInfo();
    if (trackInfo_.title.empty() && trackInfo_.artist.empty() && playlist_.current() >= 0)
        trackInfo_.title = playlist_.at(size_t(playlist_.current())).title;
    std::wstring t;
    if (!trackInfo_.artist.empty() && !trackInfo_.title.empty()) t = trackInfo_.artist + L" - " + trackInfo_.title;
    else t = trackInfo_.title.empty() ? trackInfo_.artist : trackInfo_.title;
    if (trackInfo_.durationSec > 0) {
        const int d = static_cast<int>(trackInfo_.durationSec + 0.5);
        wchar_t buf[24];
        swprintf_s(buf, L" (%d:%02d)", d / 60, d % 60);
        t += buf;
    }
    if (t != infoText_) { infoText_ = std::move(t); marqueeOffset_ = 0; }
    updateMarqueeTimer();
}

void MainWindow::updateMarqueeTimer() {
    const bool need = !shade_ && int(infoText_.size()) * sprites::kGlyphW > sprites::kInfoTextArea.w;
    if (need && !marqueeTimerOn_ && hwnd_) { SetTimer(hwnd_, kMarqueeTimerId, 60, nullptr); marqueeTimerOn_ = true; }
    else if (!need && marqueeTimerOn_ && hwnd_) { KillTimer(hwnd_, kMarqueeTimerId); marqueeTimerOn_ = false; }
}

// Draws text with the skin's bitmap font, clipped horizontally to [clipX0, clipX1).
void MainWindow::drawText(const std::wstring& text, int x, int y, int clipX0, int clipX1, int scrollPx) {
    const Image* img = skin_ ? skin_->image(L"text") : nullptr;
    if (!img) return;
    for (size_t i = 0; i < text.size(); ++i) {
        const int cx = x + int(i) * sprites::kGlyphW - scrollPx;
        if (cx + sprites::kGlyphW <= clipX0 || cx >= clipX1) continue;
        Rect g = sprites::glyph(text[i]);
        const int cutL = std::max(0, clipX0 - cx);
        const int cutR = std::max(0, cx + g.w - clipX1);
        g.x += cutL;
        g.w -= cutL + cutR;
        blit(*img, g, cx + cutL, y);
    }
}

void MainWindow::drawSongInfo() {
    using namespace sprites;
    const Rect& a = kInfoTextArea;
    if (!infoText_.empty()) {
        const int textW = int(infoText_.size()) * kGlyphW;
        if (textW <= a.w) {
            drawText(infoText_, a.x, a.y, a.x, a.x + a.w, 0);
        } else {
            // Scroll: text, a gap, then the text again so it wraps around.
            const std::wstring loop = infoText_ + L"  ***  ";
            const int period = int(loop.size()) * kGlyphW;
            const int off = marqueeOffset_ % period;
            drawText(loop, a.x, a.y, a.x, a.x + a.w, off);
            drawText(loop, a.x + period, a.y, a.x, a.x + a.w, off);
        }
    }
    if (trackInfo_.bitrateKbps > 0)
        drawText(std::to_wstring(std::min(trackInfo_.bitrateKbps, 999)), kBitrateDst.x, kBitrateDst.y,
                 kBitrateDst.x, kBitrateDst.x + kBitrateDst.w, 0);
    if (trackInfo_.sampleRateHz > 0)
        drawText(std::to_wstring(std::min(trackInfo_.sampleRateHz / 1000, 99)), kSampleRateDst.x,
                 kSampleRateDst.y, kSampleRateDst.x, kSampleRateDst.x + kSampleRateDst.w, 0);
    if (const Image* ms = skin_->image(L"monoster")) {
        const bool mono = trackInfo_.channels == 1, stereo = trackInfo_.channels >= 2;
        blit(*ms, mono ? kMonoOn : kMonoOff, kMonoDst.x, kMonoDst.y);
        blit(*ms, stereo ? kStereoOn : kStereoOff, kStereoDst.x, kStereoDst.y);
    }
}

void MainWindow::render() {
    if (!hwnd_) return;
    if (renderCooling_) { dirty_ = true; return; }   // will draw when the cooldown ends
    renderNow();
    renderCooling_ = true;
    SetTimer(hwnd_, kRenderTimerId, 16, nullptr);
}

void MainWindow::renderNow() {
    if (!hwnd_ || !bits_) return;
    dirty_ = false;
    std::fill_n(bits_, size_t(surfW_) * surfH_, 0u);  // fully transparent

    if (skin_) {
        if (!shade_) {
            if (const Image* main = skin_->image(L"main")) blit(*main, sprites::kMainBackground, 0, 0);
        }
        if (const Image* tb = skin_->image(L"titlebar")) {
            const Rect& strip = shade_ ? (active_ ? sprites::kTitleShadeActive : sprites::kTitleShadeInactive)
                                       : (active_ ? sprites::kTitleActive : sprites::kTitleInactive);
            blit(*tb, strip, 0, 0);
        }
        if (!shade_) {
            drawTime();
            drawStateIndicator();
            drawSongInfo();
        }
        drawControls();
        if (!shade_) drawVis();
    }

    SIZE size{surfW_, surfH_};
    POINT src{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, memDC_, &src, 0, &bf, ULW_ALPHA);
}

// Floor division so pointer positions left of / above the window (possible while
// the mouse is captured) map to negative skin coordinates.
void MainWindow::windowPixelToSkin(POINT p, int& sx, int& sy) const {
    auto fdiv = [this](int v) { return v >= 0 ? v / scale_ : -((-v + scale_ - 1) / scale_); };
    sx = fdiv(p.x);
    sy = fdiv(p.y);
}

void MainWindow::beginSliderDrag(const ControlDef& d, int sx) {
    if (d.id == Ctl::Position) {
        const double dur = duration();
        if (dur <= 0) { pressed_ = Ctl::None; return; }  // nothing seekable
        const int track = d.dst.w - sprites::kPosThumbW;
        const float cur = clamp01(static_cast<float>(position() / dur));
        const int thumbX = iround(cur * float(track));
        const int rel = sx - d.dst.x;
        // Grab the thumb where it was clicked; a click on the bare track centres it.
        posGrab_ = (rel >= thumbX && rel < thumbX + sprites::kPosThumbW) ? rel - thumbX
                                                                         : sprites::kPosThumbW / 2;
        posDragging_ = true;
    }
    updateSlider(d.id, sx);
}

void MainWindow::updateSlider(Ctl id, int sx) {
    const ControlDef* d = findDef(id);
    if (!d) return;
    const int rel = sx - d->dst.x;
    switch (id) {
        case Ctl::Position: {
            if (!posDragging_) return;
            const int track = d->dst.w - sprites::kPosThumbW;
            posDrag_ = clamp01(float(rel - posGrab_) / float(track));
            break;
        }
        case Ctl::Volume: {
            const int half = sprites::kSliderThumbW / 2;
            volume_ = clamp01(float(rel - half) / float(d->dst.w - sprites::kSliderThumbW));
            if (std::fabs(volume_ - lastSentVolume_) >= 0.01f || volume_ == 0.0f || volume_ == 1.0f) {
                engine_.setVolume(volume_);  // only when it actually changed
                lastSentVolume_ = volume_;
            }
            break;
        }
        case Ctl::Balance: {
            const int half = sprites::kSliderThumbW / 2;
            const float f = clamp01(float(rel - half) / float(d->dst.w - sprites::kSliderThumbW));
            float b = f * 2.0f - 1.0f;
            if (std::fabs(b) < 0.1f) b = 0.0f;  // snap to centre
            balance_ = b;
            if (std::fabs(balance_ - lastSentBalance_) >= 0.01f) {
                engine_.setBalance(balance_);
                lastSentBalance_ = balance_;
            }
            break;
        }
        default: break;
    }
}

void MainWindow::activate(Ctl id) {
    switch (id) {
        case Ctl::Close:    DestroyWindow(hwnd_); return;
        case Ctl::Minimize: ShowWindow(hwnd_, SW_MINIMIZE); return;
        case Ctl::Eject: {
            wchar_t buf[MAX_PATH * 2] = {};
            OPENFILENAMEW ofn{sizeof(ofn)};
            ofn.hwndOwner = hwnd_;
            std::wstring patterns;
            for (const auto& e : engine_.supportedExtensions()) {
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
            filter.push_back(L'\0');  // the final terminator is the string's own null
            ofn.lpstrFilter = filter.c_str();
            ofn.lpstrFile = buf;
            ofn.nMaxFile = MAX_PATH * 2;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
            if (GetOpenFileNameW(&ofn)) addToPlaylistAndPlay({std::wstring(buf)});
            return;
        }
        case Ctl::Menu:     showMenu(); return;
        case Ctl::Play:     doPlay(); return;
        case Ctl::Pause:    engine_.pause(); return;
        case Ctl::Stop:     engine_.stop(); return;
        case Ctl::Shade:
            shade_ = !shade_;
            updateMarqueeTimer();
            resizeWindowAndSurface();
            render();
            return;
        case Ctl::Prev: {
            const int i = playlist_.prev(shuffle_);
            if (i >= 0) playIndex(i);
            return;
        }
        case Ctl::Next: {
            const int i = playlist_.next(shuffle_, repeat_);
            if (i >= 0) playIndex(i);
            return;
        }
        case Ctl::Shuffle:  shuffle_ = !shuffle_; requeueNext(); break;
        case Ctl::Repeat:   repeat_ = !repeat_; requeueNext(); break;
        case Ctl::Eq:       toggleEq(); return;
        case Ctl::Playlist: togglePlaylist(); return;
        default: return;    // menu, prev, next, eject, sliders: later steps / handled elsewhere
    }
    render();
}

LRESULT CALLBACK MainWindow::ThunkProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    MainWindow* self;
    if (m == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->proc(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT MainWindow::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &p);
            int sx, sy;
            windowPixelToSkin(p, sx, sy);
            if (sy >= 0 && sy < sprites::kTitleBarHeight && !hitControl(sx, sy))
                return HTCAPTION;  // drag the window by the title bar
            return HTCLIENT;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(h);
            int sx, sy;
            windowPixelToSkin(POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)}, sx, sy);
            const ControlDef* d = hitControl(sx, sy);
            if (!d) return 0;
            pressed_ = d->id;
            pressedInside_ = true;
            if (d->kind == Kind::Slider) beginSliderDrag(*d, sx);
            if (pressed_ != Ctl::None) {  // a slider with nothing to drag resets it
                SetCapture(h);
                render();
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (pressed_ == Ctl::None) return 0;
            int sx, sy;
            windowPixelToSkin(POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)}, sx, sy);
            const ControlDef* d = findDef(pressed_);
            if (!d) return 0;
            if (d->kind == Kind::Slider) {
                updateSlider(pressed_, sx);
                render();
            } else {
                const ControlDef* hit = hitControl(sx, sy);
                const bool inside = hit && hit->id == pressed_;
                if (inside != pressedInside_) { pressedInside_ = inside; render(); }
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (pressed_ != Ctl::None) {
                // Reset state before ReleaseCapture: it sends WM_CAPTURECHANGED synchronously.
                const Ctl id = pressed_;
                const bool inside = pressedInside_;
                pressed_ = Ctl::None;
                pressedInside_ = false;
                ReleaseCapture();
                if (id == Ctl::Position && posDragging_) {
                    posDragging_ = false;
                    const double dur = duration();
                    if (dur > 0) engine_.seek(double(posDrag_) * dur);
                }
                render();
                if (inside) activate(id);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (pressed_ != Ctl::None) {  // capture stolen mid-drag: cancel
                pressed_ = Ctl::None;
                pressedInside_ = false;
                posDragging_ = false;
                render();
            }
            return 0;
        case WM_TIMER:
            if (w == kRenderTimerId) {
                KillTimer(h, kRenderTimerId);
                renderCooling_ = false;
                if (dirty_) render();
                return 0;
            }
            if (w == kVisTimerId) {
                if (!shade_ && updateVis()) render();
                return 0;
            }
            if (w == kMarqueeTimerId) {
                marqueeOffset_ += 1;
                render();
                return 0;
            }
            if (w == kTimerId && engine_.state() == EngineState::Playing && !posDragging_) render();
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (onKey(w, m == WM_SYSKEYDOWN)) return 0;
            break;
        case WM_MOUSEWHEEL:
            setVolumeUi(volume_ + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 0.05f : -0.05f));
            return 0;
        case WM_WINDOWPOSCHANGED:
            layoutDocked();
            return DefWindowProcW(h, m, w, l);
        case WM_ACTIVATE:
            active_ = LOWORD(w) != WA_INACTIVE;
            render();
            return 0;
        case WM_DPICHANGED: {
            setScaleFromDpi(HIWORD(w));
            const RECT* r = reinterpret_cast<const RECT*>(l);
            SetWindowPos(h, nullptr, r->left, r->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            resizeWindowAndSurface();
            render();
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(w);
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> audio;
            std::wstring skinPath;
            for (UINT i = 0; i < n; ++i) {
                wchar_t buf[MAX_PATH * 2] = {};
                DragQueryFileW(drop, i, buf, MAX_PATH * 2);
                std::wstring p(buf);
                std::wstring ext = p.size() >= 4 ? p.substr(p.size() - 4) : L"";
                for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
                if (ext == L".wsz" || ext == L".zip") skinPath = p;
                else audio.push_back(std::move(p));
            }
            DragFinish(drop);
            if (!skinPath.empty()) {
                std::wstring err;
                if (!loadSkin(skinPath, err))
                    MessageBoxW(h, err.c_str(), L"Could not load skin", MB_OK | MB_ICONWARNING);
            }
            std::vector<std::wstring> expanded;
            const auto exts = engine_.supportedExtensions();
            for (const auto& a : audio) collectMedia(a, exts, expanded);
            addToPlaylistAndPlay(expanded);
            savePlaylist();
            return 0;
        }
        case kMsgNet: {
            std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(l));
            if (*fn) (*fn)();
            return 0;
        }
        case WM_APP_ENGINE:
            onEngineEvent(static_cast<EngineEvent>(w));
            render();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_DESTROY:
            saveSettings();
            KillTimer(h, kVisTimerId);
            KillTimer(h, kTimerId);
            KillTimer(h, kRenderTimerId);
            KillTimer(h, kMarqueeTimerId);
            hwnd_ = nullptr;  // render() after DestroyWindow becomes a no-op
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------
// Persistence, menus, shortcuts, spectrum analyzer

double MainWindow::position() {
    const ULONGLONG now = GetTickCount64();
    if (now - cacheTick_ >= 100) {
        cacheTick_ = now;
        posCache_ = engine_.position();
        durCache_ = engine_.duration();
    }
    return posCache_;
}

double MainWindow::duration() {
    position();  // refreshes both
    return durCache_;
}

void MainWindow::savePlaylist() {
    savePlaylistFile(Settings::playlistPath(), playlist_);
}

void MainWindow::applySettings() {
    if (!settings_ || !hwnd_) return;
    const SettingsData& d = settings_->data;
    volume_ = d.volume;
    balance_ = d.balance;
    lastSentVolume_ = -1.0f;
    engine_.setVolume(volume_);
    engine_.setBalance(balance_);
    shuffle_ = d.shuffle;
    repeat_ = d.repeat;
    alwaysOnTop_ = d.alwaysOnTop;
    if (alwaysOnTop_)
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (d.hasMainPos)
        SetWindowPos(hwnd_, nullptr, d.mainX, d.mainY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (d.shade != shade_) {
        shade_ = d.shade;
        updateMarqueeTimer();
        resizeWindowAndSurface();
    }
    if (eqWin_) eqWin_->setState(d.eqBands, d.eqPreamp, d.eqEnabled);
    startEqVisible_ = d.eqVisible;
    startPlVisible_ = d.plVisible;

    std::vector<std::wstring> saved;
    if (loadPlaylistFile(Settings::playlistPath(), saved)) playlist_.addMany(saved);
    if (plWin_) plWin_->refresh();
}

void MainWindow::saveSettings() {
    if (!settings_ || !hwnd_) return;
    SettingsData& d = settings_->data;
    d.skinPath = skinPath_;
    d.volume = volume_;
    d.balance = balance_;
    d.shuffle = shuffle_;
    d.repeat = repeat_;
    d.shade = shade_;
    d.eqVisible = eqOn_;
    d.plVisible = plOn_;
    d.alwaysOnTop = alwaysOnTop_;
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (GetWindowPlacement(hwnd_, &wp)) {
        d.hasMainPos = true;
        d.mainX = wp.rcNormalPosition.left;
        d.mainY = wp.rcNormalPosition.top;
    }
    if (eqWin_) eqWin_->getState(d.eqBands, d.eqPreamp, d.eqEnabled);
    settings_->save();
    savePlaylist();
}

void MainWindow::addInitialFiles(const std::vector<std::wstring>& paths, bool play) {
    std::vector<std::wstring> files;
    const auto exts = engine_.supportedExtensions();
    for (const auto& p : paths) collectMedia(p, exts, files);
    if (files.empty()) return;
    const size_t first = playlist_.size();
    playlist_.addMany(files);
    if (plWin_) plWin_->refresh();
    savePlaylist();
    if (play) playIndex(int(first));
}

void MainWindow::doPlay() {
    if (engine_.state() == EngineState::Stopped && !playlist_.empty())
        playIndex(playlist_.current() >= 0 ? playlist_.current() : 0);
    else
        engine_.play();
}

void MainWindow::togglePlaylist() {
    plOn_ = !plOn_;
    if (plWin_) { plWin_->show(plOn_); layoutDocked(); } else plOn_ = false;
    render();
}

void MainWindow::toggleEq() {
    eqOn_ = !eqOn_;
    if (eqWin_) { eqWin_->show(eqOn_); layoutDocked(); } else eqOn_ = false;
    render();
}

void MainWindow::openSkinBrowser() {
    SkinBrowser::show(hwnd_, hinst_, Settings::skinDirs(), skinPath_, [this](const std::wstring& path) {
        std::wstring err;
        if (!loadSkin(path, err))
            MessageBoxW(hwnd_, err.c_str(), L"Could not load skin", MB_OK | MB_ICONWARNING);
    });
}

void MainWindow::setVolumeUi(float v) {
    volume_ = clamp01(v);
    engine_.setVolume(volume_);
    lastSentVolume_ = volume_;
    render();
}

void MainWindow::showMenu() {
    enum : UINT { kSkins = 1, kOpen, kRadio, kClipUrl, kPlaylist, kEq, kTop, kExit };
    HMENU m = CreatePopupMenu();
    if (!m) return;
    AppendMenuW(m, MF_STRING, kSkins, L"Skin Browser...\tAlt+S");
    AppendMenuW(m, MF_STRING, kOpen, L"Play file...");
    AppendMenuW(m, MF_STRING, kRadio, L"Radio...");
    AppendMenuW(m, MF_STRING, kClipUrl, L"Play stream URL from clipboard");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (plOn_ ? MF_CHECKED : 0), kPlaylist, L"Playlist Editor\tAlt+E");
    AppendMenuW(m, MF_STRING | (eqOn_ ? MF_CHECKED : 0), kEq, L"Equalizer\tAlt+G");
    AppendMenuW(m, MF_STRING | (alwaysOnTop_ ? MF_CHECKED : 0), kTop, L"Always on top");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kExit, L"Exit");

    RECT rc;
    GetWindowRect(hwnd_, &rc);
    SetForegroundWindow(hwnd_);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                                     rc.left + 6 * scale_, rc.top + 12 * scale_, 0, hwnd_, nullptr));
    DestroyMenu(m);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    switch (cmd) {
        case kSkins:    openSkinBrowser(); break;
        case kOpen:     activate(Ctl::Eject); break;
        case kRadio:    showRadioMenu(); break;
        case kClipUrl: {
            std::wstring url;
            if (OpenClipboard(hwnd_)) {
                if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) {
                    if (const wchar_t* t = static_cast<const wchar_t*>(GlobalLock(hd))) { url = t; GlobalUnlock(hd); }
                }
                CloseClipboard();
            }
            while (!url.empty() && iswspace(url.back())) url.pop_back();
            while (!url.empty() && iswspace(url.front())) url.erase(url.begin());
            if (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0) {
                const std::wstring u = url;
                auto resolved = std::make_shared<std::wstring>();
                runAsync([u, resolved] { if (!net::resolveStreamUrl(u, *resolved)) resolved->clear(); },
                         [this, u, resolved] { if (!resolved->empty()) playUrl(*resolved, u); });
            } else {
                MessageBoxW(hwnd_, L"Copy an http(s) stream URL first, then choose this entry again.", L"Stream URL", MB_OK);
            }
            break;
        }
        case kPlaylist: togglePlaylist(); break;
        case kEq:       toggleEq(); break;
        case kTop:
            alwaysOnTop_ = !alwaysOnTop_;
            SetWindowPos(hwnd_, alwaysOnTop_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        case kExit:     DestroyWindow(hwnd_); break;
        default: break;
    }
}

void MainWindow::runAsync(std::function<void()> work, std::function<void()> done) {
    const HWND h = hwnd_;
    std::thread([h, work = std::move(work), done = std::move(done)]() mutable {
        work();
        auto* fn = new std::function<void()>(std::move(done));
        if (!PostMessageW(h, kMsgNet, 0, reinterpret_cast<LPARAM>(fn))) delete fn;
    }).detach();
}

void MainWindow::playUrl(const std::wstring& url, const std::wstring& title) {
    const size_t idx = playlist_.size();
    playlist_.add(url, title);
    if (plWin_) plWin_->refresh();
    playIndex(int(idx));
}

void MainWindow::showRadioMenu() {
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    radioMenu_->show(hwnd_, rc.left + 6 * scale_, rc.top + 12 * scale_);
}

bool MainWindow::onKey(WPARAM vk, bool alt) {
    if (alt) {
        switch (vk) {
            case 'E': togglePlaylist(); return true;
            case 'G': toggleEq(); return true;
            case 'S': openSkinBrowser(); return true;
            default: return false;
        }
    }
    switch (vk) {
        case 'Z': activate(Ctl::Prev); return true;
        case 'X': doPlay(); return true;
        case 'C': engine_.pause(); render(); return true;
        case 'V': engine_.stop(); render(); return true;
        case 'B': activate(Ctl::Next); return true;
        case VK_UP:   setVolumeUi(volume_ + 0.05f); return true;
        case VK_DOWN: setVolumeUi(volume_ - 0.05f); return true;
        case VK_LEFT:
        case VK_RIGHT: {
            const double dur = duration();
            if (dur > 0) {
                const double t = position() + (vk == VK_RIGHT ? 5.0 : -5.0);
                engine_.seek(std::min(std::max(0.0, t), dur));
                render();
            }
            return true;
        }
        default: return false;
    }
}

// Smooths the raw engine bars (instant attack, slow fall) and tracks falling peak dots.
// Returns true when the picture needs redrawing.
bool MainWindow::updateVis() {
    float raw[sprites::kVisBars] = {};
    if (engine_.state() == EngineState::Playing) engine_.getSpectrum(raw, sprites::kVisBars);
    bool any = false;
    for (int i = 0; i < sprites::kVisBars; ++i) {
        const float v = clamp01(raw[i]);
        visBars_[i] = v > visBars_[i] ? v : std::max(v, visBars_[i] - 0.09f);
        if (visBars_[i] >= visPeaks_[i]) {
            visPeaks_[i] = visBars_[i];
            visPeakVel_[i] = 0.0f;
        } else {
            visPeakVel_[i] += 0.004f;
            visPeaks_[i] = std::max(0.0f, visPeaks_[i] - visPeakVel_[i]);
        }
        any = any || visBars_[i] > 0.001f || visPeaks_[i] > 0.001f;
    }
    const bool redraw = any || visWasActive_;
    visWasActive_ = any;
    return redraw;
}

void MainWindow::fillRect(int x, int y, int w, int h, uint32_t argb) {
    if (!bits_) return;
    const int S = scale_;
    for (int py = y * S; py < (y + h) * S; ++py) {
        if (py < 0 || py >= surfH_) continue;
        uint32_t* row = bits_ + size_t(py) * surfW_;
        for (int px = x * S; px < (x + w) * S; ++px)
            if (px >= 0 && px < surfW_) row[px] = argb;
    }
}

void MainWindow::drawVis() {
    if (!skin_) return;
    const auto& colors = skin_->visColors();
    auto argb = [&](size_t idx) {
        const COLORREF c = colors[std::min<size_t>(idx, colors.size() - 1)];
        return 0xFF000000u | (uint32_t(GetRValue(c)) << 16) | (uint32_t(GetGValue(c)) << 8) | uint32_t(GetBValue(c));
    };
    const Rect& a = sprites::kVisDst;
    for (int i = 0; i < sprites::kVisBars; ++i) {
        const int x = a.x + i * sprites::kVisBarStep;
        const int h = std::min(a.h, iround(visBars_[i] * float(a.h)));
        // viscolor.txt 2..17 run top to bottom, so row 0 (bottom) uses colour 17. (verify)
        for (int row = 0; row < h; ++row)
            fillRect(x, a.y + a.h - 1 - row, sprites::kVisBarW, 1, argb(size_t(17 - row)));
        const int ph = std::min(a.h, iround(visPeaks_[i] * float(a.h)));
        if (ph > 0) fillRect(x, a.y + a.h - ph, sprites::kVisBarW, 1, argb(23));
    }
}
