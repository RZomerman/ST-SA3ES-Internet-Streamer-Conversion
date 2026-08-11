#include "tuner_state.h"

bool applyFrame(TunerState& state, const Frame& frame, uint32_t now_ms) {
  const DecodedFrame decoded = decodeFrame(frame);
  ++state.valid_frames;
  state.last_command_ms = now_ms;
  if (decoded.type == FrameType::UNKNOWN || decoded.type == FrameType::TRANSITION) return false;

  bool changed = false;
  if (decoded.type == FrameType::FREQUENCY) {
    changed = state.band != decoded.band || state.frequency_hz != decoded.frequency_hz;
    state.band = decoded.band;
    state.frequency_hz = decoded.frequency_hz;
    state.last_frequency_frame = frame;
  } else {
    changed = state.band != decoded.band ||
              (decoded.antenna != Antenna::UNKNOWN && state.antenna != decoded.antenna);
    state.band = decoded.band;
    if (decoded.antenna != Antenna::UNKNOWN) state.antenna = decoded.antenna;
    state.last_control_frame = frame;
  }
  return changed;
}