#include <algorithm>
#include <array>
#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);

static constexpr std::array<int16_t, 16> kDpcmTable = {
    -0x0c0, -0x080, -0x040, -0x020, -0x010, -0x008, -0x004, -0x002,
    0x002,  0x004,  0x008,  0x010,  0x020,  0x040,  0x080,  0x0c0,
};

static int16_t clamp_predictor(int32_t value) {
  return static_cast<int16_t>(
      std::clamp(value, static_cast<int32_t>(-32768),
                 static_cast<int32_t>(32767)));
}

static void dpcm16_decompress_last_bytes(const std::vector<uint8_t> &bytes,
                                         int16_t &predictor) {
  int32_t value = predictor;
  for (uint8_t b : bytes) {
    uint8_t hi = b >> 4;
    value = clamp_predictor(value + kDpcmTable[hi]);
    predictor = static_cast<int16_t>(value);
    uint8_t lo = static_cast<uint8_t>(b & 0x0F);
    value = clamp_predictor(value + kDpcmTable[lo]);
    predictor = static_cast<int16_t>(value);
  }
}

static std::vector<int16_t> dpcm16_decompress_bytes(
    const std::vector<uint8_t> &bytes, int16_t &predictor) {
  std::vector<int16_t> out;
  out.reserve(bytes.size() * 2);
  int32_t value = predictor;
  for (uint8_t b : bytes) {
    uint8_t hi = b >> 4;
    value = clamp_predictor(value + kDpcmTable[hi]);
    predictor = static_cast<int16_t>(value);
    out.push_back(predictor);
    uint8_t lo = static_cast<uint8_t>(b & 0x0F);
    value = clamp_predictor(value + kDpcmTable[lo]);
    predictor = static_cast<int16_t>(value);
    out.push_back(predictor);
  }
  return out;
}

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
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());
  for (int i = 0; i < 8; ++i)
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

  // Audio block 0 truncated to 20 bytes (header + 10 bytes payload + padding)
  push32(data, 2);  // pos (even)
  push32(data, 10); // size (8 runway + 2 data)
  for (int i = 0; i < 8; ++i)
    data.push_back(static_cast<uint8_t>(i));
  data.push_back(0x88);
  data.push_back(0x77);
  for (int i = 0; i < 2; ++i)
    data.push_back(0); // padding to reach 20 bytes

  // Frame 1 data (numCels = 0)
  data.push_back(0);
  data.push_back(0);

  // Audio block 1 fully populated
  push32(data, 4);   // pos (even)
  push32(data, 16);  // size (full block)
  std::array<uint8_t, 8> runway1 = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
  data.insert(data.end(), runway1.begin(), runway1.end());
  std::array<uint8_t, 8> payload1 = {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe};
  data.insert(data.end(), payload1.begin(), payload1.end());

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

  fs::path wavFrame2;
  for (const auto &entry : fs::directory_iterator(outDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto name = entry.path().filename().string();
    if (name == "frame_00002_even.wav") {
      wavFrame2 = entry.path();
      break;
    }
  }
  REQUIRE(!wavFrame2.empty());

  std::ifstream wav(wavFrame2, std::ios::binary);
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

  int16_t predictor = 0;
  std::vector<uint8_t> primerBytes(8, 0x88);
  auto primerSamples = dpcm16_decompress_bytes(primerBytes, predictor);
  REQUIRE(primerSamples.size() == 16);
  std::vector<uint8_t> runway0 = {0, 1, 2, 3, 4, 5, 6, 7};
  dpcm16_decompress_last_bytes(runway0, predictor);
  std::vector<uint8_t> payload0 = {0x88, 0x77};
  auto block0Samples = dpcm16_decompress_bytes(payload0, predictor);
  REQUIRE(block0Samples.size() == 4);
  std::vector<uint8_t> runwayBytes1(runway1.begin(), runway1.end());
  dpcm16_decompress_last_bytes(runwayBytes1, predictor);
  std::vector<uint8_t> payloadBytes1(payload1.begin(), payload1.end());
  auto expectedSamples = dpcm16_decompress_bytes(payloadBytes1, predictor);

  std::vector<int16_t> actualSamples;
  actualSamples.reserve(audioData.size() / 2);
  for (size_t i = 0; i + 1 < audioData.size(); i += 2) {
    uint16_t lo = audioData[i];
    uint16_t hi = static_cast<uint16_t>(audioData[i + 1]) << 8;
    actualSamples.push_back(static_cast<int16_t>(lo | hi));
  }

  REQUIRE(actualSamples == expectedSamples);
}
