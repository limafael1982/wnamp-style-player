#include "net/Http.h"

#include <windows.h>
#include <winhttp.h>

#include <string>

namespace net {

void logNet(const std::wstring& line) {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring logPath = exePath;
    const size_t slashPos = logPath.find_last_of(L"\\/");
    logPath = (slashPos == std::wstring::npos ? L"" : logPath.substr(0, slashPos + 1)) + L"wnamp_net.log";
    HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER fileSize{};
    GetFileSizeEx(file, &fileSize);
    if (fileSize.QuadPart > 200000) { CloseHandle(file); DeleteFileW(logPath.c_str()); return; }
    const std::wstring lineWithNewline = line + L"\r\n";
    const int byteLen = WideCharToMultiByte(CP_UTF8, 0, lineWithNewline.data(), int(lineWithNewline.size()), nullptr,
                                             0, nullptr, nullptr);
    std::string utf8Line(size_t(byteLen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, lineWithNewline.data(), int(lineWithNewline.size()), utf8Line.data(), byteLen,
                         nullptr, nullptr);
    DWORD written = 0;
    WriteFile(file, utf8Line.data(), DWORD(utf8Line.size()), &written, nullptr);
    CloseHandle(file);
}

bool httpGet(const std::wstring& url, std::string& body) {
    body.clear();
    URL_COMPONENTS urlComponents{};
    urlComponents.dwStructSize = sizeof(urlComponents);
    wchar_t host[256], path[2048], extra[2048];
    urlComponents.lpszHostName = host;
    urlComponents.dwHostNameLength = 256;
    urlComponents.lpszUrlPath = path;
    urlComponents.dwUrlPathLength = 2048;
    urlComponents.lpszExtraInfo = extra;
    urlComponents.dwExtraInfoLength = 2048;  // "?query" part, must be sent too
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComponents)) { logNet(L"crack failed: " + url); return false; }

    HINTERNET session = WinHttpOpen(L"wnamp/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 15000);
    bool success = false;
    HINTERNET connection =
        WinHttpConnect(session, std::wstring(host, urlComponents.dwHostNameLength).c_str(), urlComponents.nPort, 0);
    HINTERNET request =
        connection ? WinHttpOpenRequest(
                         connection, L"GET",
                         (std::wstring(path, urlComponents.dwUrlPathLength) +
                          std::wstring(extra, urlComponents.dwExtraInfoLength))
                             .c_str(),
                         nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                         urlComponents.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                   : nullptr;
    DWORD status = 0;
    const bool sent = request &&
                       WinHttpSendRequest(request, L"Accept: application/json, */*\r\n", DWORD(-1), nullptr, 0, 0, 0) &&
                       WinHttpReceiveResponse(request, nullptr);
    if (!sent) logNet(L"request failed (WinHTTP error " + std::to_wstring(GetLastError()) + L"): " + url);
    if (sent) {
        DWORD headerSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status,
                             &headerSize, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) logNet(L"HTTP " + std::to_wstring(status) + L": " + url);
        if (status == 200) {
            success = true;
            for (;;) {
                DWORD availableBytes = 0;
                if (!WinHttpQueryDataAvailable(request, &availableBytes) || availableBytes == 0) break;
                const size_t previousSize = body.size();
                body.resize(previousSize + availableBytes);
                DWORD bytesRead = 0;
                if (!WinHttpReadData(request, body.data() + previousSize, availableBytes, &bytesRead)) {
                    success = false;
                    break;
                }
                body.resize(previousSize + bytesRead);
                if (body.size() > (8u << 20)) break;
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return success;
}

}  // namespace net
