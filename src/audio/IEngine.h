#pragma once
// Engine interface used by the UI. NO GStreamer headers here or anywhere
// outside src/audio/*.cpp implementations.
#include <windows.h>
#include <string>
#include <vector>

enum class EngineState { Stopped, Playing, Paused };

// Sent to the notify window via PostMessage(hwnd, msg, (WPARAM)EngineEvent, lParam).
enum class EngineEvent : WPARAM {
    StateChanged,  // lParam: EngineState
    EndOfStream,
    Error,
    TagsReady, StreamTags,
    Position,      // periodic tick, UI may poll position()
};

constexpr UINT WM_APP_ENGINE = WM_APP + 1;

struct TrackInfo {
    std::wstring artist, title;
    int bitrateKbps = 0;
    int sampleRateHz = 0;
    int channels = 0;
    double durationSec = 0;
};

class IEngine {
public:
    virtual ~IEngine() = default;

    virtual void setNotifyWindow(HWND hwnd, UINT msg = WM_APP_ENGINE) = 0;

    // Lowercase extensions (with leading dot) the engine can play, e.g. L".mp3".
    virtual std::vector<std::wstring> supportedExtensions() const = 0;

    virtual bool open(const std::wstring& path) = 0;
    // Gapless: path of the track to start when the current one is about to finish.
    virtual void queueNext(const std::wstring& path) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(double seconds) = 0;

    // Info about the most recently opened track (empty/zero fields if unknown).
    virtual TrackInfo trackInfo() const = 0;
    // Re-reads tags/stream info for `path` without touching playback (used after a gapless switch).
    virtual void updateInfoFor(const std::wstring& path) = 0;

    // Fills `count` bars (0..1, log-spaced ~40 Hz..16 kHz) from the latest spectrum measurement.
    // Returns count, or 0 (bars zero-filled) when not playing or no data yet. Poll from the UI.
    virtual int getSpectrum(float* bars, int count) = 0;

    virtual EngineState state() const = 0;
    virtual double position() const = 0;  // seconds
    virtual double duration() const = 0;  // seconds, 0 if unknown

    virtual void setVolume(float v01) = 0;       // 0..1
    virtual void setBalance(float minus1to1) = 0;
    virtual void setEqEnabled(bool on) = 0;
    virtual void setEqPreamp(float db) = 0;
    virtual void setEqBand(int band0to9, float db) = 0;
};
