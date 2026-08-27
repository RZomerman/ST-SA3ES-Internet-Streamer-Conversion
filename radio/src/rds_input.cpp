#include "rds_input.h"

#include <Arduino.h>

namespace rds {
namespace {

portMUX_TYPE inputMux = portMUX_INITIALIZER_UNLOCKED;
MetadataUpdate pendingUpdate;
bool updatePending = false;

template <std::size_t Size>
void copyRdsText(std::array<char, Size>& destination, const char* source) {
  std::size_t index = 0;
  for (; index + 1 < Size && source[index] != '\0'; ++index) {
    const unsigned char value = static_cast<unsigned char>(source[index]);
    destination[index] = value >= 0x20 && value <= 0x7E
                             ? static_cast<char>(value)
                             : ' ';
  }
  destination[index] = '\0';
}

} // namespace

SubmitStatus submitMetadata(const char* station, const char* scrollingText,
                            uint8_t pty) {
  if (station == nullptr || scrollingText == nullptr) {
    return SubmitStatus::NullArgument;
  }
  if (pty > 31) return SubmitStatus::InvalidPty;

  MetadataUpdate update;
  copyRdsText(update.station, station);
  copyRdsText(update.scrollingText, scrollingText);
  update.pty = pty;

  portENTER_CRITICAL(&inputMux);
  pendingUpdate = update;
  updatePending = true;
  portEXIT_CRITICAL(&inputMux);
  return SubmitStatus::Accepted;
}

bool takeMetadataUpdate(MetadataUpdate& update) {
  portENTER_CRITICAL(&inputMux);
  const bool available = updatePending;
  if (available) {
    update = pendingUpdate;
    updatePending = false;
  }
  portEXIT_CRITICAL(&inputMux);
  return available;
}

} // namespace rds