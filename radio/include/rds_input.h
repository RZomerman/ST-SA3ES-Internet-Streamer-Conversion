#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rds {

constexpr std::size_t kMaxStationNameLength = 64;
constexpr std::size_t kMaxRadioTextLength = 64;

enum class SubmitStatus : uint8_t {
  Accepted,
  NullArgument,
  InvalidPty,
};

// Starts the RDS clock/data service. Uses hardware timer 1 so ESP32Radio-V2
// can retain timer 0 for its 100 ms service interrupt.
bool begin(uint8_t clockPin = 5, uint8_t dataPin = 6);

// Call frequently from the host application's loop().
void process();

struct MetadataUpdate {
  std::array<char, kMaxStationNameLength + 1> station{};
  std::array<char, kMaxRadioTextLength + 1> scrollingText{};
  uint8_t pty = 0;
};

// Thread-safe. Input is copied immediately; strings longer than the RDS
// limits are truncated and unsupported control characters become spaces.
SubmitStatus submitMetadata(const char* station, const char* scrollingText,
                            uint8_t pty);

// Called by the RDS service task to consume the newest submitted update.
bool takeMetadataUpdate(MetadataUpdate& update);

} // namespace rds