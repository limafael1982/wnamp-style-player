// The only translation unit family allowed to include GStreamer headers.
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include <filesystem>

#include <algorithm>
#include <atomic>
#include <deque>
#include <cmath>
#include <iterator>
#include <mutex>
#include <set>

#include "audio/EngineFactories.h"

namespace {

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Returns a newly allocated URI string (g_free) or nullptr.
bool isRemote(const std::wstring& p) {
    return p.find(L"://") != std::wstring::npos || p.rfind(L"spotify:", 0) == 0;
}

gchar* uriFromPath(const std::wstring& path) {
    const std::string u8 = toUtf8(path);
    if (isRemote(path)) return g_strdup(u8.c_str());
    GError* err = nullptr;
    gchar* uri = gst_filename_to_uri(u8.c_str(), &err);
    if (err) g_error_free(err);
    return uri;
}

class GStreamerEngine final : public IEngine {
public:
    GStreamerEngine() = default;
    ~GStreamerEngine() override {
        if (playbin_) {
            GstBus* bus = gst_element_get_bus(playbin_);
            gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
            gst_object_unref(bus);
            gst_element_set_state(playbin_, GST_STATE_NULL);
            gst_object_unref(playbin_);
        }
    }

    bool init() {
        gst_init(nullptr, nullptr);
        playbin_ = gst_element_factory_make("playbin", "player");
        if (!playbin_) return false;

        // equalizer-10bands ! audiopanorama ! spectrum (spectrum feeds the visualizer later)
        GError* err = nullptr;
        GstElement* filter = gst_parse_bin_from_description(
            "audioconvert ! equalizer-10bands name=eq ! audiopanorama name=pan ! "
            "spectrum name=spec bands=64 threshold=-80 interval=25000000 "
            "post-messages=true message-magnitude=true",
            TRUE, &err);
        if (err) g_error_free(err);
        if (filter) {
            eq_ = gst_bin_get_by_name(GST_BIN(filter), "eq");
            pan_ = gst_bin_get_by_name(GST_BIN(filter), "pan");
            g_object_set(playbin_, "audio-filter", filter, nullptr);
        }

        g_signal_connect(playbin_, "about-to-finish", G_CALLBACK(&GStreamerEngine::onAboutToFinish), this);

        // No GLib main loop: handle bus messages synchronously on the posting thread,
        // and forward them to the UI with PostMessage.
        GstBus* bus = gst_element_get_bus(playbin_);
        gst_bus_set_sync_handler(bus, &GStreamerEngine::onBusSync, this, nullptr);
        gst_object_unref(bus);
        return true;
    }

    void setNotifyWindow(HWND hwnd, UINT msg) override { msg_ = msg; hwnd_ = hwnd; }

    // Built from GStreamer's typefind factories, so it matches whatever plugins are installed.
    std::vector<std::wstring> supportedExtensions() const override {
        std::set<std::wstring> out;
        GList* list = gst_type_find_factory_get_list();
        for (GList* l = list; l; l = l->next) {
            auto* f = GST_TYPE_FIND_FACTORY(l->data);
            const gchar* const* exts = gst_type_find_factory_get_extensions(f);
            if (!exts) continue;
            bool audio = false;
            if (GstCaps* caps = gst_type_find_factory_get_caps(f)) {
                for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
                    const gchar* n = gst_structure_get_name(gst_caps_get_structure(caps, i));
                    if (!n) continue;
                    if (g_str_has_prefix(n, "audio/") || g_str_has_prefix(n, "application/ogg") ||
                        g_str_has_prefix(n, "application/x-id3") || g_str_has_prefix(n, "application/x-apetag") ||
                        g_str_has_prefix(n, "video/x-ms-asf") || g_str_has_prefix(n, "video/x-matroska") ||
                        g_str_has_prefix(n, "video/webm") || g_str_has_prefix(n, "video/quicktime"))
                        audio = true;
                }
            }
            if (!audio) continue;
            for (; *exts; ++exts) {
                std::wstring e = L".";
                for (const gchar* c = *exts; *c; ++c) e += static_cast<wchar_t>(g_ascii_tolower(*c));
                out.insert(e);
            }
        }
        gst_plugin_feature_list_free(list);
        return {out.begin(), out.end()};
    }

