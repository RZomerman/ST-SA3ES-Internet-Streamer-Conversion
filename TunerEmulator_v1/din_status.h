#pragma once

#include <Arduino.h>

class DinStatus {
 public:
  explicit DinStatus(uint8_t pin) : pin_(pin) {}
  void begin();
  void release();
  void pullLowFor(uint32_t duration_ms);
  void service();

 private:
  uint8_t pin_;
  uint32_t release_at_ms_ = 0;
  bool low_ = false;
};