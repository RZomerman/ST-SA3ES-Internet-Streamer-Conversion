#include "din_status.h"

void DinStatus::begin() {
  release();
}

void DinStatus::release() {
  digitalWrite(pin_, LOW);
  pinMode(pin_, INPUT);
  low_ = false;
}

void DinStatus::pullLowFor(uint32_t duration_ms) {
  digitalWrite(pin_, LOW);
  pinMode(pin_, OUTPUT);
  release_at_ms_ = millis() + duration_ms;
  low_ = true;
}

void DinStatus::service() {
  if (low_ && static_cast<int32_t>(millis() - release_at_ms_) >= 0) release();
}