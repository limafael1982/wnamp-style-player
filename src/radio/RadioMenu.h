#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "net/SomaFm.h"
#include "radio/Station.h"

// Builds and drives the "Radio..." popup menu (built-in stations + an on-demand SomaFM
// submenu) and turns the user's choice into a playable (url, title) pair.
//
// This class owns everything specific to "how do we present radio sources and resolve
// a choice to a stream" so MainWindow doesn't have to: it knows nothing about HWND
// message pumps, menu command IDs elsewhere in the app, or playback/playlist internals.
// It depends only on abstractions it's given (RunAsync, OnSelect, Anchor) — not on
// MainWindow — so it can be reused or tested without it (dependency inversion).
class RadioMenu {
public:
    // Runs `work` off the UI thread, then calls `done` back on the UI thread. The
    // caller (MainWindow) owns what "UI thread" means; RadioMenu just needs this done.
    using RunAsync = std::function<void(std::function<void()> work, std::function<void()> done)>;
    // Called with a resolved, directly-playable stream URL and a display title.
    using OnSelect = std::function<void(const std::wstring& url, const std::wstring& title)>;
    // Computes the popup's screen-coordinate anchor point for `owner`, freshly each
    // time it's called — the owner window may have moved since the menu was last shown.
    using Anchor = std::function<void(HWND owner, int& x, int& y)>;

    RadioMenu(RunAsync runAsync, OnSelect onSelect, Anchor anchor);

    // Builds the popup menu, blocks until the user picks something or dismisses it, and
    // (for an entry that needs resolving) kicks off the async work. `owner` positions
    // and owns the native popup.
    void show(HWND owner);

private:
    void onCommand(UINT cmd);
    void loadSomaChannels();
    void selectSoma(const net::RadioChannel& channel);

    HWND lastOwner_ = nullptr;
    RunAsync runAsync_;
    OnSelect onSelect_;
    Anchor anchor_;
    std::vector<net::RadioChannel> somaChannels_;
    bool somaLoading_ = false;
};
