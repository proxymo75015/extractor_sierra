#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

using robot::RobotExtractor;
using robot::RobotExtractorTester;

namespace fs = std::filesystem;

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

static std::vector<uint8_t> build_header(uint16_t primerSize) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);          // version
  push16(h, 8);          // audio block size
  push16(h, 0);          // primerZeroCompressFlag
  push16(h, 0);          // skip
  push16(h, 1);          // numFrames
  push16(h, 0);          // paletteSize
  push16(h, primerSize); // primerReservedSize
  push16(h, 1);          // xRes
  push16(h, 1);          // yRes
  h.push_back(0);        // hasPalette
  h.push_back(1);        // hasAudio
  push16(h, 0);          // skip
  push16(h, 60);         // frameRate
  push16(h, 0);          // isHiRes
  push16(h, 0);          // maxSkippablePackets
  push16(h, 1);          // maxCelsPerFrame
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // padding
  return h;
}

static std::vector<uint8_t>
build_primer_header(uint32_t total, uint32_t evenSize, uint32_t oddSize) {
  std::vector<uint8_t> p;
  push32(p, total);
  push16(p, 0); // compType
  push32(p, evenSize);
  push32(p, oddSize);
  return p;
}

TEST_CASE("Primer seek goes to reserved end") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "primer_seek.rbt";
  fs::path outDir = tmpDir / "primer_seek_out";
  fs::create_directories(outDir);

  auto data = build_header(22); // 14 header + 8 data
  auto primer = build_primer_header(8, 8, 0);
  data.insert(data.end(), primer.begin(), primer.end());
  data.insert(data.end(), 8, 0); // even primer data

  push16(data, 2); // frame size
  push16(data, 2); // packet size
  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  // Pad to 2048-byte boundary
  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  // Frame data: numCels = 0
  data.push_back(0);
  data.push_back(0);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  const auto &frames = RobotExtractorTester::frameSizes(extractor);
  REQUIRE(frames[0] != RobotExtractorTester::evenPrimer(extractor).size());
  REQUIRE(frames[0] != RobotExtractorTester::oddPrimer(extractor).size());
}
