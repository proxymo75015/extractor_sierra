#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;
using robot::RobotExtractorTester;

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
  for (int i = 0; i < 4; ++i)
    push32(h, 0); // champs supplémentaires  
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // zone réservée
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

TEST_CASE("readPrimer releases primer buffers") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "primer_memory.rbt";
  fs::path outDir = tmpDir / "primer_memory_out";
  fs::create_directories(outDir);

  uint32_t evenSize = 10000;
  uint32_t oddSize = 10000;
  uint32_t total = evenSize + oddSize;
  auto data = build_header(static_cast<uint16_t>(total + 14));
  auto primer = build_primer_header(total, evenSize, oddSize);
  data.insert(data.end(), primer.begin(), primer.end());
  data.resize(data.size() + total, 0); // primer data

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, false);
  RobotExtractorTester::readHeader(extractor);

  for (int i = 0; i < 3; ++i) {
    if (i > 0) {
      RobotExtractorTester::file(extractor).clear();
      constexpr std::streamoff primerHeaderSize =
          sizeof(uint32_t) + sizeof(int16_t) + sizeof(uint32_t) +
          sizeof(uint32_t);
      std::streamoff headerPos =
          RobotExtractorTester::primerPosition(extractor) - primerHeaderSize;
      RobotExtractorTester::file(extractor).seekg(headerPos, std::ios::beg);
    }
    RobotExtractorTester::readPrimer(extractor);
    REQUIRE(RobotExtractorTester::evenPrimer(extractor).size() == 0);
    REQUIRE(RobotExtractorTester::evenPrimer(extractor).capacity() == 0);
    REQUIRE(RobotExtractorTester::oddPrimer(extractor).size() == 0);
    REQUIRE(RobotExtractorTester::oddPrimer(extractor).capacity() == 0);
  }
}
