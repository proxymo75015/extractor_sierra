#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <vector>

#include "audio_decompression_helpers.hpp"
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

std::optional<size_t> find_alignment(const std::vector<int16_t> &stream,
                                     const std::vector<int16_t> &samples) {
  if (samples.empty()) {
    return std::nullopt;
  }
  if (stream.size() < samples.size()) {
    return std::nullopt;
  }
  for (size_t start = 0; start + samples.size() <= stream.size(); ++start) {
    bool match = true;
    for (size_t i = 0; i < samples.size(); ++i) {
      if (stream[start + i] != samples[i]) {
        match = false;
        break;
      }
    }
    if (match) {
      return start;
    }
  }
  return std::nullopt;
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
  int16_t predictor = 0;
  auto primerSamples =
      audio_test::decompress_primer(primerData, predictor);
  REQUIRE(primerSamples.size() == 3);

  BlockInfo block0;
  block0.position = 4; // requires alternate offset
  block0.raw = {0, 0, 0, 0, 0, 0, 0, 0, 20, 10, 10, 10, 10, 10, 10, 10};
  {
    int16_t blockPredictor = 0;
    block0.samples =
        audio_test::decompress_without_runway(block0.raw, blockPredictor);
  }
  REQUIRE(block0.samples.size() == 8);

  BlockInfo block1;
  block1.position = 20;
  block1.raw = {0, 0, 0, 0, 0, 0, 0, 0, 100, 10, 10, 10, 10, 10, 10, 10};
  {
    int16_t blockPredictor = 0;
    block1.samples =
        audio_test::decompress_without_runway(block1.raw, blockPredictor);
  }
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
  
  const auto evenStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  const auto oddStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);
  REQUIRE(evenStream.size() + oddStream.size() > 0);

  struct ChannelExpectation {
    bool initialized = false;
    int64_t startHalfPos = 0;
    std::vector<int16_t> samples;
  };

  auto appendExpected = [](ChannelExpectation &channel, int32_t halfPos,
                           const std::vector<int16_t> &samples) {
    if (samples.empty()) {
      return;
    }
    int64_t pos = static_cast<int64_t>(halfPos);
    if (!channel.initialized) {
      channel.startHalfPos = pos;
      channel.initialized = true;
    } else if (pos < channel.startHalfPos) {
      const int64_t deltaHalf = channel.startHalfPos - pos;
      if ((deltaHalf & 1LL) != 0) {
        throw std::runtime_error("Parity mismatch in expected audio layout");
      }
      const size_t deltaSamples = static_cast<size_t>(deltaHalf / 2);
      channel.samples.insert(channel.samples.begin(), deltaSamples, 0);
      channel.startHalfPos = pos;
    }
    int64_t adjustedHalf = pos - channel.startHalfPos;
    int64_t startSampleSigned =
        adjustedHalf >= 0 ? adjustedHalf / 2 : (adjustedHalf - 1) / 2;
    size_t skip = 0;
    if (startSampleSigned < 0) {
      skip = static_cast<size_t>(-startSampleSigned);
      if (skip >= samples.size()) {
        return;
      }
      startSampleSigned = 0;
    }
    const size_t startSample = static_cast<size_t>(startSampleSigned);
    const size_t available = samples.size() - skip;
    const size_t requiredSize = startSample + available;
    if (channel.samples.size() < requiredSize) {
      channel.samples.resize(requiredSize, 0);
    }
    std::copy(samples.begin() + static_cast<std::ptrdiff_t>(skip),
              samples.end(),
              channel.samples.begin() + static_cast<std::ptrdiff_t>(startSample));
  };

  ChannelExpectation evenExpected;
  ChannelExpectation oddExpected;
  appendExpected(evenExpected, 0, primerSamples);

  for (const auto &block : blocks) {
    if (block.samples.empty()) {
      continue;
    }
    const bool shouldBeEven = (block.position % 2) == 0;
    ChannelExpectation &target = shouldBeEven ? evenExpected : oddExpected;
    appendExpected(target, block.position, block.samples);
    auto evenIndex = find_alignment(evenStream, block.samples);
    auto oddIndex = find_alignment(oddStream, block.samples);
    if (shouldBeEven) {
      REQUIRE(evenIndex.has_value());
      REQUIRE_FALSE(oddIndex.has_value());
    } else {
      REQUIRE(oddIndex.has_value());
      REQUIRE_FALSE(evenIndex.has_value());
    }
  }

  REQUIRE(evenStream == evenExpected.samples);
  REQUIRE(oddStream == oddExpected.samples);  
}
