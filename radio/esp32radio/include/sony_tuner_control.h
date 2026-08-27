#pragma once

#include <cstdint>

#include "station_map.h"

namespace sony_tuner {

struct SonyTunerFrame {
  uint8_t raw[3]{};
  uint16_t divider = 0;
  uint8_t control = 0;
  int32_t rfKHz = 0;
  bool valid = false;
};

struct StationSelection {
  int32_t rfKHz = 0;
  StationMapEntry station{};
  bool mapped = false;
};

bool decodeSonyFmFrame(uint32_t wireBits, SonyTunerFrame& frame);
bool begin();
bool takeStationSelection(StationSelection& selection);
void setPlaybackActive(bool active);

} // namespace sony_tuner