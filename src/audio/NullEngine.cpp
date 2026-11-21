#include "audio/Engine.h"
#include "audio/EngineFactories.h"

namespace {
class NullEngine final : public IEngine {
public:
    void setNotifyWindow(HWND hwnd, UINT msg) override { hwnd_ = hwnd; msg_ = msg; }
    std::vector<std::wstring> supportedExtensions() const override {
        return {L".mp3", L".flac", L".ogg", L".opus", L".wav", L".m4a", L".aac", L".wma"};
    }
    bool open(const std::wstring&) override { state_ = EngineState::Stopped; pos_ = 0; return true; }
    void queueNext(const std::wstring&) override {}
    void play() override { state_ = EngineState::Playing; notify(); }
    void pause() override {
        if (state_ == EngineState::Playing) state_ = EngineState::Paused;
        else if (state_ == EngineState::Paused) state_ = EngineState::Playing;
        notify();
    }
    void stop() override { state_ = EngineState::Stopped; pos_ = 0; notify(); }
    void seek(double s) override { pos_ = s; }

    TrackInfo trackInfo() const override { return {}; }
    void updateInfoFor(const std::wstring&) override {}
    int getSpectrum(float* bars, int count) override {
        for (int i = 0; i < count; ++i) bars[i] = 0.0f;
        return 0;
    }
    EngineState state() const override { return state_; }
    double position() const override { return pos_; }
    double duration() const override { return 0; }

    void setVolume(float) override {}
    void setBalance(float) override {}
    void setEqEnabled(bool) override {}
    void setEqPreamp(float) override {}
    void setEqBand(int, float) override {}

private:
    void notify() {
        if (hwnd_) PostMessageW(hwnd_, msg_, static_cast<WPARAM>(EngineEvent::StateChanged),
                                static_cast<LPARAM>(state_));
    }
    HWND hwnd_ = nullptr;
    UINT msg_ = WM_APP_ENGINE;
    EngineState state_ = EngineState::Stopped;
    double pos_ = 0;
};
}  // namespace

std::unique_ptr<IEngine> createNullEngine() { return std::make_unique<NullEngine>(); }
