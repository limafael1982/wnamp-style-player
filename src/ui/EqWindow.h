#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>

#include "audio/IEngine.h"
#include "skin/Skin.h"
#include "skin/SpritesEq.h"

// Equalizer window: layered per-pixel-alpha popup drawn from eq.bmp.
// Sliders: index 0 = preamp, 1..10 = the ten bands.
class EqWindow {
public:
    explicit EqWindow(IEngine& engine);
    ~EqWindow();
    EqWindow(const EqWindow&) = delete;
    EqWindow& operator=(const EqWindow&) = delete;

    bool create(HINSTANCE hinst, HWND owner);
    void setSkin(std::shared_ptr<Skin> skin);
    void setScale(int scale);
    void show(bool visible);
    bool visible() const;
    void moveTo(int x, int y);
    void getRect(RECT& out) const;
    HWND hwnd() const { return hwnd_; }
    void setOnClosed(std::function<void()> cb) { onClosed_ = std::move(cb); }

    // Applies 10 band gains (dB, clamped to [-12,12]) and the preamp; pushes to the engine and redraws.
    void setPreset(const float bands[10], float preamp);
    // State access so the main window can persist the EQ. setState also sets the ON flag.
    void getState(float bands[10], float& preamp, bool& enabled) const;
    void setState(const float bands[10], float preamp, bool enabled);

private:
    static LRESULT CALLBACK ThunkProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT proc(HWND, UINT, WPARAM, LPARAM);

    int partAt(int skinX, int skinY) const;
    void updateSlider(int index, int skinY);
    float normOf(int index) const;  // 0 = top (+12 dB) .. 1 = bottom (-12 dB)

    void applyAllToEngine();
    void showPresetMenu();
    void drawCurve(const Image& eq);
    void fillRect(int x, int y, int w, int h, uint32_t px);

    void resizeSurface();
    void markDirty();
    void render();
    void blit(const Image& img, const Rect& src, int dx, int dy);
    void drawToggle(const Image& img, const Rect& dst, const Rect& offUp, const Rect& offDown,
                    const Rect& onUp, const Rect& onDown, bool state, int part);

    IEngine& engine_;
    HWND hwnd_ = nullptr;
    std::shared_ptr<Skin> skin_;
    int scale_ = 1;

    HDC memDC_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ oldBmp_ = nullptr;
    uint32_t* bits_ = nullptr;
    int surfW_ = 0, surfH_ = 0;

    bool active_ = true;
    bool dirty_ = false;
    bool timerOn_ = false;

    bool eqOn_ = true;
    bool auto_ = false;
    float gains_[11] = {};  // dB, [0] = preamp

    int pressedPart_ = -1;
    bool pressedInside_ = false;
    int dragSlider_ = -1;
    int grabOffset_ = 0;

    std::function<void()> onClosed_;
};
