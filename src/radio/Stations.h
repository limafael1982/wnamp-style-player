#pragma once
#include <vector>

#include "radio/Station.h"

namespace radio {

// Built-in stations. To add one, add a line here — nothing else in the codebase needs
// to change (RadioMenu lists whatever this returns).
inline const std::vector<Station>& builtinStations() {
    static const std::vector<Station> kStations = {
        {L"Jumbo Radio", L"https://backend.airplays.fm/api/v1/radio_stations/17/stream_proxy"},
        {L"Tempo FM", L"https://radio.webfrequencia.com.br:8110/tempofm"},
    };
    return kStations;
}

}  // namespace radio
