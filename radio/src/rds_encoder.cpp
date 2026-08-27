#include "rds_encoder.h"

#include <algorithm>

namespace rds {

uint16_t offsetWord(BlockType type) {
  switch (type) {
    case BlockType::A: return 0x0FC;
    case BlockType::B: return 0x198;
    case BlockType::C: return 0x168;
    case BlockType::CPrime: return 0x350;
    case BlockType::D: return 0x1B4;
  }
  return 0;
}

uint16_t crcRemainder(uint32_t word26) {
  uint32_t reg = word26 & 0x03FFFFFFu;
  for (int bit = 25; bit >= 10; --bit) {
    if (reg & (1u << bit)) {
      reg ^= static_cast<uint32_t>(kGeneratorPolynomial) << (bit - 10);
    }
  }
  return static_cast<uint16_t>(reg & 0x03FFu);
}

uint32_t encodeBlock(uint16_t informationWord, BlockType type) {
  const uint32_t data = static_cast<uint32_t>(informationWord) << 10;
  const uint16_t checkword = crcRemainder(data) ^ offsetWord(type);
  return data | checkword;
}

RdsGroup encodeGroup(const std::array<uint16_t, 4>& words,
                     BlockType thirdBlockType) {
  RdsGroup result;
  result.words = words;
  const std::array<BlockType, 4> types{
      BlockType::A, BlockType::B, thirdBlockType, BlockType::D};

  std::size_t out = 0;
  for (std::size_t block = 0; block < 4; ++block) {
    result.blocks[block] = encodeBlock(words[block], types[block]);
    for (int bit = 25; bit >= 0; --bit) {
      result.bits[out++] = static_cast<uint8_t>(
          (result.blocks[block] >> bit) & 1u);
    }
  }
  return result;
}

Encoder::Encoder(const StationData& station) : station_(station) {
  rebuildPaddedRadioText();
}

void Encoder::setStation(const StationData& station) {
  station_ = station;
  psSegment_ = 0;
  rtSegment_ = 0;
  scheduleIndex_ = 0;
  textAB_ = !textAB_;
  rebuildPaddedRadioText();
}

void Encoder::setProgramService(const std::array<char, 8>& ps) {
  station_.ps = ps;
  psSegment_ = 0;
}

void Encoder::setRadioText(const std::string& text) {
  if (text == station_.radioText) return;
  station_.radioText = text;
  textAB_ = !textAB_;
  rtSegment_ = 0;
  rebuildPaddedRadioText();
}

void Encoder::setPty(uint8_t pty) {
  station_.pty = pty & 0x1F;
}

void Encoder::rebuildPaddedRadioText() {
  paddedRt_.fill(' ');
  // Group 2A supports 64 characters. Follow the RADIO538 capture by inserting
  // CR after meaningful text when space remains, then pad the rest with spaces.
  const std::size_t n = std::min<std::size_t>(station_.radioText.size(), 64);
  std::copy_n(station_.radioText.begin(), n, paddedRt_.begin());
  if (n < paddedRt_.size()) paddedRt_[n] = '\r';
}

RdsGroup Encoder::makeGroup0A(uint8_t segment) {
  segment &= 0x03;
  // B: type 0A, TP, PTY, TA, M/S, DI and two-bit PS address.
  // DI is set on address 3 to match the verified RADIO538 capture (054F).
  uint16_t b = 0;
  b |= static_cast<uint16_t>(station_.tp) << 10;
  b |= static_cast<uint16_t>(station_.pty & 0x1F) << 5;
  b |= static_cast<uint16_t>(station_.ta) << 4;
  b |= static_cast<uint16_t>(station_.music) << 3;
  if (segment == 3) b |= 1u << 2;
  b |= segment;

  // First verified RADIO538 AF words. They are retained as a replay-compatible
  // baseline; AF handling can later become station-specific.
  static constexpr std::array<uint16_t, 4> kRadio538Af{
      0x8F90, 0x9293, 0x9495, 0x9697};
  const uint16_t d =
      (static_cast<uint16_t>(static_cast<uint8_t>(station_.ps[segment * 2])) << 8) |
      static_cast<uint8_t>(station_.ps[segment * 2 + 1]);
  return encodeGroup({station_.pi, b, kRadio538Af[segment], d});
}

RdsGroup Encoder::makeGroup2A(uint8_t segment) {
  segment &= 0x0F;
  uint16_t b = 0x2000; // group type 2A
  b |= static_cast<uint16_t>(station_.tp) << 10;
  b |= static_cast<uint16_t>(station_.pty & 0x1F) << 5;
  b |= static_cast<uint16_t>(textAB_) << 4;
  b |= segment;

  const std::size_t offset = segment * 4;
  const uint16_t c =
      (static_cast<uint16_t>(static_cast<uint8_t>(paddedRt_[offset])) << 8) |
      static_cast<uint8_t>(paddedRt_[offset + 1]);
  const uint16_t d =
      (static_cast<uint16_t>(static_cast<uint8_t>(paddedRt_[offset + 2])) << 8) |
      static_cast<uint8_t>(paddedRt_[offset + 3]);
  return encodeGroup({station_.pi, b, c, d});
}

RdsGroup Encoder::nextGroup() {
  // Start-up scheduler: 5 x 0A and 3 x 2A per eight groups. This approximates
  // the observed RADIO538 service mix after omitting the periodic 3A service.
  static constexpr std::array<uint8_t, 8> kSchedule{
      0, 2, 0, 0, 2, 0, 2, 0};
  const uint8_t type = kSchedule[scheduleIndex_++ & 0x07];
  if (type == 0) {
    const auto group = makeGroup0A(psSegment_);
    psSegment_ = (psSegment_ + 1) & 0x03;
    return group;
  }
  const auto group = makeGroup2A(rtSegment_);
  rtSegment_ = (rtSegment_ + 1) & 0x0F;
  return group;
}

} // namespace rds
