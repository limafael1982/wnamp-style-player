#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <functional>

#include "audio/IEngine.h"
#include "net/SomaFm.h"
#include "playlist/Playlist.h"
#include "settings/Settings.h"
#include "ui/EqWindow.h"
#include "ui/PlaylistWindow.h"
#include "skin/Skin.h"
#include "skin/Sprites.h"

// Main player window: a layered (per-pixel alpha) popup window that draws the
// skin at an integer scale and is shaped by region.txt. Buttons, toggles and
// sliders share one control table used for drawing and hit-testing.
class MainWindow {
public:
    explicit MainWindow(IEngine& engine);
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool create(HINSTANCE hinst);
    void show();
    bool loadSkin(const std::wstring& path, std::wstring& err);
    HWND hwnd() const { return hwnd_; }

    // Persistence: call setSettings() + applySettings() after create(), before show().
    void setSettings(Settings* s) { settings_ = s; }
    void applySettings();
    // Files and/or folders (e.g. from the command line); folders are scanned recursively.
    void addInitialFiles(const std::vector<std::wstring>& paths, bool play);

private:
    enum class Ctl {
        None, Menu, Minimize, Shade, Close,
        Prev, Play, Pause, Stop, Next, Eject,
        Shuffle, Repeat, Eq, Playlist,
        Position, Volume, Balance,
    };
    enum class Kind { Button, Toggle, Slider };
    enum : unsigned { kNormal = 1, kShade = 2, kBoth = 3 };  // which window mode shows a control

    struct ControlDef {
        Ctl id;
        Kind kind;
        unsigned modes;
        const wchar_t* sheet;
        Rect dst;
        Rect up, down;      // buttons/toggles (toggle: "off" sprites)
        Rect upOn, downOn;  // toggles only
    };
    static const ControlDef* controlTable(size_t& count);

    static LRESULT CALLBACK ThunkProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT proc(HWND, UINT, WPARAM, LPARAM);

    unsigned currentMode() const { return shade_ ? kShade : kNormal; }
    const ControlDef* hitControl(int skinX, int skinY) const;
    const ControlDef* findDef(Ctl id) const;
    bool toggleState(Ctl id) const;
    void activate(Ctl id);
    void beginSliderDrag(const ControlDef& d, int skinX);
    void updateSlider(Ctl id, int skinX);
    void windowPixelToSkin(POINT clientPt, int& sx, int& sy) const;

    void setScaleFromDpi(UINT dpi);
    void resizeWindowAndSurface();
    void applyRegion();
    void render();      // coalesced: draws now, then at most once per ~16 ms
    void renderNow();
    void drawText(const std::wstring& text, int x, int y, int clipX0, int clipX1, int scrollPx);
    void drawSongInfo();
    void refreshTrackInfo();
    double position();   // cached engine queries (refreshed at most every ~100 ms)
    double duration();
    void playIndex(int index);
    void doPlay();
    void togglePlaylist();
    void toggleEq();
    void showMenu();
    void showRadioMenu();
    void runAsync(std::function<void()> work, std::function<void()> done);
    void playUrl(const std::wstring& url, const std::wstring& title);
    void playRadio(const net::RadioChannel& ch);
    void openSkinBrowser();
    void setVolumeUi(float v);
    bool onKey(WPARAM vk, bool alt);
    void savePlaylist();
    void saveSettings();
    bool updateVis();
    void drawVis();
    void fillRect(int x, int y, int w, int h, uint32_t argb);
    void requeueNext();
    void layoutDocked();
    void onEngineEvent(EngineEvent e);
    void addToPlaylistAndPlay(const std::vector<std::wstring>& paths);
    void updateMarqueeTimer();
    void blit(const Image& img, const Rect& src, int dx, int dy);
    void drawControls();
    void drawPositionBar(const ControlDef& d);
    void drawVolume(const ControlDef& d);
    void drawBalance(const ControlDef& d);
    void drawTime();
    void drawStateIndicator();

    std::vector<net::RadioChannel> radio_;
    bool radioLoading_ = false;
    IEngine& engine_;
    HWND hwnd_ = nullptr;
    std::shared_ptr<Skin> skin_;
    int scale_ = 1;

    HDC memDC_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ oldBmp_ = nullptr;
    uint32_t* bits_ = nullptr;
    int surfW_ = 0, surfH_ = 0;

    HINSTANCE hinst_ = nullptr;
    double posCache_ = 0, durCache_ = 0;
    ULONGLONG cacheTick_ = 0;
    float lastSentBalance_ = -2.0f;
    Settings* settings_ = nullptr;
    std::wstring skinPath_;
    bool alwaysOnTop_ = false;
    bool startEqVisible_ = false, startPlVisible_ = false;
    float visBars_[19] = {}, visPeaks_[19] = {}, visPeakVel_[19] = {};
    bool visWasActive_ = false;
    Playlist playlist_;
    std::unique_ptr<EqWindow> eqWin_;
    std::unique_ptr<PlaylistWindow> plWin_;
    int queuedIndex_ = -1;   // track handed to the engine for gapless playback
    TrackInfo trackInfo_;
    std::wstring infoText_;
    int marqueeOffset_ = 0;
    bool marqueeTimerOn_ = false;
    bool dirty_ = false;
    bool renderCooling_ = false;
    float lastSentVolume_ = -1.0f;

    bool active_ = true;
    bool shade_ = false;

    // Interaction state.
    Ctl pressed_ = Ctl::None;
    bool pressedInside_ = false;

    // Widget state (toggles are UI state only for now).
    bool shuffle_ = false, repeat_ = false, eqOn_ = false, plOn_ = false;
    float volume_ = 0.8f;    // 0..1
    float balance_ = 0.0f;   // -1..1
    bool posDragging_ = false;
    float posDrag_ = 0.0f;   // 0..1 while dragging
    int posGrab_ = 0;        // pointer offset inside the thumb, skin px
};
