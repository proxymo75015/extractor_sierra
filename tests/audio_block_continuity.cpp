#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "robot_extractor.hpp"
#include "utilities.hpp"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr uint16_t kAudioBlockSize = 24;
constexpr size_t kRunwayBytes = robot::kRobotRunwayBytes;
constexpr size_t kRunwaySamples = robot::kRobotRunwayBytes;

void push16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>(x >> 8));
}

void push32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

std::vector<uint8_t> build_header(uint16_t primerReserved,
                                  uint16_t numFrames) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);                 // version
  push16(h, kAudioBlockSize);   // audio block size
  push16(h, 0);                 // primerZeroCompressFlag
  push16(h, 0);                 // skip
  push16(h, numFrames);         // numFrames
  push16(h, 0);                 // paletteSize
  push16(h, primerReserved);    // primerReservedSize
  push16(h, 1);                 // xRes
  push16(h, 1);                 // yRes
  h.push_back(0);               // hasPalette
  h.push_back(1);               // hasAudio
  push16(h, 0);                 // skip
  push16(h, 60);                // frameRate
  push16(h, 0);                 // isHiRes
  push16(h, 0);                 // maxSkippablePackets
  push16(h, 1);                 // maxCelsPerFrame
  for (int i = 0; i < 4; ++i)
    push32(h, 0); // champs supplémentaires
  for (int i = 0; i < 2; ++i)
    push32(h, 0); // zone réservée
  return h;
}

std::vector<uint8_t> build_primer_header(uint32_t total, uint32_t evenSize,
                                         uint32_t oddSize) {
  std::vector<uint8_t> p;
  push32(p, total);
  push16(p, 0); // compType
  push32(p, evenSize);
  push32(p, oddSize);
  return p;
}

std::vector<std::byte> to_bytes(const std::vector<uint8_t> &src) {
  std::vector<std::byte> out;
  out.reserve(src.size());
  for (uint8_t b : src) {
    out.push_back(static_cast<std::byte>(b));
  }
  return out;
}

std::vector<int16_t> decompress_without_runway(const std::vector<uint8_t> &bytes) {
  auto asBytes = to_bytes(bytes);
  int16_t predictor = 0;
  auto pcm = robot::dpcm16_decompress(std::span(asBytes), predictor);
  std::vector<int16_t> samples;
  if (pcm.size() > kRunwaySamples) {
    samples.assign(pcm.begin() + static_cast<std::ptrdiff_t>(kRunwaySamples),
                   pcm.end());
  }
  return samples;
}

struct StereoSamples {
  std::vector<int16_t> even;
  std::vector<int16_t> odd;
};

StereoSamples read_wav_samples(const fs::path &wavPath) {
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
  REQUIRE(dataBytes % 4 == 0);
  wav.seekg(44, std::ios::beg);
  std::vector<int16_t> interleaved(dataBytes / 2);
  if (!interleaved.empty()) {
    wav.read(reinterpret_cast<char *>(interleaved.data()),
             static_cast<std::streamsize>(dataBytes));
    REQUIRE(wav.gcount() == static_cast<std::streamsize>(dataBytes));
  }
  StereoSamples result;
  const size_t frameCount = interleaved.size() / 2;
  result.even.reserve(frameCount);
  result.odd.reserve(frameCount);
  for (size_t i = 0; i < frameCount; ++i) {
    result.even.push_back(interleaved[i * 2]);
    result.odd.push_back(interleaved[i * 2 + 1]);
  }
  return result;
}

void fill_gap(std::vector<int16_t> &buffer, size_t gapStart, size_t gapEnd,
              int16_t prevSample, int16_t nextSample) {
  if (gapEnd <= gapStart) {
    return;
  }
  const size_t prevIndex = gapStart - 1;
  const size_t span = gapEnd - prevIndex;
  for (size_t i = gapStart; i < gapEnd; ++i) {
    double t = static_cast<double>(i - prevIndex) / static_cast<double>(span);
    double value = static_cast<double>(prevSample) +
                   (static_cast<double>(nextSample) - prevSample) * t;
    int32_t rounded = static_cast<int32_t>(std::lrint(value));
    if (rounded < -32768) {
      rounded = -32768;
    } else if (rounded > 32767) {
      rounded = 32767;
    }
    buffer[i] = static_cast<int16_t>(rounded);
  }
}

} // namespace

