#include <cassert>
#include <iostream>
#include <string>

#include "rds_encoder.h"

static uint16_t syndrome(uint32_t word) { return rds::crcRemainder(word); }

int main() {
  rds::Encoder encoder;
  std::string ps(8, '?');
  for (uint8_t segment = 0; segment < 4; ++segment) {
    const auto group = encoder.makeGroup0A(segment);
    assert(group.words[0] == 0x83C7);
    assert(syndrome(group.blocks[0]) == rds::offsetWord(rds::BlockType::A));
    assert(syndrome(group.blocks[1]) == rds::offsetWord(rds::BlockType::B));
    assert(syndrome(group.blocks[2]) == rds::offsetWord(rds::BlockType::C));
    assert(syndrome(group.blocks[3]) == rds::offsetWord(rds::BlockType::D));
    ps[segment * 2] = static_cast<char>(group.words[3] >> 8);
    ps[segment * 2 + 1] = static_cast<char>(group.words[3] & 0xFF);
  }
  assert(ps == "Blog.Azu");

  std::string rt;
  for (uint8_t segment = 0; segment < 16; ++segment) {
    const auto group = encoder.makeGroup2A(segment);
    rt.push_back(static_cast<char>(group.words[2] >> 8));
    rt.push_back(static_cast<char>(group.words[2] & 0xFF));
    rt.push_back(static_cast<char>(group.words[3] >> 8));
    rt.push_back(static_cast<char>(group.words[3] & 0xFF));
  }
  const std::string expectedRt = "Blog.AzureInfra.com\r";
  assert(rt.substr(0, expectedRt.size()) == expectedRt);
  for (std::size_t i = expectedRt.size(); i < rt.size(); ++i) {
    assert(rt[i] == ' ');
  }
  std::cout << "PASS: PI=83C7 PS=" << ps
            << " RT=Blog.AzureInfra.com\\r + spaces\n";
}
