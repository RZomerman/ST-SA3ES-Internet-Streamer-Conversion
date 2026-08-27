#include "station_map.h"

#include <Arduino.h>
#include <cstring>

namespace sony_tuner {
namespace {

StationMapEntry stationMap[kMaximumStations] = {
    {90000, "RADIO538",
     "playerservices.streamtheworld.com:80/api/livestream-redirect/RADIO538AAC.aac"},
    {91000, "BNR Nieuwsradio", "stream.bnr.nl:80/bnr_aac_96_20"},
};
size_t currentStationCount = 2;
portMUX_TYPE stationMapMux = portMUX_INITIALIZER_UNLOCKED;

} // namespace

bool stationForFrequency(int32_t rfKHz, StationMapEntry& station) {
  bool found = false;
  portENTER_CRITICAL(&stationMapMux);
  for (size_t index = 0; index < currentStationCount; ++index) {
    if (stationMap[index].rfKHz == rfKHz) {
      station = stationMap[index];
      found = true;
      break;
    }
  }
  portEXIT_CRITICAL(&stationMapMux);
  return found;
}

size_t stationCount() {
  portENTER_CRITICAL(&stationMapMux);
  const size_t count = currentStationCount;
  portEXIT_CRITICAL(&stationMapMux);
  return count;
}

bool stationAt(size_t index, StationMapEntry& station) {
  portENTER_CRITICAL(&stationMapMux);
  const bool found = index < currentStationCount;
  if (found) station = stationMap[index];
  portEXIT_CRITICAL(&stationMapMux);
  return found;
}

bool replaceStationMap(const StationMapEntry* stations, size_t count) {
  if (stations == nullptr || count > kMaximumStations) return false;
  for (size_t index = 0; index < count; ++index) {
    if (stations[index].rfKHz < 76000 || stations[index].rfKHz > 108000 ||
        stations[index].rfKHz % 50 != 0 || stations[index].name[0] == '\0' ||
        stations[index].streamUrl[0] == '\0') {
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (stations[previous].rfKHz == stations[index].rfKHz) return false;
    }
  }

  portENTER_CRITICAL(&stationMapMux);
  memcpy(stationMap, stations, count * sizeof(StationMapEntry));
  currentStationCount = count;
  portEXIT_CRITICAL(&stationMapMux);
  return true;
}

} // namespace sony_tuner