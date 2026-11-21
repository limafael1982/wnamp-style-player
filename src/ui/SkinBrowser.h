#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

// Modeless skin picker: a list of skins found in `dirs` with a preview of the selected
// skin's main window. Double-click or Apply calls onChoose(path) so the caller can apply
// it live. Only one browser exists at a time; calling show() again brings it to front and
// refreshes its list and callback.
class SkinBrowser {
public:
    static void show(HWND owner, HINSTANCE hinst, const std::vector<std::wstring>& dirs,
                     const std::wstring& currentSkinPath,
                     std::function<void(const std::wstring&)> onChoose);
};
