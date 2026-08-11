#pragma once

#include <stddef.h>
#include <stdint.h>

class LcdCamCapture {
 public:
  static constexpr size_t kCaptureBytes = 696;
  static constexpr size_t kFrameBytes = 24;

  bool begin(bool vsync_high, bool ce_as_vsync, bool invert_pclk);
  bool arm();
  bool takeCompleted(const uint8_t*& samples, size_t& length);
  const char* error() const { return error_; }

 private:
  bool configureHardware();
  void setError(const char* message) { error_ = message; }

  const char* error_ = "not initialized";
  size_t capture_bytes_ = kCaptureBytes;
  bool initialized_ = false;
};