#include "radio/RadioMenu.h"

#include <memory>

namespace {
constexpr UINT kCmdSomaLoad = 800;
constexpr UINT kCmdBuiltinBase = 900;
constexpr UINT kCmdSomaBase = 1000;
}  // namespace

RadioMenu::RadioMenu(RunAsync runAsync, OnSelect onSelect, Anchor anchor)
    : runAsync_(std::move(runAsync)), onSelect_(std::move(onSelect)), anchor_(std::move(anchor)) {}

void RadioMenu::show(HWND owner) {
    lastOwner_ = owner;
    const auto& builtins = stations_;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (size_t i = 0; i < builtins.size(); ++i)
        AppendMenuW(menu, MF_STRING, UINT(kCmdBuiltinBase + i), builtins[i].title.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU soma = nullptr;  // owned by `menu` once attached; destroyed with it
    if (somaChannels_.empty()) {
        AppendMenuW(menu, MF_STRING, kCmdSomaLoad,
                    somaLoading_ ? L"SomaFM (loading...)" : L"SomaFM: load channel list...");
    } else {
        soma = CreatePopupMenu();
        for (size_t i = 0; i < somaChannels_.size(); ++i) {
            std::wstring label = somaChannels_[i].title;
            if (!somaChannels_[i].genre.empty()) label += L"  (" + somaChannels_[i].genre + L")";
            for (size_t ampersandPos = label.find(L'&'); ampersandPos != std::wstring::npos;
                 ampersandPos = label.find(L'&', ampersandPos + 2))
                label.insert(ampersandPos, 1, L'&');
            AppendMenuW(soma, MF_STRING | ((i && i % 25 == 0) ? MF_MENUBARBREAK : 0),
                        UINT(kCmdSomaBase + i), label.c_str());
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(soma), L"SomaFM");
    }

    int x = 0, y = 0;
    anchor_(owner, x, y);
    SetForegroundWindow(owner);
    const UINT cmd = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, owner, nullptr));
    DestroyMenu(menu);  // also destroys the attached SomaFM submenu
    PostMessageW(owner, WM_NULL, 0, 0);

    onCommand(cmd);
}

void RadioMenu::onCommand(UINT cmd) {
    const auto& builtins = stations_;
    if (cmd == kCmdSomaLoad) {
        loadSomaChannels();
    } else if (cmd >= kCmdSomaBase && cmd - kCmdSomaBase < somaChannels_.size()) {
        selectSoma(somaChannels_[cmd - kCmdSomaBase]);
    } else if (cmd >= kCmdBuiltinBase && cmd - kCmdBuiltinBase < builtins.size()) {
        const radio::Station& station = builtins[cmd - kCmdBuiltinBase];
        onSelect_(station.url, station.title);
    }
}

void RadioMenu::loadSomaChannels() {
    if (somaLoading_) return;
    somaLoading_ = true;
    auto list = std::make_shared<std::vector<net::RadioChannel>>();
    runAsync_([list] { if (!net::fetchSomaChannels(*list)) list->clear(); },
              [this, list] {
                  somaLoading_ = false;
                  if (list->empty()) {
                      MessageBoxW(lastOwner_, L"Could not load the SomaFM channel list (network?).", L"SomaFM",
                                  MB_OK | MB_ICONWARNING);
                      return;
                  }
                  somaChannels_ = std::move(*list);
                  show(lastOwner_);  // channel list just arrived: reopen, re-anchored to the window's current spot
              });
}

void RadioMenu::selectSoma(const net::RadioChannel& channel) {
    const std::wstring playlistUrl = channel.playlistUrl, title = L"SomaFM: " + channel.title;
    auto stream = std::make_shared<std::wstring>();
    runAsync_([playlistUrl, stream] { if (!net::resolveStreamUrl(playlistUrl, *stream)) stream->clear(); },
              [this, stream, title] {
                  if (stream->empty()) {
                      MessageBoxW(lastOwner_, L"Could not resolve the radio stream.", L"SomaFM", MB_OK | MB_ICONWARNING);
                      return;
                  }
                  onSelect_(*stream, title);
              });
}