TEST_CASE("Audio stream fills gaps with interpolation") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "continuity_gap.rbt";
  fs::path outDir = tmpDir / "continuity_gap_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  std::vector<uint8_t> primerData = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA,
                                     0xDC, 0xFE, 0x13, 0x57, 0x9B, 0xDF};
  auto expectedPrimer = decompress_without_runway(primerData);

  std::vector<uint8_t> blockPayloadA = {0x21, 0x43, 0x65, 0x87,
                                        0xA9, 0xCB, 0xED, 0x0F};
  std::vector<uint8_t> blockDataA(primerData.end() - kRunwayBytes,
                                  primerData.end());
  blockDataA.insert(blockDataA.end(), blockPayloadA.begin(), blockPayloadA.end());
  auto expectedBlockA = decompress_without_runway(blockDataA);

  std::vector<uint8_t> blockPayloadB = {0x11, 0x33, 0x55, 0x77,
                                        0x99, 0xBB, 0xDD, 0xFF};
  std::vector<uint8_t> blockDataB(primerData.begin(), primerData.begin() +
                                                        static_cast<std::ptrdiff_t>(kRunwayBytes));
  blockDataB.insert(blockDataB.end(), blockPayloadB.begin(), blockPayloadB.end());
  auto expectedBlockB = decompress_without_runway(blockDataB);

  const uint32_t primerReserved =
      static_cast<uint32_t>(kPrimerHeaderSize + primerData.size());
  auto data = build_header(static_cast<uint16_t>(primerReserved), 2);
  auto primerHeader =
      build_primer_header(kPrimerHeaderSize + static_cast<uint32_t>(primerData.size()),
                          static_cast<uint32_t>(primerData.size()), 0);
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  data.insert(data.end(), primerData.begin(), primerData.end());

  push16(data, 2);
  push16(data, 2);
  push16(data, static_cast<uint16_t>(2 + kAudioBlockSize));
  push16(data, static_cast<uint16_t>(2 + kAudioBlockSize));

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  data.push_back(0);
  data.push_back(0);

  const size_t primerSamples = expectedPrimer.size();
  const size_t blockASamples = expectedBlockA.size();
  const size_t blockBSamples = expectedBlockB.size();
  const size_t gapSamples = 4;

  const uint32_t blockAPos = static_cast<uint32_t>(primerSamples * 2);
  push32(data, blockAPos);
  push32(data, static_cast<uint32_t>(blockDataA.size()));
  data.insert(data.end(), blockDataA.begin(), blockDataA.end());

  data.push_back(0);
  data.push_back(0);

  const uint32_t blockBPos =
      static_cast<uint32_t>((primerSamples + blockASamples + gapSamples) * 2);
  push32(data, blockBPos);
  push32(data, static_cast<uint32_t>(blockDataB.size()));
  data.insert(data.end(), blockDataB.begin(), blockDataB.end());

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  auto wavPath = outDir / "frame_00000.wav";
  REQUIRE(fs::exists(wavPath));
  auto stereoSamples = read_wav_samples(wavPath);

  const size_t blockAStart = primerSamples;
  const size_t blockBStart = primerSamples + blockASamples + gapSamples;
  const size_t totalSamples = blockBStart + blockBSamples;
  std::vector<int16_t> expected(totalSamples, 0);
  if (!expectedPrimer.empty()) {
    std::copy(expectedPrimer.begin(), expectedPrimer.end(), expected.begin());
  }
  if (!expectedBlockA.empty()) {
    std::copy(expectedBlockA.begin(), expectedBlockA.end(),
              expected.begin() + static_cast<std::ptrdiff_t>(blockAStart));
  }
  if (gapSamples > 0 && !expectedBlockA.empty() && !expectedBlockB.empty()) {
    const size_t gapStart = blockAStart + blockASamples;
    const size_t gapEnd = gapStart + gapSamples;
    expected.resize(std::max(expected.size(), gapEnd));
    fill_gap(expected, gapStart, gapEnd, expectedBlockA.back(),
             expectedBlockB.front());
  }
  if (!expectedBlockB.empty()) {
    std::copy(expectedBlockB.begin(), expectedBlockB.end(),
              expected.begin() + static_cast<std::ptrdiff_t>(blockBStart));
  }

  REQUIRE(stereoSamples.even == expected);
  REQUIRE(stereoSamples.odd.size() == stereoSamples.even.size());
}

