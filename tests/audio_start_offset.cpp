#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
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
constexpr size_t kZeroPrefixBytes = robot::kRobotZeroCompressSize;
constexpr size_t kRunwaySamples = robot::kRobotRunwaySamples;
constexpr size_t kBlockDataBytes =
    static_cast<size_t>(kAudioBlockSize) - robot::kRobotAudioHeaderSize;

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
decompress_primer_block(const std::vector<uint8_t> &rawBytes) {
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
  push16(h, 5);   // version
  push16(h, kAudioBlockSize);  // audio block size
  push16(h, 0);   // primerZeroCompressFlag
  push16(h, 0);   // skip
  push16(h, kNumFrames); // numFrames
  push16(h, 0);          // paletteSize
  push16(h, primerReserved);
  push16(h, 1); // xRes
  push16(h, 1); // yRes
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

std::vector<uint8_t> build_primer_header(uint32_t total, uint32_t evenSize,
                                         uint32_t oddSize) {
  std::vector<uint8_t> p;
  push32(p, total);
  push16(p, 0); // compType
  push32(p, evenSize);
  push32(p, oddSize);
  return p;
}

std::vector<int16_t> decompress_truncated_block(
    const std::vector<uint8_t> &raw) {
  std::vector<std::byte> block(kZeroPrefixBytes + raw.size(), std::byte{0});
  for (size_t i = 0; i < raw.size(); ++i) {
    block[kZeroPrefixBytes + i] = std::byte{raw[i]};
  }
  int16_t predictor = 0;
  auto samples = robot::dpcm16_decompress(std::span(block), predictor);
  if (samples.size() <= kRunwaySamples) {
    return {};
  }
  return {samples.begin() + static_cast<std::ptrdiff_t>(kRunwaySamples),
          samples.end()};
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

TEST_CASE("Audio start offset routed using doubled positions") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "audio_start_offset.rbt";
  fs::path outDir = tmpDir / "audio_start_offset_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  std::vector<uint8_t> primerData = {0, 0, 0, 0, 0, 0, 0, 0, 10, 20, 30};
  auto primerSamples = decompress_primer_block(primerData);
  REQUIRE(primerSamples.size() >= 2);
  auto data = build_header(static_cast<uint16_t>(kPrimerHeaderSize +
                                                 primerData.size()));
  auto primer =
      build_primer_header(kPrimerHeaderSize + static_cast<uint32_t>(primerData.size()),
                          0, static_cast<uint32_t>(primerData.size()));
  data.insert(data.end(), primer.begin(), primer.end());
  data.insert(data.end(), primerData.begin(), primerData.end());

  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 2); // frame size without audio
  }
  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 26); // packet size includes audio block
  }

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  std::array<BlockInfo, 2> blocks;
  blocks[0].position = 3;
  blocks[0].raw = {0x12, 0x34, 0x56, 0x78, 0x9A};
  blocks[0].samples = decompress_truncated_block(blocks[0].raw);
  blocks[1].position = 4096;
  blocks[1].raw = {0xAB, 0xCD, 0xEF, 0x10, 0x24};
  blocks[1].samples = decompress_truncated_block(blocks[1].raw);

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
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readHeader(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readPrimer(extractor));
  REQUIRE_NOTHROW(robot::RobotExtractorTester::readSizesAndCues(extractor));

  for (const auto &block : blocks) {
    std::vector<std::byte> payload(kZeroPrefixBytes + block.raw.size(),
                                   std::byte{0});
    for (size_t i = 0; i < block.raw.size(); ++i) {
      payload[kZeroPrefixBytes + i] = std::byte{block.raw[i]};
    }
    CAPTURE(block.position);
    REQUIRE_NOTHROW(robot::RobotExtractorTester::processAudioBlock(
        extractor, payload, block.position));
  }

  REQUIRE(robot::RobotExtractorTester::audioStartOffsetInitialized(extractor));
  const int64_t audioStartOffset =
      robot::RobotExtractorTester::audioStartOffset(extractor);
  REQUIRE((audioStartOffset & 1LL) == 0);
  REQUIRE(((audioStartOffset % 4) + 4) % 4 == 2);

  const auto evenStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);
  const auto oddStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, false);

  REQUIRE_FALSE(evenStream.empty());
  REQUIRE_FALSE(oddStream.empty());
  CAPTURE(evenStream.size());
  CAPTURE(oddStream.size());

  const auto &firstBlock = blocks[0];
  REQUIRE_FALSE(firstBlock.samples.empty());
  auto firstEvenIndex = find_alignment(evenStream, firstBlock.samples);
  auto firstOddIndex = find_alignment(oddStream, firstBlock.samples);
  REQUIRE(firstEvenIndex.has_value());
  REQUIRE_FALSE(firstOddIndex.has_value());
  
  for (const auto &block : blocks) {
    const auto &samples = block.samples;
    if (samples.empty()) {
      continue;
    }
    CAPTURE(samples.size());
    auto evenIndex = find_alignment(evenStream, samples);
    auto oddIndex = find_alignment(oddStream, samples);
    REQUIRE(evenIndex.has_value() != oddIndex.has_value());
    const bool isEven = evenIndex.has_value();
    const size_t startSample = isEven ? *evenIndex : *oddIndex;
    const auto &stream = isEven ? evenStream : oddStream;
    for (size_t i = 0; i < samples.size(); ++i) {
      CAPTURE(block.position);
      CAPTURE(startSample);
      CAPTURE(i);
      REQUIRE(stream[startSample + i] == samples[i]);
    }
  }
}
