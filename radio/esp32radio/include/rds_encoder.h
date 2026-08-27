#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace rds {

constexpr std::size_t kBitsPerBlock = 26;
constexpr std::size_t kBlocksPerGroup = 4;
constexpr std::size_t kBitsPerGroup = 104;
constexpr uint16_t kGeneratorPolynomial = 0x05B9;

enum class BlockType : uint8_t { A, B, C, CPrime, D };

struct StationData {
  uint16_t pi = 0x83C7;
  uint8_t pty = 10;
  bool tp = true;
  bool ta = false;
  bool music = true;
  std::array<char, 8> ps{{'R','A','D','I','O','5','3','8'}};
  std::string radioText = "RADIO538";
};

struct RdsGroup {
  std::array<uint16_t, 4> words{};
  std::array<uint32_t, 4> blocks{};
  std::array<uint8_t, kBitsPerGroup> bits{};
};

uint16_t offsetWord(BlockType type);
uint16_t crcRemainder(uint32_t word26);
uint32_t encodeBlock(uint16_t informationWord, BlockType type);
RdsGroup encodeGroup(const std::array<uint16_t, 4>& words,
                     BlockType thirdBlockType = BlockType::C);

class Encoder {
 public:
  explicit Encoder(const StationData& station = StationData{});
  void setStation(const StationData& station);
  void setProgramService(const std::array<char, 8>& ps);
  void setRadioText(const std::string& text);
  void setPty(uint8_t pty);
  RdsGroup makeGroup0A(uint8_t segment);
  RdsGroup makeGroup2A(uint8_t segment);
  RdsGroup nextGroup();

 private:
  StationData station_;
  std::array<char, 64> paddedRt_{};
  bool textAB_ = true;
  uint8_t psSegment_ = 0;
  uint8_t rtSegment_ = 0;
  uint8_t scheduleIndex_ = 0;
  void rebuildPaddedRadioText();
};

} // namespace rds