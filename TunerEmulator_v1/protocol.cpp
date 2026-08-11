#include "protocol.h"

namespace {
uint16_t pllNumber(const Frame& frame) {
  return static_cast<uint16_t>(reverseBits(frame.bytes[0])) |
         (static_cast<uint16_t>(reverseBits(frame.bytes[1])) << 8);
}

DecodedFrame unknownFrame() {
  return {FrameType::UNKNOWN, Band::UNKNOWN, Antenna::UNKNOWN, 0};
}
}  // namespace

uint8_t reverseBits(uint8_t value) {
  value = ((value & 0xF0) >> 4) | ((value & 0x0F) << 4);
  value = ((value & 0xCC) >> 2) | ((value & 0x33) << 2);
  return ((value & 0xAA) >> 1) | ((value & 0x55) << 1);
}

DecodedFrame decodeFrame(const Frame& frame) {
  const uint16_t number = pllNumber(frame);
  if (frame.bytes[2] == 0x54) {
    const int32_t frequency_hz = static_cast<int32_t>(number) * 50000 - 10700000;
    if (frequency_hz >= 76000000 && frequency_hz <= 108000000) {
      return {FrameType::FREQUENCY, Band::FM, Antenna::UNKNOWN, frequency_hz};
    }
    return unknownFrame();
  }
  if (frame.bytes[2] == 0x19) {
    // One PLL count is 562.5 Hz; round the half-Hz result to integer Hz.
    const int32_t frequency_hz = (static_cast<int32_t>(number) * 1125 + 1) / 2 - 450000;
    return {FrameType::FREQUENCY, Band::MW, Antenna::UNKNOWN, frequency_hz};
  }
  if (frame.bytes[2] == 0x1D) {
    // One PLL count is 62.5 Hz; round the half-Hz result to integer Hz.
    const int32_t frequency_hz = (static_cast<int32_t>(number) * 125 + 1) / 2 - 450000;
    return {FrameType::FREQUENCY, Band::LW, Antenna::UNKNOWN, frequency_hz};
  }

  if (frame.bytes[0] == 0xE3 && frame.bytes[2] == 0x88) {
    if (frame.bytes[1] == 0x4C) return {FrameType::PERSISTENT_CONTROL, Band::FM, Antenna::A, 0};
    if (frame.bytes[1] == 0xCC) return {FrameType::PERSISTENT_CONTROL, Band::FM, Antenna::B, 0};
    if (frame.bytes[1] == 0x4D) return {FrameType::PERSISTENT_CONTROL, Band::MW, Antenna::UNKNOWN, 0};
    if (frame.bytes[1] == 0x4F) return {FrameType::PERSISTENT_CONTROL, Band::LW, Antenna::UNKNOWN, 0};
  }
  if (frame.bytes[0] == 0xEF && frame.bytes[1] == 0x4D && frame.bytes[2] == 0x88) {
    return {FrameType::PERSISTENT_CONTROL, Band::MW, Antenna::UNKNOWN, 0};
  }
  if (frame.bytes[0] == 0xED && frame.bytes[1] == 0x4F && frame.bytes[2] == 0x88) {
    return {FrameType::PERSISTENT_CONTROL, Band::LW, Antenna::UNKNOWN, 0};
  }
  if (frame.bytes[0] == 0xEB || frame.bytes[2] == 0x98) {
    return {FrameType::TRANSITION, Band::UNKNOWN, Antenna::UNKNOWN, 0};
  }
  return unknownFrame();
}

const char* bandName(Band band) {
  switch (band) {
    case Band::FM: return "FM";
    case Band::MW: return "MW";
    case Band::LW: return "LW";
    default: return "UNKNOWN";
  }
}

const char* antennaName(Antenna antenna) {
  switch (antenna) {
    case Antenna::A: return "A";
    case Antenna::B: return "B";
    default: return "UNKNOWN";
  }
}

const char* frameTypeName(FrameType type) {
  switch (type) {
    case FrameType::FREQUENCY: return "frequency";
    case FrameType::PERSISTENT_CONTROL: return "persistent";
    case FrameType::TRANSITION: return "transition";
    default: return "unknown";
  }
}