#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "audio_decompression_helpers.hpp"
#include "robot_extractor.hpp"
#include "utilities.hpp"

namespace fs = std::filesystem;

namespace {

constexpr uint16_t kNumFrames = 3;
constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);

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

std::vector<uint8_t> build_header(uint16_t numFrames) {
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

std::vector<uint8_t> build_primer_header() {
  std::vector<uint8_t> p;
  push32(p, kPrimerHeaderSize +
                 static_cast<uint32_t>(robot::kRobotRunwayBytes));
  push16(p, 0); // compType
  push32(p, static_cast<uint32_t>(robot::kRobotRunwayBytes)); // even size
  push32(p, 0); // odd size
  return p;
}

std::vector<int16_t> decode_block(const std::vector<uint8_t> &bytes,
                                  int16_t &predictor) {
  return audio_test::decompress_without_runway(bytes, predictor);
}

std::vector<uint8_t> build_block(
    const std::array<uint8_t, robot::kRobotRunwayBytes> &runway,
    const std::array<uint8_t, robot::kRobotRunwayBytes> &tail) {
  std::vector<uint8_t> block;
  block.reserve(runway.size() + tail.size());
  block.insert(block.end(), runway.begin(), runway.end());
  block.insert(block.end(), tail.begin(), tail.end());
  return block;
}

} // namespace

TEST_CASE("Conflicting retransmission overwrites while parity mismatch is ignored") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "audio_conflict_parity.rbt";
  fs::path outDir = tmpDir / "audio_conflict_parity_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  auto data = build_header(kNumFrames);
  auto primerHeader = build_primer_header();
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  std::array<uint8_t, robot::kRobotRunwayBytes> primerEven{};
  primerEven.fill(0x88);
  data.insert(data.end(), primerEven.begin(), primerEven.end());
  int16_t predictor = 0;
  std::vector<uint8_t> primerBytes(primerEven.begin(), primerEven.end());
  audio_test::decompress_without_runway(primerBytes, predictor);  

  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 2); // frame size placeholder
  }
  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 26); // packet size placeholder
  }
  for (int i = 0; i < 256; ++i) {
    push32(data, 0);
  }
  for (int i = 0; i < 256; ++i) {
    push16(data, 0);
  }

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  {
    std::ofstream out(input, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
  }

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readHeader(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readPrimer(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readSizesAndCues(extractor));

  auto baselineEven =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  auto baselineOdd =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);

  const std::array<uint8_t, robot::kRobotRunwayBytes> runwayBlock1 = {
      0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87};
  const std::array<uint8_t, robot::kRobotRunwayBytes> block1Tail = {
      0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88};
  const std::array<uint8_t, robot::kRobotRunwayBytes> conflictTail = {
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F};
  const std::array<uint8_t, robot::kRobotRunwayBytes> parityTail = {
      0xB8, 0xB7, 0xB6, 0xB5, 0xB4, 0xB3, 0xB2, 0xB1};

  auto block1 = build_block(runwayBlock1, block1Tail);
  auto conflictBlock = build_block(runwayBlock1, conflictTail);
  auto parityBlock = build_block(runwayBlock1, parityTail);

  auto processBlock = [&](const std::vector<uint8_t> &raw, int32_t pos) {
    std::vector<std::byte> bytes(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
      bytes[i] = std::byte{raw[i]};
    }
    robot::RobotExtractorTester::processAudioBlock(
        extractor, std::span<const std::byte>(bytes.data(), bytes.size()), pos);
  };

  constexpr int32_t block1Pos = 4;
  constexpr int32_t conflictPos = block1Pos;
  constexpr int32_t parityPos = -2;

  REQUIRE_NOTHROW(processBlock(block1, block1Pos));
  auto afterFirstEven =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  auto afterFirstOdd =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);

  auto expectedEvenAfterFirst = baselineEven;
  int64_t evenBaseSample = 0;
  bool evenBaseInitialized = !baselineEven.empty();
  auto updateEvenExpected = [&](std::vector<int16_t> &buffer,
                                int64_t startSample,
                                const std::vector<int16_t> &samples) {
    if (samples.empty()) {
      return;
    }
    if (!evenBaseInitialized) {
      evenBaseSample = startSample;
      evenBaseInitialized = true;
    }
    if (startSample < evenBaseSample) {
      const size_t shift = static_cast<size_t>(evenBaseSample - startSample);
      buffer.insert(buffer.begin(), shift, 0);
      evenBaseSample = startSample;
    }
    const size_t offset = static_cast<size_t>(startSample - evenBaseSample);
    if (buffer.size() < offset) {
      buffer.resize(offset, 0);
    }
    const size_t requiredSize = offset + samples.size();
    if (buffer.size() < requiredSize) {
      buffer.resize(requiredSize, 0);
    }
    for (size_t i = 0; i < samples.size(); ++i) {
      buffer[offset + i] = samples[i];
    }
  };
  auto block1Samples = decode_block(block1, predictor);
  REQUIRE_FALSE(block1Samples.empty());
  const int64_t block1Start = block1Pos / 2;
  updateEvenExpected(expectedEvenAfterFirst, block1Start, block1Samples);

  REQUIRE(afterFirstEven == expectedEvenAfterFirst);
  REQUIRE(afterFirstOdd == baselineOdd);

  REQUIRE_NOTHROW(processBlock(conflictBlock, conflictPos));
  auto afterConflictEven =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  auto afterConflictOdd =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);

  auto conflictSamples = decode_block(conflictBlock, predictor);
  REQUIRE_FALSE(conflictSamples.empty());
  auto expectedEvenAfterConflict = expectedEvenAfterFirst;
  updateEvenExpected(expectedEvenAfterConflict, block1Start, conflictSamples);
  
  REQUIRE(afterConflictEven == expectedEvenAfterConflict);
  REQUIRE(afterConflictOdd == baselineOdd);

  REQUIRE_NOTHROW(processBlock(parityBlock, parityPos));
  auto afterParityEven =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  auto afterParityOdd =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);
  int16_t predictorForParity = predictor;
  auto paritySamples = decode_block(parityBlock, predictorForParity);

  const int64_t parityStart = parityPos / 2;
  updateEvenExpected(expectedEvenAfterConflict, parityStart, paritySamples);
  
  REQUIRE(afterParityEven == expectedEvenAfterConflict);
  REQUIRE(afterParityOdd == baselineOdd);
}
