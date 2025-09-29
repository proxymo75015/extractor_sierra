#include <catch2/catch_test_macros.hpp>
#include <cstddef>
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

static std::vector<uint8_t> build_header(uint16_t primerReservedSize,
                                         uint16_t paletteSize,
                                         bool hasPalette) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);          // version
  push16(h, 8);          // audio block size
  push16(h, 0);          // primerZeroCompressFlag
  push16(h, 0);          // skip
  push16(h, 1);          // numFrames
  push16(h, paletteSize);
  push16(h, primerReservedSize);
  push16(h, 1);          // xRes
  push16(h, 1);          // yRes
  h.push_back(hasPalette ? 1 : 0);
  h.push_back(1);        // hasAudio
  push16(h, 0);          // skip
  push16(h, 60);         // frameRate
  push16(h, 0);          // isHiRes
  push16(h, 0);          // maxSkippablePackets
  push16(h, 1);          // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0); // additional fields
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // reserved
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

TEST_CASE("Primer reserved size matches channel sizes") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "primer_equal_sizes.rbt";
  fs::path outDir = tmpDir / "primer_equal_sizes_out";
  fs::create_directories(outDir);

  constexpr uint16_t kPaletteSize = 4;
  const std::vector<uint8_t> paletteData = {0x11, 0x22, 0x33, 0x44};
  REQUIRE(paletteData.size() == kPaletteSize);

  const uint16_t primerReservedSize =
      static_cast<uint16_t>(robot::kRobotRunwayBytes);

  auto data = build_header(primerReservedSize, kPaletteSize, true);
  auto primer = build_primer_header(kPrimerHeaderSize +
                                        robot::kRobotRunwayBytes,
                                    static_cast<uint32_t>(robot::kRobotRunwayBytes),
                                    0);
  data.insert(data.end(), primer.begin(), primer.end());
  data.insert(data.end(), robot::kRobotRunwayBytes, 0); // even primer data
  data.insert(data.end(), paletteData.begin(), paletteData.end());

  push16(data, 4); // frame size
  push16(data, 5); // packet size
  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);
  data.insert(data.end(), static_cast<size_t>(5), 0);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  RobotExtractor extractor(input, outDir, false);
  REQUIRE_NOTHROW(RobotExtractorTester::readHeader(extractor));
  REQUIRE_NOTHROW(RobotExtractorTester::readPrimer(extractor));

  const auto primerDataStart = RobotExtractorTester::primerPosition(extractor);
  const auto postPrimer = RobotExtractorTester::postPrimerPos(extractor);
  REQUIRE(postPrimer ==
          primerDataStart +
              static_cast<std::streamoff>(robot::kRobotRunwayBytes));

  REQUIRE_NOTHROW(RobotExtractorTester::readPalette(extractor));
  const auto &palette = RobotExtractorTester::palette(extractor);
  REQUIRE(palette.size() == paletteData.size());
  for (size_t i = 0; i < paletteData.size(); ++i) {
    REQUIRE(std::to_integer<uint8_t>(palette[i]) == paletteData[i]);
  }

  REQUIRE_NOTHROW(RobotExtractorTester::readSizesAndCues(extractor));
  const auto &frames = RobotExtractorTester::frameSizes(extractor);
  REQUIRE(frames.size() == 1);
  REQUIRE(frames[0] == 4);

  const auto &packets = RobotExtractorTester::packetSizes(extractor);
  REQUIRE(packets.size() == 1);
  REQUIRE(packets[0] == 5);
}
