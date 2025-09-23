#include <array>
#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>

#include "robot_extractor.hpp"
#include "utilities.hpp"

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
  push16(h, static_cast<uint16_t>(kPrimerHeaderSize +
                                  robot::kRobotRunwayBytes));
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
                 static_cast<uint32_t>(robot::kRobotRunwayBytes));
  push16(p, 0); // compType
  push32(p, static_cast<uint32_t>(robot::kRobotRunwayBytes)); // even size
  push32(p, 0); // odd size
  return p;
}

static std::vector<int16_t> decode_block(const std::vector<uint8_t> &bytes) {
  std::vector<std::byte> asBytes;
  asBytes.reserve(bytes.size());
  for (uint8_t b : bytes) {
    asBytes.push_back(static_cast<std::byte>(b));
  }
  int16_t predictor = 0;
  auto decoded = robot::dpcm16_decompress(std::span(asBytes), predictor);
  if (decoded.size() <= robot::kRobotRunwayBytes) {
    return {};
  }
  return std::vector<int16_t>(decoded.begin() +
                                  static_cast<std::ptrdiff_t>(robot::kRobotRunwayBytes),
                              decoded.end());
}

TEST_CASE("Retransmitted audio blocks append only fresh data") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "retrans_audio.rbt";
  fs::path outDir = tmpDir / "retrans_audio_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  if (fs::exists(input)) {
    fs::remove(input);
  }
  fs::create_directories(outDir);

  constexpr uint16_t kNumFrames = 3;
  auto data = build_header(kNumFrames);
  auto primerHeader = build_primer_header();
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  std::array<uint8_t, robot::kRobotRunwayBytes> primerEven{};
  primerEven.fill(0x87);
  data.insert(data.end(), primerEven.begin(), primerEven.end());

  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 2); // frame size
  }
  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 26); // frame size + audio block (8 header + 16 data)
  }
  for (int i = 0; i < 256; ++i) {
    push32(data, 0);
  }
  for (int i = 0; i < 256; ++i) {
    push16(data, 0);
  }

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  const std::array<uint8_t, robot::kRobotRunwayBytes> runwayBlock1 = {
      0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87};
  const std::array<uint8_t, robot::kRobotRunwayBytes> runwayBlock2 = {
      0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x97};
  const std::array<uint8_t, robot::kRobotRunwayBytes> block1Tail = {
      0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88};
  const std::array<uint8_t, robot::kRobotRunwayBytes> block2Tail = {
      0xB8, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88};

  auto buildBlock =
      [&](const std::array<uint8_t, robot::kRobotRunwayBytes> &runway,
          const std::array<uint8_t, robot::kRobotRunwayBytes> &tail) {
    std::vector<uint8_t> block;
    block.reserve(runway.size() + tail.size());
    block.insert(block.end(), runway.begin(), runway.end());
    block.insert(block.end(), tail.begin(), tail.end());
    return block;
  };

  auto block1 = buildBlock(runwayBlock1, block1Tail);
  auto block2 = buildBlock(runwayBlock2, block2Tail);
  auto block3 = buildBlock(runwayBlock2, block2Tail);

  const uint32_t block1Pos = 4;
  const uint32_t block2Pos = 20; // overlaps block1 tail by eight samples
  const uint32_t block3Pos = block2Pos; // exact retransmission

  auto appendFrame = [&](const std::vector<uint8_t> &block, uint32_t pos) {
    data.push_back(0);
    data.push_back(0);
    push32(data, pos);
    push32(data, static_cast<uint32_t>(block.size()));
    data.insert(data.end(), block.begin(), block.end());
  };

  appendFrame(block1, block1Pos);
  appendFrame(block2, block2Pos);
  appendFrame(block3, block3Pos);

  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  }

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  fs::path metadataPath = outDir / "metadata.json";
  std::ifstream meta(metadataPath);
  REQUIRE(meta);
  nlohmann::json jsonDoc;
  meta >> jsonDoc;
  REQUIRE(jsonDoc.contains("frames"));
  REQUIRE(jsonDoc["frames"].is_array());
  REQUIRE(jsonDoc["frames"].size() == kNumFrames);

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
    std::vector<int16_t> samples(dataBytes / sizeof(int16_t));
    if (!samples.empty()) {
      wav.read(reinterpret_cast<char *>(samples.data()),
               static_cast<std::streamsize>(dataBytes));
    }
    REQUIRE(wav);
    return samples;
  };

  auto expectedBlock1 = decode_block(block1);
  auto expectedBlock2 = decode_block(block2);
  auto expectedBlock3 = decode_block(block3);

  const size_t block1Start = block1Pos / 2;
  const size_t block2Start = block2Pos / 2;
  const size_t block3Start = block3Pos / 2;

  std::vector<int16_t> expectedSamples;
  auto appendSamples = [&](size_t start, const std::vector<int16_t> &samples) {
    if (samples.empty()) {
      return;
    }
    if (expectedSamples.size() < start) {
      expectedSamples.resize(start, 0);
    }
    size_t newSize = start + samples.size();
    if (expectedSamples.size() < newSize) {
      expectedSamples.resize(newSize, 0);
    }
    for (size_t i = 0; i < samples.size(); ++i) {
      expectedSamples[start + i] = samples[i];
    }
  };

  appendSamples(block1Start, expectedBlock1);
  appendSamples(block2Start, expectedBlock2);
  appendSamples(block3Start, expectedBlock3);

  auto wavSamples = readSamples(wavPath);
  REQUIRE(wavSamples == expectedSamples);
}
