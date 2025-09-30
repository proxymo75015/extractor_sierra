#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "robot_extractor.hpp"

using robot::RobotExtractor;
using robot::RobotExtractorTester;

#include <catch2/matchers/catch_matchers_string.hpp>

namespace fs = std::filesystem;

constexpr uint32_t kAudioBlockSize = 24;
constexpr uint32_t kAudioHeaderSize = 8;
constexpr int32_t kBlockPosition = 2;
constexpr uint32_t kBlockPayloadSize = 2;

static void push16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>(x >> 8));
}

static void push32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

static std::vector<uint8_t> build_header() {
  std::vector<uint8_t> h;
  push16(h, 0x16);            // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);               // version
  push16(h, kAudioBlockSize); // audio block size
  push16(h, 0);               // primerZeroCompressFlag
  push16(h, 0);               // skip
  push16(h, 1);               // numFrames
  push16(h, 0);               // paletteSize
  push16(h, 0);               // primerReservedSize
  push16(h, 1);               // xRes
  push16(h, 1);               // yRes
  h.push_back(0);             // hasPalette
  h.push_back(1);             // hasAudio
  push16(h, 0);               // skip
  push16(h, 60);              // frameRate
  push16(h, 0);               // isHiRes
  push16(h, 0);               // maxSkippablePackets
  push16(h, 1);               // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0);             // champs supplémentaires
  for (int i = 0; i < 2; ++i)
    push32(h, 0);             // zone réservée
  return h;
}

TEST_CASE("Robot with audio but no primer fails with flag error") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "missing_primer_audio.rbt";
  fs::path outDir = tmpDir / "missing_primer_audio_out";

  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  if (fs::exists(input)) {
    fs::remove(input);
  }  
  fs::create_directories(outDir);

  auto data = build_header();
  push16(data, 2);  // frame size
  push16(data, static_cast<uint16_t>(2 + kAudioBlockSize));
  for (int i = 0; i < 256; ++i)
    push32(data, 0);  // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0);  // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  data.push_back(0);  // numCels low byte
  data.push_back(0);  // numCels high byte

  push32(data, static_cast<uint32_t>(kBlockPosition));
  push32(data, kBlockPayloadSize);
  std::array<uint8_t, kBlockPayloadSize> payload{0x10, 0x32};
  data.insert(data.end(), payload.begin(), payload.end());
  const uint32_t padding = kAudioBlockSize - kAudioHeaderSize - kBlockPayloadSize;
  data.insert(data.end(), padding, 0);
  
  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  }

  RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(RobotExtractorTester::readHeader(extractor));
  REQUIRE_NOTHROW(RobotExtractorTester::readPrimer(extractor));

  std::vector<std::byte> dummyBlock(robot::kRobotRunwayBytes, std::byte{0});
  REQUIRE_THROWS_WITH(
      RobotExtractorTester::processAudioBlock(extractor, dummyBlock,
                                              kBlockPosition),
      Catch::Matchers::ContainsSubstring("Flags corrupt"));
}
