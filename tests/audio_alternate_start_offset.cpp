#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <vector>

#include "robot_extractor.hpp"

namespace fs = std::filesystem;

namespace {
constexpr uint16_t kNumFrames = 2;
constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr uint16_t kAudioBlockSize = 24;
constexpr size_t kBlockDataBytes =
    static_cast<size_t>(kAudioBlockSize) - robot::kRobotAudioHeaderSize;
constexpr size_t kRunwaySamples = robot::kRobotRunwaySamples;

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

std::vector<int16_t>
decompress_without_runway(const std::vector<uint8_t> &rawBytes) {
  std::vector<std::byte> bytes(rawBytes.size());
  for (size_t i = 0; i < rawBytes.size(); ++i) {
    bytes[i] = std::byte{rawBytes[i]};
  }
  int16_t predictor = 0;
  auto samples = robot::dpcm16_decompress(std::span(bytes), predictor);
  if (samples.size() <= kRunwaySamples) {
    return {};
  }
  return {samples.begin() + static_cast<std::ptrdiff_t>(kRunwaySamples),
          samples.end()};
}

std::vector<uint8_t> build_header(uint16_t primerReserved) {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);                // version
  push16(h, kAudioBlockSize);  // audio block size
  push16(h, 0);                // primerZeroCompressFlag
  push16(h, 0);                // skip
  push16(h, kNumFrames);       // numFrames
  push16(h, 0);                // paletteSize
  push16(h, primerReserved);   // primerReservedSize
  push16(h, 1);                // xRes
  push16(h, 1);                // yRes
  h.push_back(0);              // hasPalette
  h.push_back(1);              // hasAudio
  push16(h, 0);                // skip
  push16(h, 60);               // frameRate
  push16(h, 0);                // isHiRes
  push16(h, 0);                // maxSkippablePackets
  push16(h, 1);                // maxCelsPerFrame
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

int64_t compute_start_sample(int32_t pos, int64_t offsetHalf) {
  int64_t halfPos = static_cast<int64_t>(pos) - offsetHalf;
  int64_t startSampleSigned = 0;
  if (halfPos >= 0) {
    startSampleSigned = halfPos / 2;
  } else {
    startSampleSigned = (halfPos - 1) / 2;
  }
  return startSampleSigned;
}

struct BlockInfo {
  int32_t position = 0;
  std::vector<uint8_t> raw;
  std::vector<int16_t> samples;
};

} // namespace

TEST_CASE("Alternate audio start offset persists across blocks") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "audio_alt_offset.rbt";
  fs::path outDir = tmpDir / "audio_alt_offset_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  std::vector<uint8_t> primerData = {0, 0, 0, 0, 0, 0, 0, 0, 10, 10, 10};
  auto primerSamples = decompress_without_runway(primerData);
  REQUIRE(primerSamples.size() == 3);

  BlockInfo block0;
  block0.position = 4; // requires alternate offset
  block0.raw = {0, 0, 0, 0, 0, 0, 0, 0, 20, 10, 10, 10, 10, 10, 10, 10};
  block0.samples = decompress_without_runway(block0.raw);
  REQUIRE(block0.samples.size() == 8);

  BlockInfo block1;
  block1.position = 20;
  block1.raw = {0, 0, 0, 0, 0, 0, 0, 0, 100, 10, 10, 10, 10, 10, 10, 10};
  block1.samples = decompress_without_runway(block1.raw);
  REQUIRE(block1.samples.size() == 8);

  const uint32_t primerTotal =
      static_cast<uint32_t>(kPrimerHeaderSize + primerData.size());
  auto data = build_header(static_cast<uint16_t>(primerTotal));
  auto primerHeader =
      build_primer_header(primerTotal, static_cast<uint32_t>(primerData.size()),
                          0);
  data.insert(data.end(), primerHeader.begin(), primerHeader.end());
  data.insert(data.end(), primerData.begin(), primerData.end());

  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 2); // frame size without audio
  }
  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, static_cast<uint16_t>(2 + kAudioBlockSize));
  }

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  const std::array<BlockInfo, 2> blocks{block0, block1};
  for (const auto &block : blocks) {
    data.push_back(0);
    data.push_back(0);
    push32(data, static_cast<uint32_t>(block.position));
    push32(data, static_cast<uint32_t>(block.raw.size()));
    data.insert(data.end(), block.raw.begin(), block.raw.end());
    REQUIRE(block.raw.size() <= kBlockDataBytes);
    const size_t padding = kBlockDataBytes - block.raw.size();
    data.insert(data.end(), padding, 0);
  }

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());
  
  REQUIRE(robot::RobotExtractorTester::audioStartOffsetInitialized(extractor));
  const int64_t audioStartOffset =
      robot::RobotExtractorTester::audioStartOffset(extractor);
  REQUIRE(audioStartOffset % 4 == 0);
  const int64_t offsetHalf = audioStartOffset / 2;
  CAPTURE(audioStartOffset);
  CAPTURE(offsetHalf);

  auto computeAlternateHalf = [](int64_t baseOffset) {
    int64_t remainder = baseOffset % 4;
    if (remainder < 0) {
      remainder += 4;
    }
    if (remainder == 0) {
      return (baseOffset + 2) / 2;
    }
    if (remainder == 2) {
      return (baseOffset - 2) / 2;
    }
    FAIL("Offset audio inattendu");
    return baseOffset / 2;
  };
  const int64_t alternateOffsetHalf = computeAlternateHalf(audioStartOffset);

  const auto evenStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  REQUIRE_FALSE(evenStream.empty());

  std::vector<int16_t> expected = primerSamples;

  for (const auto &block : blocks) {
    if (block.samples.empty()) {
      continue;
    }
    auto evaluateAlignment = [&](int64_t candidateHalf)
        -> std::optional<size_t> {
      const int64_t startSampleSigned =
          compute_start_sample(block.position, candidateHalf);
      if (startSampleSigned < 0) {
        return std::nullopt;
      }
      const size_t startIndex = static_cast<size_t>(startSampleSigned);
      if (startIndex > evenStream.size()) {
        return std::nullopt;
      }
      const size_t requiredSize = startIndex + block.samples.size();
      if (requiredSize > evenStream.size()) {
        return std::nullopt;
      }
      for (size_t i = 0; i < block.samples.size(); ++i) {
        if (evenStream[startIndex + i] != block.samples[i]) {
          return std::nullopt;
        }
      }
      return startIndex;
    };

    std::optional<size_t> startIndexOpt = evaluateAlignment(offsetHalf);
    if (!startIndexOpt) {
      startIndexOpt = evaluateAlignment(alternateOffsetHalf);
    }
    REQUIRE(startIndexOpt.has_value());
    const size_t startIndex = *startIndexOpt;
    if (expected.size() < startIndex) {
      const size_t previousSize = expected.size();
      expected.resize(startIndex, 0);
      for (size_t i = previousSize; i < startIndex; ++i) {
        expected[i] = evenStream[i];
      }
    }
    const size_t requiredSize = startIndex + block.samples.size();
    if (expected.size() < requiredSize) {
      expected.resize(requiredSize, 0);
    }
    for (size_t i = 0; i < block.samples.size(); ++i) {
      expected[startIndex + i] = block.samples[i];
    }
  }

  REQUIRE(evenStream == expected);

  const auto oddStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);
  REQUIRE(oddStream.empty());
}
