#include "rds_input.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp32-hal-timer.h>
#include <string>

#include "rds_encoder.h"

namespace rds {
namespace {

constexpr uint8_t kTimerNumber = 1;
constexpr uint16_t kTimerDivider = 4;
constexpr uint64_t kHalfClockTicks = 8421;

gpio_num_t clockPin = GPIO_NUM_NC;
gpio_num_t dataPin = GPIO_NUM_NC;
Encoder encoder;
std::array<uint8_t, kBitsPerGroup> groupBuffers[2];
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t activeBuffer = 0;
volatile uint16_t bitIndex = 0;
volatile bool bufferReady[2] = {false, false};
volatile bool refillRequested[2] = {false, false};
volatile bool clockHigh = false;
hw_timer_t* rdsTimer = nullptr;
uint8_t completedPsFrames = 0;
std::size_t marqueePosition = 0;
bool marqueeAdvanceRequested = false;
std::string marqueeText = "Blog.AzureInfra.com   ";
bool marqueeEnabled = true;

void setProgramServiceWindow() {
  std::array<char, 8> window{};
  window.fill(' ');
  for (std::size_t offset = 0; offset < window.size(); ++offset) {
    const std::size_t position = marqueeEnabled
                                     ? (marqueePosition + offset) % marqueeText.size()
                                     : offset;
    if (position < marqueeText.size()) window[offset] = marqueeText[position];
  }
  encoder.setProgramService(window);
}

void applyMetadataUpdate(const MetadataUpdate& update) {
  marqueeText = update.station.data();
  marqueeEnabled = marqueeText.size() > 8;
  if (marqueeEnabled) marqueeText += "   ";
  marqueePosition = 0;
  completedPsFrames = 0;
  marqueeAdvanceRequested = false;
  setProgramServiceWindow();
  encoder.setRadioText(update.scrollingText.data());
  encoder.setPty(update.pty);
}

void fillGroupBuffer(uint8_t index) {
  const auto group = encoder.nextGroup();
  portENTER_CRITICAL(&bufferMux);
  groupBuffers[index] = group.bits;
  bufferReady[index] = true;
  refillRequested[index] = false;
  portEXIT_CRITICAL(&bufferMux);
}

void IRAM_ATTR advanceRdsData() {
  portENTER_CRITICAL_ISR(&bufferMux);
  ++bitIndex;
  if (bitIndex >= kBitsPerGroup) {
    const uint8_t completed = activeBuffer;
    const uint8_t next = activeBuffer ^ 1u;
    if (bufferReady[next]) {
      activeBuffer = next;
      bufferReady[next] = false;
      refillRequested[completed] = true;
      bitIndex = 0;
    } else {
      bitIndex = kBitsPerGroup - 1;
    }
  }
  gpio_set_level(dataPin, groupBuffers[activeBuffer][bitIndex]);
  portEXIT_CRITICAL_ISR(&bufferMux);
}

void IRAM_ATTR onRdsHalfClock() {
  if (clockHigh) {
    gpio_set_level(clockPin, 0);
    clockHigh = false;
    advanceRdsData();
  } else {
    gpio_set_level(clockPin, 1);
    clockHigh = true;
  }
}

void completedGroup(uint8_t index) {
  uint32_t blockB = 0;
  const std::size_t start = kBitsPerBlock;
  for (std::size_t offset = 0; offset < kBitsPerBlock; ++offset) {
    blockB = (blockB << 1) | groupBuffers[index][start + offset];
  }
  const uint16_t information = static_cast<uint16_t>(blockB >> 10);
  const uint8_t type = static_cast<uint8_t>((information >> 12) & 0x0F);
  const uint8_t address = static_cast<uint8_t>(information & 0x03);
  if (type == 0 && address == 3 && ++completedPsFrames >= 2) {
    completedPsFrames = 0;
    marqueeAdvanceRequested = true;
  }
}

} // namespace

bool begin(uint8_t clockPinNumber, uint8_t dataPinNumber) {
  if (!GPIO_IS_VALID_OUTPUT_GPIO(clockPinNumber) ||
      !GPIO_IS_VALID_OUTPUT_GPIO(dataPinNumber) ||
      clockPinNumber == dataPinNumber || rdsTimer != nullptr) {
    return false;
  }

  clockPin = static_cast<gpio_num_t>(clockPinNumber);
  dataPin = static_cast<gpio_num_t>(dataPinNumber);
  fillGroupBuffer(0);
  fillGroupBuffer(1);
  activeBuffer = 0;
  bitIndex = 0;
  bufferReady[0] = false;
  clockHigh = false;

  gpio_set_direction(clockPin, GPIO_MODE_OUTPUT);
  gpio_set_direction(dataPin, GPIO_MODE_OUTPUT);
  gpio_set_level(clockPin, 0);
  gpio_set_level(dataPin, groupBuffers[0][0]);

  rdsTimer = timerBegin(kTimerNumber, kTimerDivider, true);
  if (rdsTimer == nullptr) return false;
  timerAttachInterrupt(rdsTimer, &onRdsHalfClock, true);
  timerAlarmWrite(rdsTimer, kHalfClockTicks, true);
  timerAlarmEnable(rdsTimer);
  return true;
}

void process() {
  MetadataUpdate update;
  if (takeMetadataUpdate(update)) applyMetadataUpdate(update);

  for (uint8_t index = 0; index < 2; ++index) {
    if (!refillRequested[index]) continue;
    completedGroup(index);
    if (marqueeAdvanceRequested && marqueeEnabled) {
      marqueeAdvanceRequested = false;
      marqueePosition = (marqueePosition + 1) % marqueeText.size();
      setProgramServiceWindow();
    }
    fillGroupBuffer(index);
  }
}

} // namespace rds