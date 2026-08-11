#pragma once

#include <stdint.h>

enum class Band : uint8_t { UNKNOWN, FM, MW, LW };
enum class Antenna : uint8_t { UNKNOWN, A, B };
enum class FrameType : uint8_t { UNKNOWN, FREQUENCY, PERSISTENT_CONTROL, TRANSITION };

struct Frame {
  uint8_t bytes[3];
};

struct DecodedFrame {
  FrameType type;
  Band band;
  Antenna antenna;
  int32_t frequency_hz;
};

uint8_t reverseBits(uint8_t value);
DecodedFrame decodeFrame(const Frame& frame);
const char* bandName(Band band);
const char* antennaName(Antenna antenna);
const char* frameTypeName(FrameType type);