    TrackInfo trackInfo() const override {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(mu_));
        return info_;
    }

    void updateInfoFor(const std::wstring& path) override {
        if (gchar* uri = uriFromPath(path)) { discover(uri, path); g_free(uri); }
    }

    bool open(const std::wstring& path) override {
        gchar* uri = uriFromPath(path);
        if (!uri) return false;
        discover(uri, path);
        remote_ = isRemote(path);
        clearSpectrum();
        gst_element_set_state(playbin_, GST_STATE_NULL);
        eos_ = false;
        g_object_set(playbin_, "uri", uri, nullptr);
        g_free(uri);
        state_ = EngineState::Stopped;
        // PAUSED prerolls so duration becomes available; logical state stays Stopped.
        gst_element_set_state(playbin_, GST_STATE_PAUSED);
        return true;
    }

    void queueNext(const std::wstring& path) override {
        std::lock_guard<std::mutex> lk(mu_);
        nextUri_ = path.empty() ? std::string() : [&] {
            gchar* u = uriFromPath(path);
            std::string s = u ? u : "";
            g_free(u);
            return s;
        }();
    }

    void play() override {
        if (eos_) { gst_element_set_state(playbin_, GST_STATE_READY); eos_ = false; }
        gst_element_set_state(playbin_, GST_STATE_PLAYING);
        setState(EngineState::Playing);
    }
    void pause() override {
        if (state_ == EngineState::Playing) {
            gst_element_set_state(playbin_, GST_STATE_PAUSED);
            setState(EngineState::Paused);
        } else if (state_ == EngineState::Paused) {
            gst_element_set_state(playbin_, GST_STATE_PLAYING);
            setState(EngineState::Playing);
        }
    }
    void stop() override {
        gst_element_set_state(playbin_, GST_STATE_READY);
        eos_ = false;
        setState(EngineState::Stopped);
    }
    void seek(double sec) override {
        clearSpectrum();
        const gint64 ns = static_cast<gint64>(std::max(0.0, sec) * GST_SECOND);
        gst_element_seek_simple(playbin_, GST_FORMAT_TIME,
                                GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), ns);
    }

    int getSpectrum(float* bars, int count) override {
        if (!bars || count <= 0) return 0;
        for (int i = 0; i < count; ++i) bars[i] = 0.0f;
        if (state_ != EngineState::Playing) return 0;
        float mag[kSpecBands];
        {
            // Frames are shown kSpecDelayUs after they arrive, which roughly matches the sink's buffering,
            // so the bars follow what is audible. (Independent of pipeline clocks, which differ per source.)
            const gint64 now = g_get_monotonic_time();
            std::lock_guard<std::mutex> lk(specMu_);
            while (!specQ_.empty() && specQ_.front().arrived < now - 1000000) specQ_.pop_front();
            int pick = -1;
            for (size_t i = 0; i < specQ_.size(); ++i)
                if (specQ_[i].arrived <= now - kSpecDelayUs) pick = int(i);
            if (pick < 0) return 0;
            std::copy(std::begin(specQ_[size_t(pick)].mag), std::end(specQ_[size_t(pick)].mag), mag);
            specQ_.erase(specQ_.begin(), specQ_.begin() + pick);
        }
        // Spectrum covers 0..Nyquist linearly in 64 bands; map log-spaced bars (~40 Hz..16 kHz).
        const float nyquist = specRate_ > 0 ? float(specRate_) * 0.5f : 22050.0f;
        const float fLo = 40.0f, fHi = std::min(16000.0f, nyquist);
        const float ratio = fHi / fLo;
        for (int i = 0; i < count; ++i) {
            const float f0 = fLo * std::pow(ratio, float(i) / float(count));
            const float f1 = fLo * std::pow(ratio, float(i + 1) / float(count));
            int b0 = int(f0 / nyquist * kSpecBands);
            int b1 = int(f1 / nyquist * kSpecBands);
            b0 = std::clamp(b0, 0, kSpecBands - 1);
            b1 = std::clamp(b1, b0, kSpecBands - 1);
            float db = -80.0f;
            for (int k = b0; k <= b1; ++k) db = std::max(db, mag[k]);
            bars[i] = std::clamp((db + 80.0f) / 80.0f, 0.0f, 1.0f);
        }
        return count;
    }

    EngineState state() const override { return state_; }
    double position() const override {
        gint64 p = 0;
        if (state_ == EngineState::Stopped || !gst_element_query_position(playbin_, GST_FORMAT_TIME, &p)) return 0;
        return double(p) / GST_SECOND;
    }
    double duration() const override {
        gint64 d = 0;
        if (!gst_element_query_duration(playbin_, GST_FORMAT_TIME, &d) || d < 0) return 0;
        return double(d) / GST_SECOND;
    }

    void setVolume(float v) override { g_object_set(playbin_, "volume", double(std::clamp(v, 0.0f, 1.0f)), nullptr); }
    void setBalance(float b) override { if (pan_) g_object_set(pan_, "panorama", gfloat(std::clamp(b, -1.0f, 1.0f)), nullptr); }
    void setEqEnabled(bool on) override { eqOn_ = on; applyEq(); }
    void setEqPreamp(float db) override { preamp_ = db; applyEq(); }
    void setEqBand(int band, float db) override {
        if (band < 0 || band > 9) return;
        bands_[band] = db;
        applyEq();
    }

