#pragma once
#include <string>

namespace radio {

// A station with a fixed, directly-playable stream URL.
struct Station {
    std::wstring title;
    std::wstring url;
};

}  // namespace radio
