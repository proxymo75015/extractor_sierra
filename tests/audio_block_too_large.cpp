#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr uint16_t kAudioBlockSize = 24;
constexpr uint32_t kDeclaredPayloadSize = 20; // supérieur au payload attendu (16)
constexpr uint32_t kStoredPayloadSize =
    kAudioBlockSize - static_cast<uint32_t>(robot::kRobotAudioHeaderSize);

void push16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

void push32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

std::vector<uint8_t> build_header(uint16_t audioBlockSize) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);   // version
  push16(h, audioBlockSize);
  push16(h, 0);   // primerZeroCompressFlag
  push16(h, 0);   // champ réservé
  push16(h, 1);   // numFrames
  push16(h, 0);   // paletteSize
  push16(h, static_cast<uint16_t>(kPrimerHeaderSize + robot::kRobotRunwayBytes));
  push16(h, 1);   // xRes
  push16(h, 1);   // yRes
  h.push_back(0); // hasPalette
  h.push_back(1); // hasAudio
  push16(h, 0);   // réservé
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

std::vector<uint8_t> build_primer_header() {
  std::vector<uint8_t> p;
  push32(p, kPrimerHeaderSize +
                  static_cast<uint32_t>(robot::kRobotRunwayBytes));
  push16(p, 0); // compType
  push32(p, static_cast<uint32_t>(robot::kRobotRunwayBytes));
  push32(p, 0); // odd size
  return p;
}

} // namespace

TEST_CASE("Oversized audio block is truncated gracefully") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "audio_block_too_large.rbt";
  fs::path outDir = tmpDir / "audio_block_too_large_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  std::vector<uint8_t> data = build_header(kAudioBlockSize);
  auto primerHeader = build_primer_header();
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  data.insert(data.end(), robot::kRobotRunwayBytes, 0x88);

  constexpr uint16_t kFrameSize = 2;
  constexpr uint16_t kPacketSize =
      kFrameSize + kAudioBlockSize;
  push16(data, kFrameSize);
  push16(data, kPacketSize);

  for (int i = 0; i < 256; ++i)
    push32(data, 0);
  for (int i = 0; i < 256; ++i)
    push16(data, 0);

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  // Frame data: zero cel
  data.push_back(0);
  data.push_back(0);

  // Audio block with payload larger than allowed (expected 16 bytes)
  push32(data, 2);                               // pos (even channel)
  push32(data, kDeclaredPayloadSize);            // taille déclarée (trop grande)
  data.insert(data.end(), kStoredPayloadSize, 0x42);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);

  REQUIRE_NOTHROW(robot::RobotExtractorTester::readHeader(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readPrimer(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readPalette(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readSizesAndCues(extractor));

  nlohmann::json frameJson;
  REQUIRE_NOTHROW(robot::RobotExtractorTester::exportFrame(extractor, 0, frameJson));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::finalizeAudio(extractor));

  const fs::path wavPath = outDir / "frame_00000.wav";
  REQUIRE(fs::exists(wavPath));
  REQUIRE(fs::file_size(wavPath) >= 44);
}

TEST_CASE("Audio block smaller than header is rejected") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "audio_block_too_small.rbt";
  fs::path outDir = tmpDir / "audio_block_too_small_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  const uint16_t tooSmallAudioBlock =
      static_cast<uint16_t>(robot::kRobotAudioHeaderSize - 2);
  std::vector<uint8_t> data = build_header(tooSmallAudioBlock);
  auto primerHeader = build_primer_header();
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  data.insert(data.end(), robot::kRobotRunwayBytes, 0x88);

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);

  const std::string expectedMessage =
      "Taille de bloc audio trop petite dans l'en-tête: " +
      std::to_string(tooSmallAudioBlock) + " (minimum " +
      std::to_string(robot::kRobotAudioHeaderSize) + ")";

  REQUIRE_THROWS_MATCHES(robot::RobotExtractorTester::readHeader(extractor),
                         std::runtime_error,
                         Catch::Matchers::Message(expectedMessage));
}

TEST_CASE("Audio block with no payload triggers an error") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "audio_block_no_payload.rbt";
  fs::path outDir = tmpDir / "audio_block_no_payload_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  const uint16_t headerOnlyBlock =
      static_cast<uint16_t>(robot::kRobotAudioHeaderSize);
  std::vector<uint8_t> data = build_header(headerOnlyBlock);
  auto primerHeader = build_primer_header();
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  data.insert(data.end(), robot::kRobotRunwayBytes, 0x88);

  constexpr uint16_t kFrameSize = 2;
  constexpr uint16_t kPacketSize =
      static_cast<uint16_t>(kFrameSize + robot::kRobotAudioHeaderSize);
  push16(data, kFrameSize);
  push16(data, kPacketSize);

  for (int i = 0; i < 256; ++i)
    push32(data, 0);
  for (int i = 0; i < 256; ++i)
    push16(data, 0);

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  // Frame data: zero cel
  data.push_back(0);
  data.push_back(0);

  // Audio block header only (will trigger the non-positive expected size error)
  push32(data, 2); // pos
  push32(data, 0); // taille déclarée

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);

  REQUIRE_NOTHROW(robot::RobotExtractorTester::readHeader(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readPrimer(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readPalette(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readSizesAndCues(extractor));

  nlohmann::json frameJson;
  const std::string expectedMessage =
      "Taille de bloc audio attendue non positive pour la frame 0: 0";
  REQUIRE_THROWS_MATCHES(robot::RobotExtractorTester::exportFrame(extractor, 0,
                                                                  frameJson),
                         std::runtime_error,
                         Catch::Matchers::Message(expectedMessage));
}
