#include "net/SomaFm.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>

#include "net/Http.h"

namespace net {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Minimal JSON value (enough for channels.json).
struct J {
    enum T { Null, Str, Other, Arr, Obj } t = Null;
    std::string s;
    std::vector<J> a;
    std::map<std::string, J> o;
    const J* get(const char* k) const { auto it = o.find(k); return it == o.end() ? nullptr : &it->second; }
    std::string str(const char* k) const { auto* v = get(k); return v && v->t == Str ? v->s : std::string(); }
};

struct Parser {
    const std::string& d;
    size_t i = 0;
    int depth = 0;
    explicit Parser(const std::string& s) : d(s) {}
    void ws() { while (i < d.size() && (unsigned char)d[i] <= ' ') ++i; }

    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | cp >> 6); o += char(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += char(0xE0 | cp >> 12); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
        else { o += char(0xF0 | cp >> 18); o += char(0x80 | ((cp >> 12) & 63)); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
    }
    bool str(std::string& o) {
        if (i >= d.size() || d[i] != '"') return false;
        ++i;
        while (i < d.size() && d[i] != '"') {
            char c = d[i++];
            if (c != '\\') { o += c; continue; }
            if (i >= d.size()) return false;
            c = d[i++];
            switch (c) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    if (i + 4 > d.size()) return false;
                    unsigned cp = unsigned(strtoul(d.substr(i, 4).c_str(), nullptr, 16));
                    i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= d.size() && d[i] == '\\' && d[i + 1] == 'u') {
                        const unsigned lo = unsigned(strtoul(d.substr(i + 2, 4).c_str(), nullptr, 16));
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += c;
            }
        }
        if (i >= d.size()) return false;
        ++i;
        return true;
    }
    bool value(J& v) {
        if (++depth > 32) return false;
        struct G { int& x; ~G() { --x; } } g{depth};
        ws();
        if (i >= d.size()) return false;
        const char c = d[i];
        if (c == '"') { v.t = J::Str; return str(v.s); }
        if (c == '[') {
            v.t = J::Arr; ++i; ws();
            if (i < d.size() && d[i] == ']') { ++i; return true; }
            for (;;) {
                J e;
                if (!value(e)) return false;
                v.a.push_back(std::move(e));
                ws();
                if (i < d.size() && d[i] == ',') { ++i; continue; }
                if (i < d.size() && d[i] == ']') { ++i; return true; }
                return false;
            }
        }
        if (c == '{') {
            v.t = J::Obj; ++i; ws();
            if (i < d.size() && d[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= d.size() || d[i] != ':') return false;
                ++i;
                J e;
                if (!value(e)) return false;
                v.o[k] = std::move(e);
                ws();
                if (i < d.size() && d[i] == ',') { ++i; continue; }
                if (i < d.size() && d[i] == '}') { ++i; return true; }
                return false;
            }
        }
        v.t = J::Other;  // number / true / false / null
        while (i < d.size() && d[i] != ',' && d[i] != '}' && d[i] != ']' && (unsigned char)d[i] > ' ') ++i;
        return true;
    }
};

}  // namespace

bool fetchSomaChannels(std::vector<RadioChannel>& out) {
    out.clear();
    std::string body;
    if (!httpGet(L"https://somafm.com/channels.json", body)) return false;
    J root;
    Parser p(body);
    if (!p.value(root)) return false;
    const J* ch = root.get("channels");
    if (!ch || ch->t != J::Arr) return false;
    for (const J& c : ch->a) {
        RadioChannel r;
        r.id = widen(c.str("id"));
        r.title = widen(c.str("title"));
        r.description = widen(c.str("description"));
        r.genre = widen(c.str("genre"));
        const J* pls = c.get("playlists");
        std::wstring best, any;
        int bestRank = -1;
        if (pls && pls->t == J::Arr) {
            for (const J& e : pls->a) {
                const std::string url = e.str("url"), fmt = e.str("format"), q = e.str("quality");
                if (url.empty()) continue;
                if (any.empty()) any = widen(url);
                int rank = 0;
                if (fmt == "mp3") rank += 2;
                else if (fmt == "aac" || fmt == "aacp") rank += 1;
                if (q == "highest") rank += 2;
                else if (q == "high") rank += 1;
                if (rank > bestRank) { bestRank = rank; best = widen(url); }
            }
        }
        r.playlistUrl = best.empty() ? any : best;
        if (!r.title.empty() && !r.playlistUrl.empty()) out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(), [](const RadioChannel& a, const RadioChannel& b) { return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0; });
    return !out.empty();
}

bool resolveStreamUrl(const std::wstring& playlistUrl, std::wstring& streamUrl) {
    streamUrl.clear();
    const bool isPlaylist = playlistUrl.find(L".pls") != std::wstring::npos || playlistUrl.find(L".m3u") != std::wstring::npos;
    if (!isPlaylist) { streamUrl = playlistUrl; return true; }
    std::string body;
    if (!httpGet(playlistUrl, body)) return false;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t e = body.find('\n', pos);
        if (e == std::string::npos) e = body.size();
        std::string line = body.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("File", 0) == 0) {
            const size_t eq = line.find('=');
            if (eq != std::string::npos) { streamUrl = widen(line.substr(eq + 1)); break; }
        } else if (line.rfind("http", 0) == 0) {
            streamUrl = widen(line);
            break;
        }
    }
    return !streamUrl.empty();
}

}  // namespace net
