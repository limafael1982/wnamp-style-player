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

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int byteLen = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(size_t(byteLen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), utf8.data(), byteLen, nullptr, nullptr);
    return utf8;
}

// Returns a newly allocated URI string (g_free) or nullptr.
bool isRemote(const std::wstring& path) {
    return path.find(L"://") != std::wstring::npos || path.rfind(L"spotify:", 0) == 0;
}

gchar* uriFromPath(const std::wstring& path) {
    const std::string utf8Path = toUtf8(path);
    if (isRemote(path)) return g_strdup(utf8Path.c_str());
    GError* err = nullptr;
    gchar* uri = gst_filename_to_uri(utf8Path.c_str(), &err);
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
            equalizer_ = gst_bin_get_by_name(GST_BIN(filter), "eq");
            panorama_ = gst_bin_get_by_name(GST_BIN(filter), "pan");
            spectrumElement_ = gst_bin_get_by_name(GST_BIN(filter), "spec");
            g_object_set(playbin_, "audio-filter", filter, nullptr);
        }
        if (spectrumElement_) {
            // The negotiated caps give the real sample rate for every source, local files
            // included, and are the only way to learn it for a live radio stream (where
            // discover() below deliberately skips the blocking GstDiscoverer probe).
            if (GstPad* pad = gst_element_get_static_pad(spectrumElement_, "sink")) {
                gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &GStreamerEngine::onCapsProbe, this,
                                   nullptr);
                gst_object_unref(pad);
            }
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
        GList* factories = gst_type_find_factory_get_list();
        for (GList* node = factories; node; node = node->next) {
            auto* factory = GST_TYPE_FIND_FACTORY(node->data);
            const gchar* const* exts = gst_type_find_factory_get_extensions(factory);
            if (!exts) continue;
            bool isAudio = false;
            if (GstCaps* caps = gst_type_find_factory_get_caps(factory)) {
                for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
                    const gchar* mimeType = gst_structure_get_name(gst_caps_get_structure(caps, i));
                    if (!mimeType) continue;
                    if (g_str_has_prefix(mimeType, "audio/") || g_str_has_prefix(mimeType, "application/ogg") ||
                        g_str_has_prefix(mimeType, "application/x-id3") ||
                        g_str_has_prefix(mimeType, "application/x-apetag") ||
                        g_str_has_prefix(mimeType, "video/x-ms-asf") || g_str_has_prefix(mimeType, "video/x-matroska") ||
                        g_str_has_prefix(mimeType, "video/webm") || g_str_has_prefix(mimeType, "video/quicktime"))
                        isAudio = true;
                }
            }
            if (!isAudio) continue;
            for (; *exts; ++exts) {
                std::wstring ext = L".";
                for (const gchar* ch = *exts; *ch; ++ch) ext += static_cast<wchar_t>(g_ascii_tolower(*ch));
                out.insert(ext);
            }
        }
        gst_plugin_feature_list_free(factories);
        return {out.begin(), out.end()};
    }

    TrackInfo trackInfo() const override {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(infoMutex_));
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
        std::lock_guard<std::mutex> lk(infoMutex_);
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
        float mag[kSpectrumBands];
        {
            // Frames are shown kSpectrumDelayUs after they arrive, which roughly matches the sink's buffering,
            // so the bars follow what is audible. (Independent of pipeline clocks, which differ per source.)
            const gint64 now = g_get_monotonic_time();
            std::lock_guard<std::mutex> lk(spectrumMutex_);
            while (!spectrumQueue_.empty() && spectrumQueue_.front().arrived < now - 1000000) spectrumQueue_.pop_front();
            int pick = -1;
            for (size_t i = 0; i < spectrumQueue_.size(); ++i)
                if (spectrumQueue_[i].arrived <= now - kSpectrumDelayUs) pick = int(i);
            if (pick < 0) return 0;
            std::copy(std::begin(spectrumQueue_[size_t(pick)].mag), std::end(spectrumQueue_[size_t(pick)].mag), mag);
            spectrumQueue_.erase(spectrumQueue_.begin(), spectrumQueue_.begin() + pick);
        }
        // Spectrum covers 0..Nyquist linearly in 64 bands; map log-spaced bars (~40 Hz..16 kHz).
        const float nyquist = spectrumSampleRateHz_ > 0 ? float(spectrumSampleRateHz_) * 0.5f : 22050.0f;
        const float fLo = 40.0f, fHi = std::min(16000.0f, nyquist);
        const float ratio = fHi / fLo;
        for (int i = 0; i < count; ++i) {
            const float f0 = fLo * std::pow(ratio, float(i) / float(count));
            const float f1 = fLo * std::pow(ratio, float(i + 1) / float(count));
            int b0 = int(f0 / nyquist * kSpectrumBands);
            int b1 = int(f1 / nyquist * kSpectrumBands);
            b0 = std::clamp(b0, 0, kSpectrumBands - 1);
            b1 = std::clamp(b1, b0, kSpectrumBands - 1);
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
    void setBalance(float b) override { if (panorama_) g_object_set(panorama_, "panorama", gfloat(std::clamp(b, -1.0f, 1.0f)), nullptr); }
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
        TrackInfo result;
        if (isRemote(path)) {  // live streams: no blocking probe; the UI supplies the title
            std::lock_guard<std::mutex> lk(infoMutex_);
            info_ = result;
            return;
        }
        result.title = std::filesystem::path(path).stem().wstring();  // fallback
        GError* err = nullptr;
        if (GstDiscoverer* discoverer = gst_discoverer_new(2 * GST_SECOND, &err)) {
            GstDiscovererInfo* discovererInfo = gst_discoverer_discover_uri(discoverer, uri, &err);
            if (discovererInfo) {
                if (gst_discoverer_info_get_result(discovererInfo) == GST_DISCOVERER_OK) {
                    const gint64 durationNs = gst_discoverer_info_get_duration(discovererInfo);
                    if (durationNs > 0) result.durationSec = double(durationNs) / GST_SECOND;
                    if (const GstTagList* tags = gst_discoverer_info_get_tags(discovererInfo)) {
                        gchar* tagValue = nullptr;
                        if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &tagValue) && tagValue) {
                            result.title = fromUtf8(tagValue);
                            g_free(tagValue);
                        }
                        tagValue = nullptr;
                        if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &tagValue) && tagValue) {
                            result.artist = fromUtf8(tagValue);
                            g_free(tagValue);
                        }
                    }
                    GList* audioStreams = gst_discoverer_info_get_audio_streams(discovererInfo);
                    if (audioStreams) {
                        auto* audioInfo = GST_DISCOVERER_AUDIO_INFO(audioStreams->data);
                        result.sampleRateHz = int(gst_discoverer_audio_info_get_sample_rate(audioInfo));
                        result.channels = int(gst_discoverer_audio_info_get_channels(audioInfo));
                        guint bitrate = gst_discoverer_audio_info_get_bitrate(audioInfo);
                        if (!bitrate) bitrate = gst_discoverer_audio_info_get_max_bitrate(audioInfo);
                        result.bitrateKbps = int(bitrate / 1000);
                        gst_discoverer_stream_info_list_free(audioStreams);
                    }
                }
                g_object_unref(discovererInfo);
            }
            g_object_unref(discoverer);
        }
        if (err) g_error_free(err);
        // Some MP3s (true VBR with no Xing/VBRI header) give GstDiscoverer no bitrate within its
        // timeout. Fall back to the file's average bitrate from size/duration, like Winamp does.
        if (result.bitrateKbps <= 0 && result.durationSec > 0) {
            std::error_code ec;
            const auto fileBytes = std::filesystem::file_size(path, ec);
            if (!ec && fileBytes > 0)
                result.bitrateKbps = int(double(fileBytes) * 8.0 / result.durationSec / 1000.0 + 0.5);
        }
        if (result.sampleRateHz > 0) spectrumSampleRateHz_ = result.sampleRateHz;
        std::lock_guard<std::mutex> lk(infoMutex_);
        info_ = std::move(result);
    }

    static std::wstring fromUtf8(const char* utf8) {
        const int charLen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (charLen <= 1) return {};
        std::wstring wide(size_t(charLen - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), charLen);
        return wide;
    }

    // Runs on the streaming thread: keep it cheap. Stores the latest spectrum magnitudes (dB).
    void handleElementMessage(GstMessage* message) {
        const GstStructure* structure = gst_message_get_structure(message);
        if (!structure || !gst_structure_has_name(structure, "spectrum")) return;
        const GValue* magnitudeList = gst_structure_get_value(structure, "magnitude");
        if (!magnitudeList || !GST_VALUE_HOLDS_LIST(magnitudeList)) return;
        const guint bandCount = std::min<guint>(gst_value_list_get_size(magnitudeList), kSpectrumBands);
        float magnitudes[kSpectrumBands];
        std::fill(std::begin(magnitudes), std::end(magnitudes), -80.0f);
        for (guint i = 0; i < bandCount; ++i) {
            const GValue* value = gst_value_list_get_value(magnitudeList, i);
            if (value && G_VALUE_HOLDS_FLOAT(value)) magnitudes[i] = g_value_get_float(value);
        }
        SpecFrame frame;
        frame.arrived = g_get_monotonic_time();
        std::copy(std::begin(magnitudes), std::end(magnitudes), frame.mag);
        std::lock_guard<std::mutex> lk(spectrumMutex_);
        spectrumQueue_.push_back(frame);
        while (spectrumQueue_.size() > 64) spectrumQueue_.pop_front();
    }

    void clearSpectrum() {
        std::lock_guard<std::mutex> lk(spectrumMutex_);
        spectrumQueue_.clear();
    }

    // Streaming thread: live-radio metadata (ICY StreamTitle arrives as GST_TAG_TITLE).
    void handleTagMessage(GstMessage* message) {
        GstTagList* tags = nullptr;
        gst_message_parse_tag(message, &tags);
        if (!tags) return;
        bool changed = false;
        if (remote_) {  // ICY StreamTitle arrives as GST_TAG_TITLE on live streams
            gchar* title = nullptr;
            gchar* artist = nullptr;
            gst_tag_list_get_string(tags, GST_TAG_TITLE, &title);
            gst_tag_list_get_string(tags, GST_TAG_ARTIST, &artist);
            if (title && *title) {
                std::lock_guard<std::mutex> lk(infoMutex_);
                info_.title = fromUtf8(title);
                info_.artist = artist ? fromUtf8(artist) : std::wstring();
                changed = true;
            }
            g_free(title);
            g_free(artist);
        }
        // The stream's own parser (mpegaudioparse, etc.) posts this for local files and for
        // live radio alike; it's the only bitrate source at all for a live stream, since
        // discover() skips GstDiscoverer for those.
        guint bitrate = 0;
        if ((gst_tag_list_get_uint(tags, GST_TAG_BITRATE, &bitrate) ||
             gst_tag_list_get_uint(tags, GST_TAG_NOMINAL_BITRATE, &bitrate)) &&
            bitrate > 0) {
            std::lock_guard<std::mutex> lk(infoMutex_);
            info_.bitrateKbps = int(bitrate / 1000);
            changed = true;
        }
        gst_tag_list_unref(tags);
        if (changed) notify(EngineEvent::StreamTags);
    }

    void setState(EngineState newState) {
        state_ = newState;
        notify(EngineEvent::StateChanged, static_cast<LPARAM>(newState));
    }
    void notify(EngineEvent event, LPARAM param = 0) {
        if (HWND window = hwnd_.load()) PostMessageW(window, msg_, static_cast<WPARAM>(event), param);
    }
    void applyEq() {
        if (!equalizer_) return;
        for (int i = 0; i < 10; ++i) {
            const float gainDb = eqOn_ ? std::clamp(bands_[i] + preamp_, -24.0f, 12.0f) : 0.0f;
            char propertyName[16];
            g_snprintf(propertyName, sizeof propertyName, "band%d", i);
            g_object_set(equalizer_, propertyName, double(gainDb), nullptr);
        }
    }

    // Streaming thread: swap in the queued URI for gapless playback.
    static void onAboutToFinish(GstElement* playbin, gpointer self) {
        auto* engine = static_cast<GStreamerEngine*>(self);
        std::string nextUri;
        {
            std::lock_guard<std::mutex> lk(engine->infoMutex_);
            nextUri.swap(engine->nextUri_);
        }
        if (!nextUri.empty()) {
            g_object_set(playbin, "uri", nextUri.c_str(), nullptr);
            engine->notify(EngineEvent::TagsReady);  // UI learns the track changed
        }
    }

    // Streaming thread: fires whenever the audio-filter's input caps (re)negotiate.
    static GstPadProbeReturn onCapsProbe(GstPad*, GstPadProbeInfo* probeInfo, gpointer self) {
        GstEvent* event = GST_PAD_PROBE_INFO_EVENT(probeInfo);
        if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps* caps = nullptr;
            gst_event_parse_caps(event, &caps);
            gint rate = 0;
            if (caps && gst_caps_get_size(caps) > 0 &&
                gst_structure_get_int(gst_caps_get_structure(caps, 0), "rate", &rate) && rate > 0) {
                auto* engine = static_cast<GStreamerEngine*>(self);
                {
                    std::lock_guard<std::mutex> lk(engine->infoMutex_);
                    engine->info_.sampleRateHz = rate;
                }
                engine->spectrumSampleRateHz_ = rate;
                engine->notify(EngineEvent::StreamTags);
            }
        }
        return GST_PAD_PROBE_OK;
    }

    static GstBusSyncReply onBusSync(GstBus*, GstMessage* message, gpointer self) {
        auto* engine = static_cast<GStreamerEngine*>(self);
        switch (GST_MESSAGE_TYPE(message)) {
            case GST_MESSAGE_EOS:
                engine->eos_ = true;
                engine->state_ = EngineState::Stopped;
                engine->notify(EngineEvent::EndOfStream);
                break;
            case GST_MESSAGE_TAG:
                engine->handleTagMessage(message);
                break;
            case GST_MESSAGE_ELEMENT:
                engine->handleElementMessage(message);
                break;
            case GST_MESSAGE_ERROR:
                engine->state_ = EngineState::Stopped;
                engine->notify(EngineEvent::Error);
                break;
            default: break;
        }
        return GST_BUS_DROP;
    }

    GstElement* playbin_ = nullptr;
    GstElement* equalizer_ = nullptr;
    GstElement* panorama_ = nullptr;
    GstElement* spectrumElement_ = nullptr;
    std::atomic<HWND> hwnd_{nullptr};
    UINT msg_ = WM_APP_ENGINE;
    std::atomic<EngineState> state_{EngineState::Stopped};
    std::atomic<bool> eos_{false};
    static constexpr int kSpectrumBands = 64;
    std::mutex spectrumMutex_;
    static constexpr gint64 kSpectrumDelayUs = 150000;  // tune if bars lead/lag the sound
    struct SpecFrame { gint64 arrived = 0; float mag[kSpectrumBands] = {}; };
    std::deque<SpecFrame> spectrumQueue_;
    std::atomic<bool> remote_{false};
    int spectrumSampleRateHz_ = 44100;  // used only to place the bars; spectrum is linear over 0..Nyquist
    std::mutex infoMutex_;
    std::string nextUri_;
    TrackInfo info_;
    bool eqOn_ = true;
    float preamp_ = 0;
    float bands_[10] = {};
};

}  // namespace

std::unique_ptr<IEngine> createGStreamerEngine() {
    auto engine = std::make_unique<GStreamerEngine>();
    if (!engine->init()) return nullptr;
    return engine;
}
