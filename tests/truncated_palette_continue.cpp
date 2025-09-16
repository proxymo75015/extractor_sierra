#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;
using robot::RobotExtractorTester;

namespace {

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

} // namespace

TEST_CASE("Truncated palette no longer interrupts extraction") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "trunc_palette_continue.rbt";
  fs::path outDir = tmpDir / "trunc_palette_continue_out";
  fs::create_directories(outDir);

  std::vector<uint8_t> data;
  push16(data, 0x16);             // signature
  data.insert(data.end(), {'S', 'O', 'L', '\0'});
  push16(data, 5);                // version
  push16(data, 0);                // audio block size
  push16(data, 0);                // primerZeroCompressFlag
  push16(data, 0);                // skip
  push16(data, 1);                // numFrames
  push16(data, 6);                // paletteSize
  push16(data, 0);                // primerReservedSize
  push16(data, 1);                // xRes
  push16(data, 1);                // yRes
  data.push_back(1);              // hasPalette
  data.push_back(0);              // hasAudio
  push16(data, 0);                // skip
  push16(data, 60);               // frameRate
  push16(data, 0);                // isHiRes
  push16(data, 0);                // maxSkippablePackets
  push16(data, 1);                // maxCelsPerFrame
  for (int i = 0; i < 4; ++i) push32(data, 0); // additional fields
  for (int i = 0; i < 2; ++i) push32(data, 0); // reserved

  for (uint8_t i = 0; i < 6; ++i) {
    data.push_back(i);            // palette data
  }

  push16(data, 39);               // frame size
  push16(data, 39);               // packet size

  data.insert(data.end(), 1536, 0); // cue tables

  size_t pad = (2048 - (data.size() % 2048)) % 2048;
  data.insert(data.end(), pad, 0); // alignment padding

  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  }

  robot::RobotExtractor extractor(input, outDir, false);
  REQUIRE_NOTHROW(RobotExtractorTester::readHeader(extractor));
  REQUIRE_NOTHROW(RobotExtractorTester::readPrimer(extractor));
  REQUIRE_NOTHROW(RobotExtractorTester::readPalette(extractor));

  auto &file = RobotExtractorTester::file(extractor);
  std::streamoff expectedPos = file.tellg();
  REQUIRE(expectedPos > 0);

  // Simulate a palette shorter than advertised by rewinding the stream.
  file.seekg(expectedPos - 3, std::ios::beg);
  REQUIRE(file.tellg() == expectedPos - 3);

  REQUIRE_NOTHROW(RobotExtractorTester::readSizesAndCues(extractor));

  const auto &frames = RobotExtractorTester::frameSizes(extractor);
  REQUIRE(frames.size() == 1);
  REQUIRE(frames[0] == 39);

  const auto &packets = RobotExtractorTester::packetSizes(extractor);
  REQUIRE(packets.size() == 1);
  REQUIRE(packets[0] == 39);
}
