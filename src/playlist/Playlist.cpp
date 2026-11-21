#include "playlist/Playlist.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <numeric>

namespace {
std::wstring lowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}
}  // namespace

void Playlist::add(const std::wstring& path, const std::wstring& title) {
    PlaylistEntry e;
    e.path = path;
    e.title = title.empty() ? std::filesystem::path(path).stem().wstring() : title;
    if (e.title.empty()) e.title = path;
    entries_.push_back(std::move(e));
}

void Playlist::addMany(const std::vector<std::wstring>& paths) {
    for (const auto& p : paths) add(p);
}

void Playlist::clear() {
    entries_.clear();
    current_ = -1;
}

void Playlist::removeAt(size_t index) {
    if (index >= entries_.size()) return;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    const int i = static_cast<int>(index);
    if (current_ == i) current_ = -1;
    else if (current_ > i) --current_;
}

void Playlist::setCurrent(int index) {
    current_ = (index >= 0 && index < static_cast<int>(entries_.size())) ? index : -1;
}

// Random index different from the current one (when there is more than one track).
int Playlist::randomOther() {
    const int n = static_cast<int>(entries_.size());
    if (n <= 1) return 0;
    std::uniform_int_distribution<int> dist(0, n - 2);
    int r = dist(rng_);
    if (current_ >= 0 && r >= current_) ++r;
    return r;
}

int Playlist::next(bool shuffle, bool repeat) {
    const int n = static_cast<int>(entries_.size());
    if (n == 0) return -1;
    if (shuffle) {
        if (n == 1) return (current_ == 0 && !repeat) ? -1 : 0;
        return randomOther();
    }
    const int idx = current_ + 1;  // current_ == -1 -> 0
    if (idx >= n) return repeat ? 0 : -1;
    return idx;
}

int Playlist::prev(bool shuffle) {
    const int n = static_cast<int>(entries_.size());
    if (n == 0) return -1;
    if (shuffle) return randomOther();
    return current_ > 0 ? current_ - 1 : 0;
}

void Playlist::setDuration(size_t index, double seconds) {
    if (index < entries_.size()) entries_[index].durationSec = seconds;
}

// Rebuilds the list in the given order and re-points current_ at the same track.
void Playlist::reorder(const std::vector<size_t>& perm) {
    std::vector<PlaylistEntry> out;
    out.reserve(entries_.size());
    int newCurrent = -1;
    for (size_t i = 0; i < perm.size(); ++i) {
        if (static_cast<int>(perm[i]) == current_) newCurrent = static_cast<int>(i);
        out.push_back(std::move(entries_[perm[i]]));
    }
    entries_ = std::move(out);
    current_ = newCurrent;
}

bool Playlist::moveSelected(std::set<int>& sel, int delta) {
    const int n = static_cast<int>(entries_.size());
    if (sel.empty() || delta == 0 || n < 2) return false;
    if (delta < 0 ? *sel.begin() <= 0 : *sel.rbegin() >= n - 1) return false;
    std::vector<size_t> perm(static_cast<size_t>(n));
    std::iota(perm.begin(), perm.end(), size_t{0});
    std::set<int> moved;
    if (delta < 0) {
        for (int i : sel) { std::swap(perm[size_t(i)], perm[size_t(i - 1)]); moved.insert(i - 1); }
    } else {
        for (auto it = sel.rbegin(); it != sel.rend(); ++it) {
            std::swap(perm[size_t(*it)], perm[size_t(*it + 1)]);
            moved.insert(*it + 1);
        }
    }
    reorder(perm);
    sel = std::move(moved);
    return true;
}

void Playlist::sortByTitle() {
    std::vector<size_t> perm(entries_.size());
    std::iota(perm.begin(), perm.end(), size_t{0});
    std::stable_sort(perm.begin(), perm.end(), [this](size_t a, size_t b) {
        return lowerW(entries_[a].title) < lowerW(entries_[b].title);
    });
    reorder(perm);
}

void Playlist::sortByPath() {
    std::vector<size_t> perm(entries_.size());
    std::iota(perm.begin(), perm.end(), size_t{0});
    std::stable_sort(perm.begin(), perm.end(), [this](size_t a, size_t b) {
        return lowerW(entries_[a].path) < lowerW(entries_[b].path);
    });
    reorder(perm);
}

void Playlist::sortByFilename() {
    std::vector<size_t> perm(entries_.size());
    std::iota(perm.begin(), perm.end(), size_t{0});
    auto name = [this](size_t i) {
        return lowerW(std::filesystem::path(entries_[i].path).filename().wstring());
    };
    std::stable_sort(perm.begin(), perm.end(), [&](size_t a, size_t b) { return name(a) < name(b); });
    reorder(perm);
}

void Playlist::reverse() {
    std::vector<size_t> perm(entries_.size());
    std::iota(perm.rbegin(), perm.rend(), size_t{0});
    reorder(perm);
}

void Playlist::randomize() {
    std::vector<size_t> perm(entries_.size());
    std::iota(perm.begin(), perm.end(), size_t{0});
    std::shuffle(perm.begin(), perm.end(), rng_);
    reorder(perm);
}

std::vector<std::wstring> Playlist::paths() const {
    std::vector<std::wstring> out;
    out.reserve(entries_.size());
    for (const auto& e : entries_) out.push_back(e.path);
    return out;
}
