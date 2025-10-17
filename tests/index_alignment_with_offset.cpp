#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

using robot::RobotExtractor;
using robot::RobotExtractorTester;

namespace fs = std::filesystem;

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

std::vector<uint8_t> build_header() {
  std::vector<uint8_t> h;
  push16(h, 0x16);            // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);               // version
  push16(h, 0);               // audio block size
  push16(h, 0);               // primerZeroCompressFlag
  push16(h, 0);               // skip
  push16(h, 1);               // numFrames
  push16(h, 0);               // paletteSize
  push16(h, 0);               // primerReservedSize
  push16(h, 1);               // xRes
  push16(h, 1);               // yRes
  h.push_back(0);             // hasPalette
  h.push_back(0);             // hasAudio
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

} // namespace

TEST_CASE("Index tables align relative to file offset") {
  constexpr uint16_t kFrameSize = 2;
  constexpr uint16_t kPacketSize = 2;
  constexpr size_t kPadding = 123; // deliberately not aligned to 2048

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "alignment_with_offset.rbt";
  fs::path outDir = tmpDir / "alignment_with_offset_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  auto robotData = build_header();

  push16(robotData, kFrameSize);
  push16(robotData, kPacketSize);

  for (int i = 0; i < 256; ++i)
    push32(robotData, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(robotData, 0); // cue values

  const size_t indexEnd = robotData.size();
  const size_t alignedIndexEnd = ((indexEnd + 2047) / 2048) * 2048;
  robotData.resize(alignedIndexEnd, 0);

  const std::array<uint8_t, kFrameSize> framePayload{0xAB, 0xCD};
  robotData.insert(robotData.end(), framePayload.begin(), framePayload.end());

  std::vector<uint8_t> container(kPadding, 0xEE);
  container.insert(container.end(), robotData.begin(), robotData.end());

  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(container.data()),
              static_cast<std::streamsize>(container.size()));
  }

  RobotExtractor extractor(input, outDir, false);
  auto &file = RobotExtractorTester::file(extractor);
  file.seekg(static_cast<std::streamoff>(kPadding), std::ios::beg);

  REQUIRE_NOTHROW(RobotExtractorTester::readHeader(extractor));
  REQUIRE_NOTHROW(RobotExtractorTester::readPrimer(extractor));
  REQUIRE_NOTHROW(RobotExtractorTester::readPalette(extractor));
  REQUIRE_NOTHROW(RobotExtractorTester::readSizesAndCues(extractor));

  const auto frameDataPos = file.tellg();
  REQUIRE(frameDataPos ==
          static_cast<std::streamoff>(kPadding + alignedIndexEnd));

  const auto &frames = RobotExtractorTester::frameSizes(extractor);
  REQUIRE(frames.size() == 1);
  REQUIRE(frames[0] == kFrameSize);

  const auto &packets = RobotExtractorTester::packetSizes(extractor);
  REQUIRE(packets.size() == 1);
  REQUIRE(packets[0] == kPacketSize);
}
