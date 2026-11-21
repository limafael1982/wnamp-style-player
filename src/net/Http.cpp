#include "net/Http.h"

#include <windows.h>
#include <winhttp.h>

#include <string>

namespace net {

void logNet(const std::wstring& line) {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p = exe;
    const size_t k = p.find_last_of(L"\\/");
    p = (k == std::wstring::npos ? L"" : p.substr(0, k + 1)) + L"wnamp_net.log";
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 200000) { CloseHandle(h); DeleteFileW(p.c_str()); return; }
    const std::wstring l = line + L"\r\n";
    const int n = WideCharToMultiByte(CP_UTF8, 0, l.data(), int(l.size()), nullptr, 0, nullptr, nullptr);
    std::string u(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, l.data(), int(l.size()), u.data(), n, nullptr, nullptr);
    DWORD w = 0;
    WriteFile(h, u.data(), DWORD(u.size()), &w, nullptr);
    CloseHandle(h);
}

bool httpGet(const std::wstring& url, std::string& body) {
    body.clear();
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[2048], extra[2048];
    uc.lpszHostName = host;  uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;  // "?query" part, must be sent too
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { logNet(L"crack failed: " + url); return false; }

    HINTERNET ses = WinHttpOpen(L"wnamp/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!ses) return false;
    WinHttpSetTimeouts(ses, 8000, 8000, 8000, 15000);
    bool ok = false;
    HINTERNET con = WinHttpConnect(ses, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", (std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength)).c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                        : nullptr;
    DWORD status = 0;
    const bool sent = req && WinHttpSendRequest(req, L"Accept: application/json, */*\r\n", DWORD(-1), nullptr, 0, 0, 0) &&
                      WinHttpReceiveResponse(req, nullptr);
    if (!sent) logNet(L"request failed (WinHTTP error " + std::to_wstring(GetLastError()) + L"): " + url);
    if (sent) {
        DWORD sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz,
                            WINHTTP_NO_HEADER_INDEX);
        if (status != 200) logNet(L"HTTP " + std::to_wstring(status) + L": " + url);
        if (status == 200) {
            ok = true;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                const size_t old = body.size();
                body.resize(old + avail);
                DWORD got = 0;
                if (!WinHttpReadData(req, body.data() + old, avail, &got)) { ok = false; break; }
                body.resize(old + got);
                if (body.size() > (8u << 20)) break;
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

}  // namespace net
