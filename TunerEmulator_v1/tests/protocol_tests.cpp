#include <assert.h>
#include <stdio.h>

#include "../protocol.h"
#include "../receiver.h"

void expectFrequency(uint8_t b0, uint8_t b1, uint8_t b2, Band band, int32_t frequency_hz) {
  const Frame frame{{b0, b1, b2}};
  const DecodedFrame decoded = decodeFrame(frame);
  assert(decoded.type == FrameType::FREQUENCY);
  assert(decoded.band == band);
  assert(decoded.frequency_hz == frequency_hz);
}

void expectControl(uint8_t b0, uint8_t b1, uint8_t b2, Band band, Antenna antenna) {
  const Frame frame{{b0, b1, b2}};
  const DecodedFrame decoded = decodeFrame(frame);
  assert(decoded.type == FrameType::PERSISTENT_CONTROL);
  assert(decoded.band == band);
  assert(decoded.antenna == antenna);
}

void testReceiverAssembly() {
  const Frame expected{{0x7B, 0xE0, 0x54}};
  FrameReceiver receiver;
  receiver.beginFrame();
  for (uint8_t byte_index = 0; byte_index < 3; ++byte_index) {
    for (uint8_t offset = 0; offset < 8; ++offset) {
      receiver.sampleFallingEdge((expected.bytes[byte_index] >> (7 - offset)) & 1);
    }
  }
  const CaptureResult complete = receiver.endFrame();
  assert(complete.complete && !complete.overflow);
  assert(complete.frame.bytes[0] == 0x7B && complete.frame.bytes[1] == 0xE0 && complete.frame.bytes[2] == 0x54);

  receiver.beginFrame();
  for (uint8_t bit = 0; bit < 23; ++bit) receiver.sampleFallingEdge(false);
  assert(!receiver.endFrame().complete);
  receiver.beginFrame();
  for (uint8_t bit = 0; bit < 25; ++bit) receiver.sampleFallingEdge(false);
  assert(receiver.endFrame().overflow);
}

int main() {
  assert(reverseBits(0x35) == 0xAC);
  expectFrequency(0x35, 0xE0, 0x54, Band::FM, 87500000);
  expectFrequency(0x7B, 0xE0, 0x54, Band::FM, 90000000);
  expectFrequency(0xE0, 0x10, 0x54, Band::FM, 92050000);
  expectFrequency(0x22, 0x90, 0x54, Band::FM, 107900000);
  expectFrequency(0x0B, 0x60, 0x19, Band::MW, 531000);
  expectFrequency(0x08, 0xE0, 0x19, Band::MW, 567000);
  expectFrequency(0x02, 0x10, 0x19, Band::MW, 738000);
  expectFrequency(0x01, 0xD4, 0x1D, Band::LW, 246000);
  expectFrequency(0x03, 0x34, 0x1D, Band::LW, 266000);
  expectControl(0xE3, 0x4C, 0x88, Band::FM, Antenna::A);
  expectControl(0xE3, 0xCC, 0x88, Band::FM, Antenna::B);
  expectControl(0xEF, 0x4D, 0x88, Band::MW, Antenna::UNKNOWN);
  expectControl(0xED, 0x4F, 0x88, Band::LW, Antenna::UNKNOWN);
  assert(decodeFrame(Frame{{0xEB, 0xCC, 0x88}}).type == FrameType::TRANSITION);
  assert(decodeFrame(Frame{{0xE3, 0xCC, 0x98}}).type == FrameType::TRANSITION);
  testReceiverAssembly();
  puts("protocol tests passed");
  return 0;
}