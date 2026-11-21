#include "playlist/Playlist.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <numeric>

namespace {
std::wstring lowerW(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}
}  // namespace

void Playlist::add(const std::wstring& path, const std::wstring& title) {
    PlaylistEntry entry;
    entry.path = path;
    entry.title = title.empty() ? std::filesystem::path(path).stem().wstring() : title;
    if (entry.title.empty()) entry.title = path;
    entries_.push_back(std::move(entry));
}

void Playlist::addMany(const std::vector<std::wstring>& paths) {
    for (const auto& path : paths) add(path);
}

void Playlist::clear() {
    entries_.clear();
    current_ = -1;
}

void Playlist::removeAt(size_t index) {
    if (index >= entries_.size()) return;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    const int removedIndex = static_cast<int>(index);
    if (current_ == removedIndex) current_ = -1;
    else if (current_ > removedIndex) --current_;
}

void Playlist::setCurrent(int index) {
    current_ = (index >= 0 && index < static_cast<int>(entries_.size())) ? index : -1;
}

// Random index different from the current one (when there is more than one track).
int Playlist::randomOther() {
    const int count = static_cast<int>(entries_.size());
    if (count <= 1) return 0;
    std::uniform_int_distribution<int> dist(0, count - 2);
    int pick = dist(rng_);
    if (current_ >= 0 && pick >= current_) ++pick;
    return pick;
}

int Playlist::next(bool shuffle, bool repeat) {
    const int count = static_cast<int>(entries_.size());
    if (count == 0) return -1;
    if (shuffle) {
        if (count == 1) return (current_ == 0 && !repeat) ? -1 : 0;
        return randomOther();
    }
    const int nextIndex = current_ + 1;  // current_ == -1 -> 0
    if (nextIndex >= count) return repeat ? 0 : -1;
    return nextIndex;
}

int Playlist::prev(bool shuffle) {
    const int count = static_cast<int>(entries_.size());
    if (count == 0) return -1;
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

bool Playlist::moveSelected(std::set<int>& selected, int delta) {
    const int count = static_cast<int>(entries_.size());
    if (selected.empty() || delta == 0 || count < 2) return false;
    if (delta < 0 ? *selected.begin() <= 0 : *selected.rbegin() >= count - 1) return false;
    std::vector<size_t> perm(static_cast<size_t>(count));
    std::iota(perm.begin(), perm.end(), size_t{0});
    std::set<int> moved;
    if (delta < 0) {
        for (int row : selected) {
            std::swap(perm[size_t(row)], perm[size_t(row - 1)]);
            moved.insert(row - 1);
        }
    } else {
        for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
            std::swap(perm[size_t(*it)], perm[size_t(*it + 1)]);
            moved.insert(*it + 1);
        }
    }
    reorder(perm);
    selected = std::move(moved);
    return true;
}

void Playlist::sortByTitle() {
    std::vector<size_t> perm(entries_.size());
    std::iota(perm.begin(), perm.end(), size_t{0});
    std::stable_sort(perm.begin(), perm.end(), [this](size_t lhs, size_t rhs) {
        return lowerW(entries_[lhs].title) < lowerW(entries_[rhs].title);
    });
    reorder(perm);
}

void Playlist::sortByPath() {
    std::vector<size_t> perm(entries_.size());
    std::iota(perm.begin(), perm.end(), size_t{0});
    std::stable_sort(perm.begin(), perm.end(), [this](size_t lhs, size_t rhs) {
        return lowerW(entries_[lhs].path) < lowerW(entries_[rhs].path);
    });
    reorder(perm);
}

void Playlist::sortByFilename() {
    std::vector<size_t> perm(entries_.size());
    std::iota(perm.begin(), perm.end(), size_t{0});
    auto filenameOf = [this](size_t index) {
        return lowerW(std::filesystem::path(entries_[index].path).filename().wstring());
    };
    std::stable_sort(perm.begin(), perm.end(),
                      [&](size_t lhs, size_t rhs) { return filenameOf(lhs) < filenameOf(rhs); });
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
    for (const auto& entry : entries_) out.push_back(entry.path);
    return out;
}
