#include "net/SomaFm.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>

#include "net/Http.h"

namespace net {
namespace {

std::wstring widen(const std::string& narrow) {
    if (narrow.empty()) return {};
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, narrow.data(), int(narrow.size()), nullptr, 0);
    std::wstring wide(size_t(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow.data(), int(narrow.size()), wide.data(), wideLen);
    return wide;
}

// Minimal JSON value (enough for channels.json).
struct JsonValue {
    enum Kind { Null, String, Other, Array, Object } kind = Null;
    std::string text;
    std::vector<JsonValue> items;
    std::map<std::string, JsonValue> fields;
    const JsonValue* find(const char* key) const {
        auto it = fields.find(key);
        return it == fields.end() ? nullptr : &it->second;
    }
    std::string getString(const char* key) const {
        const JsonValue* value = find(key);
        return value && value->kind == String ? value->text : std::string();
    }
};

struct JsonParser {
    const std::string& json;
    size_t pos = 0;
    int depth = 0;
    explicit JsonParser(const std::string& text) : json(text) {}
    void skipWhitespace() { while (pos < json.size() && (unsigned char)json[pos] <= ' ') ++pos; }

    static void appendUtf8(std::string& out, unsigned codepoint) {
        if (codepoint < 0x80) {
            out += char(codepoint);
        } else if (codepoint < 0x800) {
            out += char(0xC0 | codepoint >> 6);
            out += char(0x80 | (codepoint & 63));
        } else if (codepoint < 0x10000) {
            out += char(0xE0 | codepoint >> 12);
            out += char(0x80 | ((codepoint >> 6) & 63));
            out += char(0x80 | (codepoint & 63));
        } else {
            out += char(0xF0 | codepoint >> 18);
            out += char(0x80 | ((codepoint >> 12) & 63));
            out += char(0x80 | ((codepoint >> 6) & 63));
            out += char(0x80 | (codepoint & 63));
        }
    }

    bool parseString(std::string& out) {
        if (pos >= json.size() || json[pos] != '"') return false;
        ++pos;
        while (pos < json.size() && json[pos] != '"') {
            char ch = json[pos++];
            if (ch != '\\') { out += ch; continue; }
            if (pos >= json.size()) return false;
            ch = json[pos++];
            switch (ch) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (pos + 4 > json.size()) return false;
                    unsigned codepoint = unsigned(strtoul(json.substr(pos, 4).c_str(), nullptr, 16));
                    pos += 4;
                    if (codepoint >= 0xD800 && codepoint < 0xDC00 && pos + 6 <= json.size() && json[pos] == '\\' &&
                        json[pos + 1] == 'u') {
                        const unsigned low = unsigned(strtoul(json.substr(pos + 2, 4).c_str(), nullptr, 16));
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        pos += 6;
                    }
                    appendUtf8(out, codepoint);
                    break;
                }
                default: out += ch;
            }
        }
        if (pos >= json.size()) return false;
        ++pos;
        return true;
    }

    bool parseValue(JsonValue& result) {
        if (++depth > 32) return false;
        struct DepthGuard {
            int& depth;
            ~DepthGuard() { --depth; }
        } guard{depth};
        skipWhitespace();
        if (pos >= json.size()) return false;
        const char ch = json[pos];
        if (ch == '"') { result.kind = JsonValue::String; return parseString(result.text); }
        if (ch == '[') {
            result.kind = JsonValue::Array;
            ++pos;
            skipWhitespace();
            if (pos < json.size() && json[pos] == ']') { ++pos; return true; }
            for (;;) {
                JsonValue element;
                if (!parseValue(element)) return false;
                result.items.push_back(std::move(element));
                skipWhitespace();
                if (pos < json.size() && json[pos] == ',') { ++pos; continue; }
                if (pos < json.size() && json[pos] == ']') { ++pos; return true; }
                return false;
            }
        }
        if (ch == '{') {
            result.kind = JsonValue::Object;
            ++pos;
            skipWhitespace();
            if (pos < json.size() && json[pos] == '}') { ++pos; return true; }
            for (;;) {
                skipWhitespace();
                std::string key;
                if (!parseString(key)) return false;
                skipWhitespace();
                if (pos >= json.size() || json[pos] != ':') return false;
                ++pos;
                JsonValue fieldValue;
                if (!parseValue(fieldValue)) return false;
                result.fields[key] = std::move(fieldValue);
                skipWhitespace();
                if (pos < json.size() && json[pos] == ',') { ++pos; continue; }
                if (pos < json.size() && json[pos] == '}') { ++pos; return true; }
                return false;
            }
        }
        result.kind = JsonValue::Other;  // number / true / false / null
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
               (unsigned char)json[pos] > ' ')
            ++pos;
        return true;
    }
};

}  // namespace

bool fetchSomaChannels(std::vector<RadioChannel>& out) {
    out.clear();
    std::string body;
    if (!httpGet(L"https://somafm.com/channels.json", body)) return false;
    JsonValue root;
    JsonParser parser(body);
    if (!parser.parseValue(root)) return false;
    const JsonValue* channelsNode = root.find("channels");
    if (!channelsNode || channelsNode->kind != JsonValue::Array) return false;
    for (const JsonValue& channelNode : channelsNode->items) {
        RadioChannel channel;
        channel.id = widen(channelNode.getString("id"));
        channel.title = widen(channelNode.getString("title"));
        channel.description = widen(channelNode.getString("description"));
        channel.genre = widen(channelNode.getString("genre"));
        const JsonValue* playlistsNode = channelNode.find("playlists");
        std::wstring bestUrl, anyUrl;
        int bestRank = -1;
        if (playlistsNode && playlistsNode->kind == JsonValue::Array) {
            for (const JsonValue& playlistNode : playlistsNode->items) {
                const std::string url = playlistNode.getString("url");
                const std::string format = playlistNode.getString("format");
                const std::string quality = playlistNode.getString("quality");
                if (url.empty()) continue;
                if (anyUrl.empty()) anyUrl = widen(url);
                int rank = 0;
                if (format == "mp3") rank += 2;
                else if (format == "aac" || format == "aacp") rank += 1;
                if (quality == "highest") rank += 2;
                else if (quality == "high") rank += 1;
                if (rank > bestRank) { bestRank = rank; bestUrl = widen(url); }
            }
        }
        channel.playlistUrl = bestUrl.empty() ? anyUrl : bestUrl;
        if (!channel.title.empty() && !channel.playlistUrl.empty()) out.push_back(std::move(channel));
    }
    std::sort(out.begin(), out.end(), [](const RadioChannel& lhs, const RadioChannel& rhs) {
        return _wcsicmp(lhs.title.c_str(), rhs.title.c_str()) < 0;
    });
    return !out.empty();
}

bool resolveStreamUrl(const std::wstring& playlistUrl, std::wstring& streamUrl) {
    streamUrl.clear();
    const bool isPlaylist =
        playlistUrl.find(L".pls") != std::wstring::npos || playlistUrl.find(L".m3u") != std::wstring::npos;
    if (!isPlaylist) { streamUrl = playlistUrl; return true; }
    std::string body;
    if (!httpGet(playlistUrl, body)) return false;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t lineEnd = body.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = body.size();
        std::string line = body.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("File", 0) == 0) {
            const size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) { streamUrl = widen(line.substr(eqPos + 1)); break; }
        } else if (line.rfind("http", 0) == 0) {
            streamUrl = widen(line);
            break;
        }
    }
    return !streamUrl.empty();
}

}  // namespace net
