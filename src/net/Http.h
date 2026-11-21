#pragma once
#include <string>

namespace net {
// Blocking HTTPS/HTTP GET through WinHTTP (call from a worker thread). Returns false on failure.
// Appends one line to wnamp_net.log next to the exe (diagnostics for network features).
void logNet(const std::wstring& line);
bool httpGet(const std::wstring& url, std::string& body);
}
