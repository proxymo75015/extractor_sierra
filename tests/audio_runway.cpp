#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "robot_extractor.hpp"
#include "utilities.hpp"

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr uint16_t kAudioBlockSize = 24;
constexpr size_t kRunwayBytes = 8;
constexpr size_t kTruncatedPayloadBytes = 2;
constexpr size_t kBlockBytes = static_cast<size_t>(kAudioBlockSize) - 8;
constexpr size_t kExpectedPayloadBytes =
    kBlockBytes > kRunwayBytes ? kBlockBytes - kRunwayBytes : size_t{0};

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
  push16(h, kAudioBlockSize); // audio block size
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

TEST_CASE("Audio block with runway triggers error") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "runway.rbt";
  fs::path outDir = tmpDir / "runway_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }  
  fs::create_directories(outDir);

  auto data = build_header();
  auto primerHeader = build_primer_header();
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  for (size_t i = 0; i < kRunwayBytes; ++i)
    data.push_back(0x88); // even primer data

  push16(data, 2);  // frame size
  push16(data, 26); // packet size

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  // Pad to 2048-byte boundary
  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  // Frame data (numCels = 0)
  data.push_back(0); // numCels low byte
  data.push_back(0); // numCels high byte

  push32(data, 2);  // pos (even)
  const uint32_t truncatedSize =
      static_cast<uint32_t>(kRunwayBytes + kTruncatedPayloadBytes);
  push32(data, truncatedSize);
  for (size_t i = 0; i < kRunwayBytes; ++i)
    data.push_back(static_cast<uint8_t>(i));
  data.push_back(0x88);
  data.push_back(0x77);
  const size_t payloadPadding = kExpectedPayloadBytes > kTruncatedPayloadBytes
                                    ? kExpectedPayloadBytes - kTruncatedPayloadBytes
                                    : size_t{0};
  for (size_t i = 0; i < payloadPadding; ++i)
    data.push_back(0); // padding to audio block size

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  fs::path wavPath;
  std::string bestName;
  for (const auto &entry : fs::directory_iterator(outDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto name = entry.path().filename().string();
    if (name.rfind("frame_", 0) != 0 ||
        name.find("_even.wav") == std::string::npos) {
      continue;
    }
    if (wavPath.empty() || name > bestName) {
      wavPath = entry.path();
      bestName = name;
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

  std::vector<std::byte> runway;
  runway.reserve(kRunwayBytes);
  for (size_t i = 0; i < kRunwayBytes; ++i) {
    runway.push_back(static_cast<std::byte>(i));
  }

  std::vector<std::byte> payload = {
      std::byte{static_cast<unsigned char>(0x88)},
      std::byte{static_cast<unsigned char>(0x77)},
  };
  REQUIRE(payload.size() == kTruncatedPayloadBytes);
  if (payload.size() < kExpectedPayloadBytes) {
    payload.resize(kExpectedPayloadBytes,
                   std::byte{static_cast<unsigned char>(0)});
  }
  REQUIRE(payload.size() == kExpectedPayloadBytes);
  std::vector<std::byte> block = runway;
  block.insert(block.end(), payload.begin(), payload.end());

  int16_t predictor = 0;
  auto allSamples =
      robot::dpcm16_decompress(std::span<const std::byte>(block), predictor);
  std::vector<int16_t> expectedSamples;
  constexpr size_t runwaySamples = kRunwayBytes * 2;
  if (allSamples.size() > runwaySamples) {
    expectedSamples.assign(allSamples.begin() +
                               static_cast<std::ptrdiff_t>(runwaySamples),
                           allSamples.end());
  }

  REQUIRE(actualSamples == expectedSamples);
}