private:
    // Reads tags + stream info synchronously (GstDiscoverer, 2 s timeout).
    void discover(const gchar* uri, const std::wstring& path) {
        TrackInfo ti;
        if (isRemote(path)) {  // live streams: no blocking probe; the UI supplies the title
            std::lock_guard<std::mutex> lk(mu_);
            info_ = ti;
            return;
        }
        ti.title = std::filesystem::path(path).stem().wstring();  // fallback
        GError* err = nullptr;
        if (GstDiscoverer* d = gst_discoverer_new(2 * GST_SECOND, &err)) {
            GstDiscovererInfo* di = gst_discoverer_discover_uri(d, uri, &err);
            if (di) {
                if (gst_discoverer_info_get_result(di) == GST_DISCOVERER_OK) {
                    const gint64 dur = gst_discoverer_info_get_duration(di);
                    if (dur > 0) ti.durationSec = double(dur) / GST_SECOND;
                    if (const GstTagList* tags = gst_discoverer_info_get_tags(di)) {
                        gchar* s = nullptr;
                        if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &s) && s) { ti.title = fromUtf8(s); g_free(s); }
                        s = nullptr;
                        if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &s) && s) { ti.artist = fromUtf8(s); g_free(s); }
                    }
                    GList* streams = gst_discoverer_info_get_audio_streams(di);
                    if (streams) {
                        auto* a = GST_DISCOVERER_AUDIO_INFO(streams->data);
                        ti.sampleRateHz = int(gst_discoverer_audio_info_get_sample_rate(a));
                        ti.channels = int(gst_discoverer_audio_info_get_channels(a));
                        guint br = gst_discoverer_audio_info_get_bitrate(a);
                        if (!br) br = gst_discoverer_audio_info_get_max_bitrate(a);
                        ti.bitrateKbps = int(br / 1000);
                        gst_discoverer_stream_info_list_free(streams);
                    }
                }
                g_object_unref(di);
            }
            g_object_unref(d);
        }
        if (err) g_error_free(err);
        if (ti.sampleRateHz > 0) specRate_ = ti.sampleRateHz;
        std::lock_guard<std::mutex> lk(mu_);
        info_ = std::move(ti);
    }

    static std::wstring fromUtf8(const char* s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (n <= 1) return {};
        std::wstring w(size_t(n - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
        return w;
    }

    // Runs on the streaming thread: keep it cheap. Stores the latest spectrum magnitudes (dB).
    void handleElementMessage(GstMessage* m) {
        const GstStructure* st = gst_message_get_structure(m);
        if (!st || !gst_structure_has_name(st, "spectrum")) return;
        const GValue* list = gst_structure_get_value(st, "magnitude");
        if (!list || !GST_VALUE_HOLDS_LIST(list)) return;
        const guint n = std::min<guint>(gst_value_list_get_size(list), kSpecBands);
        float tmp[kSpecBands];
        std::fill(std::begin(tmp), std::end(tmp), -80.0f);
        for (guint i = 0; i < n; ++i) {
            const GValue* v = gst_value_list_get_value(list, i);
            if (v && G_VALUE_HOLDS_FLOAT(v)) tmp[i] = g_value_get_float(v);
        }
        SpecFrame f;
        f.arrived = g_get_monotonic_time();
        std::copy(std::begin(tmp), std::end(tmp), f.mag);
        std::lock_guard<std::mutex> lk(specMu_);
        specQ_.push_back(f);
        while (specQ_.size() > 64) specQ_.pop_front();
    }

    void clearSpectrum() {
        std::lock_guard<std::mutex> lk(specMu_);
        specQ_.clear();
    }

    // Streaming thread: live-radio metadata (ICY StreamTitle arrives as GST_TAG_TITLE).
    void handleTagMessage(GstMessage* m) {
        if (!remote_) return;
        GstTagList* tags = nullptr;
        gst_message_parse_tag(m, &tags);
        if (!tags) return;
        gchar* title = nullptr;
        gchar* artist = nullptr;
        gst_tag_list_get_string(tags, GST_TAG_TITLE, &title);
        gst_tag_list_get_string(tags, GST_TAG_ARTIST, &artist);
        bool changed = false;
        if (title && *title) {
            std::lock_guard<std::mutex> lk(mu_);
            info_.title = fromUtf8(title);
            info_.artist = artist ? fromUtf8(artist) : std::wstring();
            changed = true;
        }
        g_free(title);
        g_free(artist);
        gst_tag_list_unref(tags);
        if (changed) notify(EngineEvent::StreamTags);
    }

    void setState(EngineState s) {
        state_ = s;
        notify(EngineEvent::StateChanged, static_cast<LPARAM>(s));
    }
    void notify(EngineEvent e, LPARAM l = 0) {
        if (HWND h = hwnd_.load()) PostMessageW(h, msg_, static_cast<WPARAM>(e), l);
    }
    void applyEq() {
        if (!eq_) return;
        for (int i = 0; i < 10; ++i) {
            const float g = eqOn_ ? std::clamp(bands_[i] + preamp_, -24.0f, 12.0f) : 0.0f;
            char name[16];
            g_snprintf(name, sizeof name, "band%d", i);
            g_object_set(eq_, name, double(g), nullptr);
        }
    }

    // Streaming thread: swap in the queued URI for gapless playback.
    static void onAboutToFinish(GstElement* pb, gpointer self) {
        auto* e = static_cast<GStreamerEngine*>(self);
        std::string next;
        {
            std::lock_guard<std::mutex> lk(e->mu_);
            next.swap(e->nextUri_);
        }
        if (!next.empty()) {
            g_object_set(pb, "uri", next.c_str(), nullptr);
            e->notify(EngineEvent::TagsReady);  // UI learns the track changed
        }
    }

    static GstBusSyncReply onBusSync(GstBus*, GstMessage* m, gpointer self) {
        auto* e = static_cast<GStreamerEngine*>(self);
        switch (GST_MESSAGE_TYPE(m)) {
            case GST_MESSAGE_EOS:
                e->eos_ = true;
                e->state_ = EngineState::Stopped;
                e->notify(EngineEvent::EndOfStream);
                break;
            case GST_MESSAGE_TAG:
                e->handleTagMessage(m);
                break;
            case GST_MESSAGE_ELEMENT:
                e->handleElementMessage(m);
                break;
            case GST_MESSAGE_ERROR:
                e->state_ = EngineState::Stopped;
                e->notify(EngineEvent::Error);
                break;
            default: break;
        }
        return GST_BUS_DROP;
    }

    GstElement* playbin_ = nullptr;
    GstElement* eq_ = nullptr;
    GstElement* pan_ = nullptr;
    std::atomic<HWND> hwnd_{nullptr};
    UINT msg_ = WM_APP_ENGINE;
    std::atomic<EngineState> state_{EngineState::Stopped};
    std::atomic<bool> eos_{false};
    static constexpr int kSpecBands = 64;
    std::mutex specMu_;
    static constexpr gint64 kSpecDelayUs = 150000;  // tune if bars lead/lag the sound
    struct SpecFrame { gint64 arrived = 0; float mag[kSpecBands] = {}; };
    std::deque<SpecFrame> specQ_;
    std::atomic<bool> remote_{false};
    int specRate_ = 44100;  // used only to place the bars; spectrum is linear over 0..Nyquist
    std::mutex mu_;
    std::string nextUri_;
    TrackInfo info_;
    bool eqOn_ = true;
    float preamp_ = 0;
    float bands_[10] = {};
};

}  // namespace

std::unique_ptr<IEngine> createGStreamerEngine() {
    auto e = std::make_unique<GStreamerEngine>();
    if (!e->init()) return nullptr;
    return e;
}
