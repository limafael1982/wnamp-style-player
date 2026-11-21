#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <vector>

#include "audio/Engine.h"
#include "settings/Settings.h"
#include "ui/MainWindow.h"

namespace {

std::wstring pickSkin() {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.lpstrFilter = L"Winamp skins (*.wsz;*.zip)\0*.wsz;*.zip\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrTitle = L"Choose a Winamp 2 skin";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&ofn) ? std::wstring(buf) : std::wstring();
}

bool exists(const std::wstring& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::exists(p, ec);
}

// A command-line argument is a skin if it is a .wsz/.zip or a folder holding main.bmp.
bool isSkinArg(const std::wstring& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::wstring ext = fs::path(p).extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    if (ext == L".wsz" || ext == L".zip") return true;
    return fs::is_directory(p, ec) && fs::exists(fs::path(p) / L"main.bmp", ec);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);  // manifest also sets this
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    // Command line: skins (.wsz/.zip/folder with main.bmp) and media files/folders, in any order.
    std::wstring skinArg;
    std::vector<std::wstring> media;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (isSkinArg(argv[i])) skinArg = argv[i];
            else media.emplace_back(argv[i]);
        }
        LocalFree(argv);
    }

    Settings settings;
    settings.load();

    auto engine = createEngine();
    MainWindow win(*engine);
    if (!win.create(hinst)) {
        MessageBoxW(nullptr, L"Could not create the main window.", L"Wnamp", MB_OK | MB_ICONERROR);
        return 1;
    }
    win.setSettings(&settings);
    win.applySettings();

    // Skin order: command line, last used, built-in default, then ask.
    const std::wstring defaultSkin = Settings::exeDir() + L"\\skins\\default.wsz";
    const std::wstring candidates[] = {skinArg, settings.data.skinPath, defaultSkin};
    bool loaded = false;
    std::wstring err;
    for (const auto& c : candidates) {
        if (exists(c) && win.loadSkin(c, err)) { loaded = true; break; }
    }
    if (!loaded) {
        const std::wstring chosen = pickSkin();
        if (chosen.empty() || !win.loadSkin(chosen, err)) {
            if (!chosen.empty())
                MessageBoxW(nullptr, err.c_str(), L"Could not load skin", MB_OK | MB_ICONERROR);
            return chosen.empty() ? 0 : 1;
        }
    }
    win.show();
    if (!media.empty()) win.addInitialFiles(media, true);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
