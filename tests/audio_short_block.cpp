#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"
#include "wav_helpers.hpp"

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);

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

std::vector<uint8_t> build_header(uint16_t payloadSize) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);                                              // version
  push16(h, static_cast<uint16_t>(robot::kRobotAudioHeaderSize + payloadSize));
  push16(h, 0);                                              // primerZeroCompressFlag
  push16(h, 0);                                              // reserved
  push16(h, 1);                                              // numFrames
  push16(h, 0);                                              // paletteSize
  push16(h, static_cast<uint16_t>(kPrimerHeaderSize));
  push16(h, 1);                                              // xRes
  push16(h, 1);                                              // yRes
  h.push_back(0);                                            // hasPalette
  h.push_back(1);                                            // hasAudio
  push16(h, 0);                                              // reserved
  push16(h, 24);                                             // frameRate
  push16(h, 0);                                              // isHiRes
  push16(h, 0);                                              // maxSkippablePackets
  push16(h, 1);                                              // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0);
  for (int i = 0; i < 2; ++i)
    push32(h, 0);
  return h;
}

std::vector<uint8_t> build_primer_header() {
  std::vector<uint8_t> primer;
  push32(primer, kPrimerHeaderSize);
  push16(primer, 0); // compType
  push32(primer, 0); // even size
  push32(primer, 0); // odd size
  return primer;
}
} // namespace

TEST_CASE("Audio block smaller than runway is not zero-extended") {
  constexpr uint16_t kPayloadSize = 6;
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "audio_short_block.rbt";
  fs::path outDir = tmpDir / "audio_short_block_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  std::vector<uint8_t> data = build_header(kPayloadSize);
  auto primerHeader = build_primer_header();
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());

  constexpr uint16_t kFrameSize = 2;
  constexpr uint16_t kPacketSize =
      kFrameSize + static_cast<uint16_t>(robot::kRobotAudioHeaderSize + kPayloadSize);
  push16(data, kFrameSize);
  push16(data, kPacketSize);

  for (int i = 0; i < 256; ++i)
    push32(data, 0);
  for (int i = 0; i < 256; ++i)
    push16(data, 0);

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  data.push_back(0); // numCels low byte
  data.push_back(0); // numCels high byte

  push32(data, 4);               // pos (even channel)
  push32(data, kPayloadSize);    // size
  std::array<uint8_t, kPayloadSize> payload = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  data.insert(data.end(), payload.begin(), payload.end());

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  const auto evenStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  REQUIRE(evenStream.empty());

  fs::path wavPath = outDir / "frame_00000.wav";
  if (fs::exists(wavPath)) {
    std::ifstream wav(wavPath, std::ios::binary);
    REQUIRE(wav);
    auto layout = read_wav_layout(wav);
    REQUIRE(layout.dataSize == 0);
  }
}
