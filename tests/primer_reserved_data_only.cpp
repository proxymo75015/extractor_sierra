#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

using robot::RobotExtractor;
using robot::RobotExtractorTester;

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);

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

static std::vector<uint8_t> build_header(uint16_t primerReservedSize) {
  std::vector<uint8_t> h;
  push16(h, 0x16);            // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);               // version
  push16(h, 8);               // audio block size
  push16(h, 0);               // primerZeroCompressFlag
  push16(h, 0);               // skip
  push16(h, 1);               // numFrames
  push16(h, 0);               // paletteSize
  push16(h, primerReservedSize);
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
    push32(h, 0);             // additional fields
  for (int i = 0; i < 2; ++i)
    push32(h, 0);             // reserved
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

TEST_CASE("Primer reserved size only covers channel data") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "primer_reserved_data_only.rbt";
  fs::path outDir = tmpDir / "primer_reserved_data_only_out";
  fs::create_directories(outDir);

  const uint32_t evenSize = robot::kRobotZeroCompressSize;
  const uint32_t oddSize = robot::kRobotZeroCompressSize;
  const uint32_t totalPrimer = kPrimerHeaderSize + evenSize + oddSize;

  auto data = build_header(static_cast<uint16_t>(evenSize + oddSize));
  auto primer = build_primer_header(totalPrimer, evenSize, oddSize);
  data.insert(data.end(), primer.begin(), primer.end());
  
  std::vector<uint8_t> evenPrimer(evenSize, 0);
  std::vector<uint8_t> oddPrimer(oddSize, 0);

  data.insert(data.end(), evenPrimer.begin(), evenPrimer.end());
  data.insert(data.end(), oddPrimer.begin(), oddPrimer.end());
  
  std::vector<uint8_t> tail;
  push16(tail, 2); // frame size
  push16(tail, 2); // packet size
  for (int i = 0; i < 256; ++i)
    push32(tail, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(tail, 0); // cue values

  data.insert(data.end(), tail.begin(), tail.end());
  
  data.resize(((data.size() + 2047) / 2048) * 2048, 0);
  data.push_back(0);
  data.push_back(0);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  RobotExtractor extractor(input, outDir, false);
  REQUIRE_NOTHROW(extractor.extract());

  const auto primerStart = RobotExtractorTester::postHeaderPos(extractor);
  const auto dataStart = RobotExtractorTester::primerPosition(extractor);
  const auto postPrimer = RobotExtractorTester::postPrimerPos(extractor);

  REQUIRE(dataStart ==
          primerStart + static_cast<std::streamoff>(kPrimerHeaderSize));
  REQUIRE(postPrimer ==
          dataStart + static_cast<std::streamoff>(evenSize + oddSize));
}
