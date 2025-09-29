#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

using robot::RobotExtractor;
using robot::RobotExtractorTester;

namespace {

void push_le16(std::vector<uint8_t> &buffer, uint16_t value) {
  buffer.push_back(static_cast<uint8_t>(value & 0xFF));
  buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void push_be16(std::vector<uint8_t> &buffer, uint16_t value) {
  buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  buffer.push_back(static_cast<uint8_t>(value & 0xFF));
}

void push_be32(std::vector<uint8_t> &buffer, uint32_t value) {
  buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
  buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  buffer.push_back(static_cast<uint8_t>(value & 0xFF));
}

} // namespace

TEST_CASE("Robot big-endian header loads correctly") {
  namespace fs = std::filesystem;

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "robot_big_endian.rbt";
  fs::path outDir = tmpDir / "robot_big_endian_out";

  std::vector<uint8_t> header;
  push_le16(header, 0x0016);           // signature (little-endian)
  header.insert(header.end(), {'S', 'O', 'L', '\0'});
  push_be16(header, 5);                // version
  push_be16(header, 0);                // audio block size
  push_be16(header, 0);                // primerZeroCompressFlag
  push_be16(header, 0);                // reserved
  push_be16(header, 3);                // numFrames
  push_be16(header, 0);                // paletteSize
  push_be16(header, 0);                // primerReservedSize
  push_be16(header, 640);              // xRes
  push_be16(header, 480);              // yRes
  header.push_back(1);                 // hasPalette
  header.push_back(0);                 // hasAudio
  push_be16(header, 0);                // reserved
  push_be16(header, 60);               // frameRate
  push_be16(header, 1);                // isHiRes
  push_be16(header, 0);                // maxSkippablePackets
  push_be16(header, 2);                // maxCelsPerFrame
  for (int i = 0; i < 4; ++i) {
    push_be32(header, 0);              // fixed cel sizes
  }
  for (int i = 0; i < 2; ++i) {
    push_be32(header, 0);              // reserved header space
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
  REQUIRE_NOTHROW(RobotExtractorTester::readHeader(extractor));
  REQUIRE(RobotExtractorTester::postHeaderPos(extractor) ==
          static_cast<std::streamoff>(header.size()));
  REQUIRE(RobotExtractorTester::bigEndian(extractor));
  REQUIRE(RobotExtractorTester::numFrames(extractor) == 3);
  REQUIRE(RobotExtractorTester::xRes(extractor) == 640);
  REQUIRE(RobotExtractorTester::yRes(extractor) == 480);
  REQUIRE(RobotExtractorTester::hasPalette(extractor));
  REQUIRE(RobotExtractorTester::maxCelsPerFrame(extractor) == 2);
}
