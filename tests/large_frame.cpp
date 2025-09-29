#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <limits>
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

static std::vector<uint8_t> build_header_v6(uint16_t maxCelsPerFrame) {
  std::vector<uint8_t> h;
  push16(h, 0x16);           // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 6);              // version
  push16(h, 0);              // audio block size
  push16(h, 0);              // primerZeroCompressFlag
  push16(h, 0);              // skip
  push16(h, 1);              // numFrames
  push16(h, 0);              // paletteSize
  push16(h, 0);              // primerReservedSize
  push16(h, 1);              // xRes
  push16(h, 1);              // yRes
  h.push_back(0);            // hasPalette
  h.push_back(0);            // hasAudio
  push16(h, 0);              // skip
  push16(h, 60);             // frameRate
  push16(h, 0);              // isHiRes
  push16(h, 0);              // maxSkippablePackets
  push16(h, maxCelsPerFrame); // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0);            // champs supplémentaires
  for (int i = 0; i < 2; ++i)
    push32(h, 0);            // zone réservée
  return h;
}

static std::vector<uint8_t> build_large_frame(uint16_t numCels,
                                              uint32_t chunkSize) {
  std::vector<uint8_t> data;
  data.reserve(static_cast<size_t>(numCels) * (22u + 10u + chunkSize) + 2u);
  push16(data, numCels);

  const uint16_t dataSize = static_cast<uint16_t>(10 + chunkSize);
  for (uint16_t i = 0; i < numCels; ++i) {
    std::vector<uint8_t> celHeader(22, 0);
    celHeader[1] = 100; // vertical scale
    celHeader[2] = 1;   // width low byte
    celHeader[4] = 1;   // height low byte
    celHeader[14] = static_cast<uint8_t>(dataSize & 0xFF);
    celHeader[15] = static_cast<uint8_t>(dataSize >> 8);
    celHeader[16] = 1; // numChunks
    data.insert(data.end(), celHeader.begin(), celHeader.end());

    push32(data, chunkSize);
    push32(data, 1); // decomp size
    push16(data, 2); // raw chunk
    data.insert(data.end(), chunkSize, 0x41);
  }

  return data;
}

TEST_CASE("Les frames supérieures à 10 MiB sont extraites avec succès") {
  constexpr uint16_t kNumCels = 200;
  constexpr uint32_t kChunkSize = 60000;
  REQUIRE(kChunkSize + 10 <= std::numeric_limits<uint16_t>::max());
  auto frameData = build_large_frame(kNumCels, kChunkSize);
  const uint32_t frameSize = static_cast<uint32_t>(frameData.size());
  REQUIRE(frameSize > 10u * 1024u * 1024u);

  auto data = build_header_v6(kNumCels);
  push32(data, frameSize); // frame size table
  push32(data, frameSize); // packet size table
  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  const size_t padding = (2048 - (data.size() % 2048)) % 2048;
  data.insert(data.end(), padding, 0);
  data.insert(data.end(), frameData.begin(), frameData.end());

  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "large_frame.rbt";
  fs::path outDir = tmpDir / "large_frame_out";
  fs::create_directories(outDir);

  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  }

  robot::RobotExtractor extractor(input, outDir, false);
  REQUIRE_NOTHROW(extractor.extract());

  const auto &frames = RobotExtractorTester::frameSizes(extractor);
  REQUIRE(frames.size() == 1);
  CHECK(frames[0] == frameSize);

  const auto &packets = RobotExtractorTester::packetSizes(extractor);
  REQUIRE(packets.size() == 1);
  CHECK(packets[0] == frameSize);

  fs::remove(input);
  fs::remove_all(outDir);
}
