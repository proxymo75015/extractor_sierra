#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

namespace {
constexpr uint16_t kNumFrames = 2;
constexpr uint32_t kAudioBlockSize = 24;
constexpr size_t kZeroPrefixBytes = robot::kRobotZeroCompressSize;
constexpr size_t kRunwaySamples = robot::kRobotRunwaySamples;
constexpr size_t kBlockDataBytes =
    static_cast<size_t>(kAudioBlockSize) - robot::kRobotAudioHeaderSize;

void push16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>(x >> 8));
}

void push32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

std::vector<uint8_t> build_header() {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);    // version
  push16(h, kAudioBlockSize); // audio block size
  push16(h, 0);    // primerZeroCompressFlag
  push16(h, 0);    // skip
  push16(h, kNumFrames); // numFrames
  push16(h, 0);    // paletteSize
  push16(h, 0);    // primerReservedSize (no primer)
  push16(h, 1);    // xRes
  push16(h, 1);    // yRes
  h.push_back(0);  // hasPalette
  h.push_back(1);  // hasAudio
  push16(h, 0);    // skip
  push16(h, 60);   // frameRate
  push16(h, 0);    // isHiRes
  push16(h, 0);    // maxSkippablePackets
  push16(h, 1);    // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0);  // additional fields
  for (int i = 0; i < 2; ++i)
    push32(h, 0);  // reserved zone
  return h;
}

std::vector<int16_t> decompress_truncated_block(const std::vector<uint8_t> &raw) {
  std::vector<std::byte> block(kZeroPrefixBytes + raw.size(), std::byte{0});
  for (size_t i = 0; i < raw.size(); ++i) {
    block[kZeroPrefixBytes + i] = std::byte{raw[i]};
  }
  int16_t predictor = 0;
  auto samples = robot::dpcm16_decompress(std::span(block), predictor);
  if (samples.size() <= kRunwaySamples) {
    return {};
  }
  return {samples.begin() + static_cast<std::ptrdiff_t>(kRunwaySamples),
          samples.end()};
}

int64_t compute_start_sample(int32_t pos, int64_t offsetHalf) {
  int64_t halfPos = static_cast<int64_t>(pos) - offsetHalf;
  int64_t startSampleSigned = 0;
  if (halfPos >= 0) {
    startSampleSigned = halfPos / 2;
  } else {
    startSampleSigned = (halfPos - 1) / 2;
  }
  return startSampleSigned;
}

struct BlockInfo {
  int32_t position = 0;
  bool even = false;
  std::vector<uint8_t> raw;
  std::vector<int16_t> samples;
};
} // namespace

TEST_CASE("Odd-start audio without primer respects parity") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "odd_start_noprimer.rbt";
  fs::path outDir = tmpDir / "odd_start_noprimer_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  auto data = build_header();

  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 2); // frame size without audio
  }
  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 26); // packet size includes audio block
  }

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  std::array<BlockInfo, 2> blocks;
  blocks[0].position = 3;
  blocks[0].even = false;
  blocks[0].raw = {0x21, 0x43, 0x65, 0x87, 0xA9};
  blocks[0].samples = decompress_truncated_block(blocks[0].raw);

  blocks[1].position = 6;
  blocks[1].even = true;
  blocks[1].raw = {0xBA, 0xDC, 0xFE, 0x10, 0x32};
  blocks[1].samples = decompress_truncated_block(blocks[1].raw);

  for (const auto &block : blocks) {
    data.push_back(0);
    data.push_back(0);
    push32(data, static_cast<uint32_t>(block.position));
    push32(data, static_cast<uint32_t>(block.raw.size()));
    data.insert(data.end(), block.raw.begin(), block.raw.end());
    REQUIRE(block.raw.size() <= kBlockDataBytes);
    const size_t padding = kBlockDataBytes - block.raw.size();
    data.insert(data.end(), padding, 0);
  }

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  const auto evenStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  const auto oddStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);

  REQUIRE_FALSE(oddStream.empty());
  REQUIRE_FALSE(evenStream.empty());

  const int64_t initialDoubled =
      static_cast<int64_t>(blocks.front().position) * 2;
  int64_t audioStartOffset = initialDoubled % 4;
  if (audioStartOffset < 0) {
    audioStartOffset += 4;
  }
  const int64_t offsetHalf = audioStartOffset / 2;

  for (const auto &block : blocks) {
    const auto &stream = block.even ? evenStream : oddStream;
    const auto &samples = block.samples;
    if (samples.empty()) {
      continue;
    }
    const int64_t startSampleSigned =
        compute_start_sample(block.position, offsetHalf);
    REQUIRE(startSampleSigned >= 0);
    const size_t startSample = static_cast<size_t>(startSampleSigned);
    REQUIRE(stream.size() >= startSample + samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
      CAPTURE(block.position);
      CAPTURE(startSample);
      CAPTURE(i);
      REQUIRE(stream[startSample + i] == samples[i]);
    }
  }
}
