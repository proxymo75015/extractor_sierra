#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"

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

TEST_CASE("Primer WAV excludes runway samples") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "primer_runway.rbt";
  fs::path outDir = tmpDir / "primer_runway_out";
  fs::create_directories(outDir);

  constexpr uint16_t primerReserved = static_cast<uint16_t>(
      kPrimerHeaderSize + robot::kRobotRunwayBytes + 2);
  auto data = build_header(primerReserved);
  auto primer = build_primer_header(
      kPrimerHeaderSize + robot::kRobotRunwayBytes + 2,
      static_cast<uint32_t>(robot::kRobotRunwayBytes + 2), 0);
  data.insert(data.end(), primer.begin(), primer.end());
  for (size_t i = 0; i < robot::kRobotRunwayBytes; ++i)
    data.push_back(static_cast<uint8_t>(i));
  data.push_back(0x88);
  data.push_back(0x77);

  push16(data, 2); // frame size
  push16(data, 2); // packet size (no audio block)
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

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  auto wavPath = outDir / "frame_00000.wav";
  REQUIRE(fs::exists(wavPath));
  REQUIRE(fs::file_size(wavPath) == 52); // 44 header + 8 data bytes (stéréo)

  std::ifstream wav(wavPath, std::ios::binary);
  REQUIRE(wav);
  std::array<uint8_t, 44> header{};
  wav.read(reinterpret_cast<char *>(header.data()), header.size());
  uint16_t channels = header[22] |
                      (static_cast<uint16_t>(header[23]) << 8);
  REQUIRE(channels == 2);  
  uint32_t rate = header[24] | (static_cast<uint32_t>(header[25]) << 8) |
                  (static_cast<uint32_t>(header[26]) << 16) |
                  (static_cast<uint32_t>(header[27]) << 24);
  REQUIRE(rate == 22050);

  uint32_t byteRate = header[28] | (static_cast<uint32_t>(header[29]) << 8) |
                      (static_cast<uint32_t>(header[30]) << 16) |
                      (static_cast<uint32_t>(header[31]) << 24);
  REQUIRE(byteRate == 22050u * 2u * 2u);

  uint16_t blockAlign = header[32] | (static_cast<uint16_t>(header[33]) << 8);
  REQUIRE(blockAlign == 4);
}
