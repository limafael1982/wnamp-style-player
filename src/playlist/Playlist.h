#pragma once
#include <random>
#include <set>
#include <string>
#include <vector>

struct PlaylistEntry {
    std::wstring path, title;
    double durationSec = -1;  // -1 = unknown
};

// Plain playlist model (UI thread only).
class Playlist {
public:
    void add(const std::wstring& path, const std::wstring& title = std::wstring());  // title defaults to the file name without extension
    void addMany(const std::vector<std::wstring>& paths);
    void clear();
    void removeAt(size_t index);  // keeps current() pointing at the same track

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const PlaylistEntry& at(size_t index) const { return entries_[index]; }
    const std::vector<PlaylistEntry>& entries() const { return entries_; }

    int current() const { return current_; }  // -1 = none
    void setCurrent(int index);               // out-of-range -> -1

    // These only compute the index; the caller starts it and calls setCurrent().
    int next(bool shuffle, bool repeat);  // -1 = end of list
    int prev(bool shuffle);

    void setDuration(size_t index, double seconds);

    // Reordering. All of these keep current() pointing at the same track.
    void sortByTitle();
    void sortByPath();
    void sortByFilename();
    void reverse();
    // Moves every selected row one step up (delta<0) or down (delta>0). Updates `sel` to the new
    // positions and keeps current() on the same track. Returns false if the block is already at the edge.
    bool moveSelected(std::set<int>& selected, int delta);
    void randomize();

    std::vector<std::wstring> paths() const;

private:
    int randomOther();
    void reorder(const std::vector<size_t>& perm);  // new[i] = old[perm[i]]

    std::vector<PlaylistEntry> entries_;
    int current_ = -1;
    std::mt19937 rng_{std::random_device{}()};
};
