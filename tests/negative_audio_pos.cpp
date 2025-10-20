#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "audio_decompression_helpers.hpp"
#include "robot_extractor.hpp"
#include "utilities.hpp"

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr uint32_t kRunwayBytes = static_cast<uint32_t>(robot::kRobotRunwayBytes);

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
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);   // version
  push16(h, 24);  // audio block size
  push16(h, 0);   // primerZeroCompressFlag
  push16(h, 0);   // skip
  push16(h, 1);   // numFrames
  push16(h, 0);   // paletteSize
  push16(h, static_cast<uint16_t>(kPrimerHeaderSize + kRunwayBytes));
  push16(h, 1);   // xRes
  push16(h, 1);   // yRes
  h.push_back(0); // hasPalette
  h.push_back(1); // hasAudio
  push16(h, 0);   // skip
  push16(h, 60);  // frameRate
  push16(h, 0);   // isHiRes
  push16(h, 0);   // maxSkippablePackets
  push16(h, 1);   // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0); // champs supplémentaires  
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // zone réservée
  return h;
}

static std::vector<uint8_t> build_primer_header() {
  std::vector<uint8_t> p;
  push32(p, kPrimerHeaderSize + kRunwayBytes);
  push16(p, 0); // compType
  push32(p, kRunwayBytes); // even size
  push32(p, 0); // odd size
  return p;
}

TEST_CASE("Negative audio position is ignored") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "neg_audio_pos.rbt";
  fs::path outDir = tmpDir / "neg_audio_pos_out";
  fs::create_directories(outDir);

  auto data = build_header();
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());
  for (uint32_t i = 0; i < kRunwayBytes; ++i)
    data.push_back(0x88); // even primer data

  push16(data, 2);  // frame size
  push16(data, 26); // packet size

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048,
              0); // pad to 2048-byte boundary

  // Frame data (numCels = 0)
  data.push_back(0); // numCels low byte
  data.push_back(0); // numCels high byte

  push32(data, static_cast<uint32_t>(-2)); // negative pos
  push32(data, 10);                        // size (unused, but valid)
  for (uint32_t i = 0; i < kRunwayBytes; ++i)
    data.push_back(0); // runway
  for (int i = 0; i < 2; ++i)
    data.push_back(0); // audio data
  for (int i = 0; i < 6; ++i)
    data.push_back(0); // padding

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());
}

TEST_CASE("Audio block with position -1 is adjusted without corruption") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "neg_audio_pos_minus1.rbt";
  fs::path outDir = tmpDir / "neg_audio_pos_minus1_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  auto header = build_header();
  auto primer = build_primer_header();
  header.insert(header.end(), primer.begin(), primer.end());
  for (uint32_t i = 0; i < kRunwayBytes; ++i) {
    header.push_back(static_cast<uint8_t>(0x20 + (i * 3))); // even primer
  }

  push16(header, 2);        // frame size
  push16(header, 26);       // packet size (frame + audio block 24)

  for (int i = 0; i < 256; ++i)
    push32(header, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(header, 0); // cue values

  header.resize(((header.size() + 2047) / 2048) * 2048, 0);

  header.push_back(0); // numCels low
  header.push_back(0); // numCels high

  std::vector<uint8_t> blockData = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
                                    0xDE, 0xF0};
  std::vector<uint8_t> runway;
  runway.reserve(kRunwayBytes);
  for (uint32_t i = 0; i < kRunwayBytes; ++i) {
    runway.push_back(static_cast<uint8_t>(0x40 + i));
  }
  std::vector<uint8_t> fullBlock(runway);
  fullBlock.insert(fullBlock.end(), blockData.begin(), blockData.end());

  push32(header, static_cast<uint32_t>(-1));
  push32(header, static_cast<uint32_t>(fullBlock.size()));
  header.insert(header.end(), fullBlock.begin(), fullBlock.end());

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(header.data()),
            static_cast<std::streamsize>(header.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  std::vector<std::byte> primerBytes(kRunwayBytes);
  for (uint32_t i = 0; i < kRunwayBytes; ++i) {
    primerBytes[i] = std::byte{static_cast<unsigned char>(0x20 + (i * 3))};
  }
  int16_t evenPredictor = 0;
  auto primerSamples =
      audio_test::decompress_primer(primerBytes, evenPredictor);
  (void)primerSamples;
  int16_t oddPredictor = 0;
  auto fullSamples =
      audio_test::decompress_without_runway(fullBlock, oddPredictor);
  audio_test::ChannelExpectation oddExpected;
  audio_test::append_expected(oddExpected, -1, fullSamples);
  auto evenStream = robot::RobotExtractorTester::buildChannelStream(extractor, true);
  auto oddStream = robot::RobotExtractorTester::buildChannelStream(extractor, false);

  REQUIRE(evenStream.empty());
  REQUIRE(oddStream == oddExpected.samples);
}
