#include "receiver.h"

void FrameReceiver::beginFrame() {
  frame_ = {{0, 0, 0}};
  bit_count_ = 0;
  overflow_ = false;
}

void FrameReceiver::sampleFallingEdge(bool data_high) {
  if (bit_count_ < 24) {
    if (data_high) {
      const uint8_t byte_index = bit_count_ / 8;
      const uint8_t offset = bit_count_ % 8;
      frame_.bytes[byte_index] |= static_cast<uint8_t>(1U << (7 - offset));
    }
    ++bit_count_;
    return;
  }
  overflow_ = true;
}

CaptureResult FrameReceiver::endFrame() {
  return {frame_, bit_count_, bit_count_ == 24 && !overflow_, overflow_};
}