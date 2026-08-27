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

bool begin(uint8_t clockPin = 5, uint8_t dataPin = 6);
void process();

struct MetadataUpdate {
  std::array<char, kMaxStationNameLength + 1> station{};
  std::array<char, kMaxRadioTextLength + 1> scrollingText{};
  uint8_t pty = 0;
};

SubmitStatus submitMetadata(const char* station, const char* scrollingText,
                            uint8_t pty);
bool takeMetadataUpdate(MetadataUpdate& update);

} // namespace rds