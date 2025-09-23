#include <algorithm>
#include <array>
#include <algorithm>
#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>

#include "robot_extractor.hpp"
#include "utilities.hpp"

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr size_t kZeroPrefixBytes = robot::kRobotZeroCompressSize;
constexpr size_t kRunwayBytes = robot::kRobotRunwayBytes;
constexpr size_t kRunwaySamples = robot::kRobotRunwayBytes;

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

static std::vector<uint8_t> build_header(uint16_t numFrames) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);   // version
  push16(h, 24);  // audio block size
  push16(h, 0);   // primerZeroCompressFlag
  push16(h, 0);   // skip
  push16(h, numFrames);
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
  push32(p, kPrimerHeaderSize +
                     static_cast<uint32_t>(kRunwayBytes));
  push16(p, 0); // compType
  push32(p, static_cast<uint32_t>(kRunwayBytes)); // even size
  push32(p, 0); // odd size
  return p;
}

TEST_CASE("Truncated audio block keeps stream aligned") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "trunc_audio_followup.rbt";
  fs::path outDir = tmpDir / "trunc_audio_followup_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  if (fs::exists(input)) {
    fs::remove(input);
  }
  fs::create_directories(outDir);

  auto data = build_header(2);
  std::vector<int16_t> expectedBlock0;
  std::vector<int16_t> expectedBlock1;
  uint32_t block1Pos = 0;
  const uint32_t block0Pos = 4;  
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());
  for (size_t i = 0; i < kRunwayBytes; ++i)
    data.push_back(0x88); // even primer data

  // frame sizes
  push16(data, 2);
  push16(data, 2);
  // packet sizes (frame size + audio block length)
  push16(data, 22);
  push16(data, 26);

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  // Pad to 2048-byte boundary
  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  // Frame 0 data (numCels = 0)
  data.push_back(0);
  data.push_back(0);

  // Audio block 0 truncated to payload-only bytes (padding fills the rest)
  push32(data, block0Pos); // pos (even)
  const uint32_t block0Size = 2;
  push32(data, block0Size);
  std::vector<uint8_t> block0PayloadBytes = {0x88, 0x77};
  data.insert(data.end(), block0PayloadBytes.begin(), block0PayloadBytes.end());
  for (int i = 0; i < 10; ++i)
    data.push_back(0); // padding to reach 20 bytes

  std::vector<std::byte> block0Compressed(kZeroPrefixBytes, std::byte{0});
  for (uint8_t b : block0PayloadBytes) {
    block0Compressed.push_back(static_cast<std::byte>(b));
  }
  int16_t predictor0 = 0;
  auto block0AllSamples =
      robot::dpcm16_decompress(std::span(block0Compressed), predictor0);
  if (block0AllSamples.size() > kRunwaySamples) {
    expectedBlock0.assign(block0AllSamples.begin() +
                              static_cast<std::ptrdiff_t>(kRunwaySamples),
                          block0AllSamples.end());
  }
  const size_t block0SampleCount = expectedBlock0.size();
  const size_t block0StartSample = block0Pos / 2;
  block1Pos = static_cast<uint32_t>((block0StartSample + block0SampleCount) * 2);

  // Frame 1 data (numCels = 0)
  data.push_back(0);
  data.push_back(0);

  // Audio block 1 fully populated
  push32(data, block1Pos); // pos (even)
  push32(data, 16);  // size (full block)
  std::array<uint8_t, 8> runway1 = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
  data.insert(data.end(), runway1.begin(), runway1.end());
  std::array<uint8_t, 8> payload1 = {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe};
  data.insert(data.end(), payload1.begin(), payload1.end());

  std::vector<std::byte> block1Compressed;
  block1Compressed.reserve(runway1.size() + payload1.size());
  for (uint8_t b : runway1) {
    block1Compressed.push_back(static_cast<std::byte>(b));
  }
  for (uint8_t b : payload1) {
    block1Compressed.push_back(static_cast<std::byte>(b));
  }
  int16_t predictor1 = 0;
  auto block1AllSamples =
      robot::dpcm16_decompress(std::span(block1Compressed), predictor1);
  if (block1AllSamples.size() > kRunwaySamples) {
    expectedBlock1.assign(block1AllSamples.begin() +
                              static_cast<std::ptrdiff_t>(kRunwaySamples),
                          block1AllSamples.end());
  }

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  // Validate metadata contains two frames
  fs::path metadataPath = outDir / "metadata.json";
  std::ifstream meta(metadataPath);
  REQUIRE(meta);
  nlohmann::json jsonDoc;
  meta >> jsonDoc;
  REQUIRE(jsonDoc.contains("frames"));
  REQUIRE(jsonDoc["frames"].is_array());
  REQUIRE(jsonDoc["frames"].size() == 2);

  fs::path wavPath = outDir / "frame_00000_even.wav";
  REQUIRE(fs::exists(wavPath));

  auto readSamples = [](const fs::path &path) {
    std::ifstream wav(path, std::ios::binary);
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
    std::vector<int16_t> samples;
    samples.reserve(audioData.size() / 2);
    for (size_t i = 0; i + 1 < audioData.size(); i += 2) {
      uint16_t lo = audioData[i];
      uint16_t hi = static_cast<uint16_t>(audioData[i + 1]) << 8;
      samples.push_back(static_cast<int16_t>(lo | hi));
    }
    return samples;
  };
  
  auto actualSamples = readSamples(wavPath);

  const size_t block1StartSample = block1Pos / 2;
  size_t totalSamples = std::max(block0StartSample + expectedBlock0.size(),
                                 block1StartSample + expectedBlock1.size());
  std::vector<int16_t> expected(totalSamples, 0);
  if (!expectedBlock0.empty()) {
    std::copy(expectedBlock0.begin(), expectedBlock0.end(),
              expected.begin() + static_cast<std::ptrdiff_t>(block0StartSample));
  }
  if (!expectedBlock1.empty()) {
    std::copy(expectedBlock1.begin(), expectedBlock1.end(),
              expected.begin() + static_cast<std::ptrdiff_t>(block1StartSample));
  }
  REQUIRE(actualSamples == expected);
}
