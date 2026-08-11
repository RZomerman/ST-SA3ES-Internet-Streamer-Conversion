#pragma once

#include <stdint.h>

#include "protocol.h"

struct CaptureResult {
  Frame frame;
  uint8_t bit_count;
  bool complete;
  bool overflow;
};

class FrameReceiver {
 public:
  void beginFrame();
  void sampleFallingEdge(bool data_high);
  CaptureResult endFrame();

 private:
  Frame frame_{};
  uint8_t bit_count_ = 0;
  bool overflow_ = false;
};