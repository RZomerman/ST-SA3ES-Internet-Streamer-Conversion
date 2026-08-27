#pragma once

#include <cstddef>
#include <cstdint>

namespace sony_tuner {

constexpr size_t kMaximumStations = 16;
constexpr size_t kStationNameSize = 49;
constexpr size_t kStationUrlSize = 145;

struct StationMapEntry {
  int32_t rfKHz;
  char name[kStationNameSize];
  char streamUrl[kStationUrlSize];
};

bool stationForFrequency(int32_t rfKHz, StationMapEntry& station);
size_t stationCount();
bool stationAt(size_t index, StationMapEntry& station);
bool replaceStationMap(const StationMapEntry* stations, size_t count);

} // namespace sony_tuner