#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "radio/Station.h"

// Modeless preferences window: a sidebar of categories ("Skin", "Behavior",
// "Radios") with a matching page on the right. Every control applies its change
// immediately through the supplied callbacks — there is no OK/Cancel/Apply split,
// the same immediate-effect style as the rest of the app's secondary windows
// (EqWindow, PlaylistWindow).
//
// Like RadioMenu, this class depends only on the abstractions it's given, not on
// MainWindow directly, so it knows nothing about playback, playlists, or Settings'
// on-disk format.
class SettingsWindow {
public:
    using VoidAction = std::function<void()>;
    using StringGetter = std::function<std::wstring()>;
    using BoolGetter = std::function<bool()>;
    using BoolSetter = std::function<void(bool)>;
    using StationsGetter = std::function<std::vector<radio::Station>()>;
    using StationsSetter = std::function<void(const std::vector<radio::Station>&)>;

    struct Callbacks {
        StringGetter currentSkinName;  // display only, e.g. the current skin's file name
        VoidAction openSkinBrowser;    // opens the existing Skin Browser window

        BoolGetter getShowTrayIcon;
        BoolSetter setShowTrayIcon;
        BoolGetter getKeepRunningInBackground;
        BoolSetter setKeepRunningInBackground;

        StationsGetter getStations;
        StationsSetter setStations;
    };

    // Opens the window (or brings an already-open one to front and refreshes its
    // displayed values). `owner` is the main window, used as the Win32 owner.
    static void show(HWND owner, HINSTANCE hinst, Callbacks callbacks);
};