TEST_CASE("Audio blocks honor absolute positions when reordered") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "continuity_reorder.rbt";
  fs::path outDir = tmpDir / "continuity_reorder_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  std::vector<uint8_t> primerData = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA,
                                     0xDC, 0xFE};
  auto expectedPrimer = decompress_without_runway(primerData);

  std::vector<uint8_t> lowRunway = {0x08, 0x18, 0x28, 0x38,
                                    0x48, 0x58, 0x68, 0x78};
  std::vector<uint8_t> lowPayload = {0x20, 0x40, 0x60, 0x80,
                                     0xA0, 0xC0, 0xE0, 0x00};
  std::vector<uint8_t> blockLowData = lowRunway;
  blockLowData.insert(blockLowData.end(), lowPayload.begin(), lowPayload.end());
  auto expectedLow = decompress_without_runway(blockLowData);

  std::vector<uint8_t> highRunway = {0x11, 0x21, 0x31, 0x41,
                                     0x51, 0x61, 0x71, 0x81};
  std::vector<uint8_t> highPayload = {0x15, 0x35, 0x55, 0x75,
                                      0x95, 0xB5, 0xD5, 0xF5};
  std::vector<uint8_t> blockHighData = highRunway;
  blockHighData.insert(blockHighData.end(), highPayload.begin(),
                       highPayload.end());
  auto expectedHigh = decompress_without_runway(blockHighData);

  const uint32_t primerReserved =
      static_cast<uint32_t>(kPrimerHeaderSize + primerData.size());
  auto data = build_header(static_cast<uint16_t>(primerReserved), 2);
  auto primerHeader =
      build_primer_header(kPrimerHeaderSize + static_cast<uint32_t>(primerData.size()),
                          static_cast<uint32_t>(primerData.size()), 0);
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  data.insert(data.end(), primerData.begin(), primerData.end());

  push16(data, 2);
  push16(data, 2);
  push16(data, static_cast<uint16_t>(2 + kAudioBlockSize));
  push16(data, static_cast<uint16_t>(2 + kAudioBlockSize));

  for (int i = 0; i < 256; ++i)
    push32(data, 0);
  for (int i = 0; i < 256; ++i)
    push16(data, 0);

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  data.push_back(0);
  data.push_back(0);

  const size_t primerSamples = expectedPrimer.size();
  const size_t lowStartSample = primerSamples + 1;
  const size_t lowSamples = expectedLow.size();
  const size_t highStartSample = lowStartSample + lowSamples;

  const uint32_t highPos = static_cast<uint32_t>(highStartSample * 2);
  push32(data, highPos);
  push32(data, static_cast<uint32_t>(blockHighData.size()));
  data.insert(data.end(), blockHighData.begin(), blockHighData.end());

  data.push_back(0);
  data.push_back(0);

  const uint32_t lowPos = static_cast<uint32_t>(lowStartSample * 2);
  push32(data, lowPos);
  push32(data, static_cast<uint32_t>(blockLowData.size()));
  data.insert(data.end(), blockLowData.begin(), blockLowData.end());

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  auto wavPath = outDir / "frame_00000.wav";
  REQUIRE(fs::exists(wavPath));
  auto stereoSamples = read_wav_samples(wavPath);

  const size_t totalSamples = highStartSample + expectedHigh.size();
  std::vector<int16_t> expected(totalSamples, 0);
  if (!expectedPrimer.empty()) {
    std::copy(expectedPrimer.begin(), expectedPrimer.end(), expected.begin());
  }
  if (!expectedLow.empty()) {
    std::copy(expectedLow.begin(), expectedLow.end(),
              expected.begin() + static_cast<std::ptrdiff_t>(lowStartSample));
  }
  if (!expectedHigh.empty()) {
    std::copy(expectedHigh.begin(), expectedHigh.end(),
              expected.begin() + static_cast<std::ptrdiff_t>(highStartSample));
  }

  REQUIRE(stereoSamples.even == expected);
  REQUIRE(stereoSamples.odd.size() == stereoSamples.even.size());
}
