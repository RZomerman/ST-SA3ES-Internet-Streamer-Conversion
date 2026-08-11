#pragma once

#include <stdint.h>

#include "protocol.h"

struct TunerState {
  Band band = Band::UNKNOWN;
  Antenna antenna = Antenna::UNKNOWN;
  int32_t frequency_hz = 0;
  Frame last_frequency_frame{{0, 0, 0}};
  Frame last_control_frame{{0, 0, 0}};
  uint32_t valid_frames = 0;
  uint32_t incomplete_frames = 0;
  uint32_t overflow_frames = 0;
  uint32_t queue_overruns = 0;
  uint32_t last_command_ms = 0;
};

bool applyFrame(TunerState& state, const Frame& frame, uint32_t now_ms);