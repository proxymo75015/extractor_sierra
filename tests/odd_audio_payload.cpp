#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "audio_reference_data.hpp"
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
  push16(h, static_cast<uint16_t>(kPrimerHeaderSize + 8));
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
  push32(p, kPrimerHeaderSize + 8);
  push16(p, 0); // compType
  push32(p, 8); // even size
  push32(p, 0); // odd size
  return p;
}

TEST_CASE("Odd-sized audio payload throws") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "odd_audio_payload.rbt";
  fs::path outDir = tmpDir / "odd_audio_payload_out";
  fs::create_directories(outDir);

  auto data = build_header();
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());
  for (int i = 0; i < 8; ++i)
    data.push_back(0x88); // even primer data

  push16(data, 2);  // frame size
  push16(data, 26); // packet size (frame 2 + audio block 24)

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048,
              0); // pad to 2048-byte boundary

  // Frame data (numCels = 0)
  data.push_back(0); // numCels low byte
  data.push_back(0); // numCels high byte

  // Audio block with odd-sized payload
  push32(data, 2); // pos (even)
  push32(data, 1); // size (payload byte only)
  data.push_back(0x88); // single audio byte
  for (int i = 0; i < 15; ++i)
    data.push_back(0); // padding to reach 24 bytes

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  fs::path wavPath;
  for (const auto &entry : fs::directory_iterator(outDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto name = entry.path().filename().string();
    if (name == "frame_00001_even.wav") {
      wavPath = entry.path();
      break;
    }
  }
  REQUIRE(!wavPath.empty());

  std::ifstream wav(wavPath, std::ios::binary);
  REQUIRE(wav);
  wav.seekg(40, std::ios::beg);
  std::array<unsigned char, 4> dataSizeBytes{};
  wav.read(reinterpret_cast<char *>(dataSizeBytes.data()),
           static_cast<std::streamsize>(dataSizeBytes.size()));
  REQUIRE(wav);
  uint32_t dataBytes = static_cast<uint32_t>(dataSizeBytes[0]) |
                       (static_cast<uint32_t>(dataSizeBytes[1]) << 8) |
                       (static_cast<uint32_t>(dataSizeBytes[2]) << 16) |
                       (static_cast<uint32_t>(dataSizeBytes[3]) << 24);
  REQUIRE(dataBytes % 2 == 0);
  wav.seekg(44, std::ios::beg);
  std::vector<uint8_t> audioData(dataBytes);
  if (!audioData.empty()) {
    wav.read(reinterpret_cast<char *>(audioData.data()),
             static_cast<std::streamsize>(audioData.size()));
    REQUIRE(wav.gcount() == static_cast<std::streamsize>(audioData.size()));
  }

  std::vector<int16_t> actualSamples;
  actualSamples.reserve(audioData.size() / 2);
  for (size_t i = 0; i + 1 < audioData.size(); i += 2) {
    uint16_t lo = audioData[i];
    uint16_t hi = static_cast<uint16_t>(audioData[i + 1]) << 8;
    actualSamples.push_back(static_cast<int16_t>(lo | hi));
  }

  std::vector<int16_t> expectedSamples(
      test_audio_reference::kScummVmOddPayloadEvenBlock.begin(),
      test_audio_reference::kScummVmOddPayloadEvenBlock.end());

  REQUIRE(actualSamples == expectedSamples);
}
