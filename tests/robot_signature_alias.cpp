#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "robot_extractor.hpp"

using robot::RobotExtractor;
using robot::RobotExtractorTester;

namespace {

void push16(std::vector<uint8_t> &buffer, uint16_t value) {
  buffer.push_back(static_cast<uint8_t>(value & 0xFF));
  buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void push32(std::vector<uint8_t> &buffer, uint32_t value) {
  buffer.push_back(static_cast<uint8_t>(value & 0xFF));
  buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

} // namespace

TEST_CASE("Robot signature 0x3D is rejected") {
  namespace fs = std::filesystem;

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "robot_signature_0x3d.rbt";
  fs::path outDir = tmpDir / "robot_signature_0x3d_out";

  std::vector<uint8_t> header;
  push16(header, 0x003d);            // signature alias
  header.insert(header.end(), {'S', 'O', 'L', '\0'});
  push16(header, 5);                 // version
  push16(header, 0);                 // audio block size
  push16(header, 0);                 // primerZeroCompressFlag
  push16(header, 0);                 // reserved
  push16(header, 1);                 // numFrames
  push16(header, 0);                 // paletteSize
  push16(header, 0);                 // primerReservedSize
  push16(header, 1);                 // xRes
  push16(header, 1);                 // yRes
  header.push_back(0);               // hasPalette
  header.push_back(0);               // hasAudio
  push16(header, 0);                 // reserved
  push16(header, 60);                // frameRate
  push16(header, 0);                 // isHiRes
  push16(header, 0);                 // maxSkippablePackets
  push16(header, 1);                 // maxCelsPerFrame
  for (int i = 0; i < 4; ++i) {
    push32(header, 0);               // extended fields
  }
  for (int i = 0; i < 8; ++i) {
    header.push_back(0);             // reserved padding
  }

  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(header.data()),
              static_cast<std::streamsize>(header.size()));
  }

  std::error_code ec;
  fs::remove_all(outDir, ec);
  fs::create_directories(outDir);

  RobotExtractor extractor(input, outDir, false);
  REQUIRE_THROWS_AS(RobotExtractorTester::readHeader(extractor),
                    std::runtime_error);
}
