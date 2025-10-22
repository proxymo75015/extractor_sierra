#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "audio_decompression_helpers.hpp"
#include "robot_extractor.hpp"
#include "wav_helpers.hpp"

namespace fs = std::filesystem;

constexpr uint16_t kNumFrames = 4;
constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr size_t kZeroPrefixBytes = robot::kRobotZeroCompressSize;
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

static std::vector<int16_t>
decompress_truncated_block(const std::vector<uint8_t> &raw,
                           int16_t & /*predictor*/) {
  std::vector<std::byte> block(kZeroPrefixBytes + raw.size(), std::byte{0});
  for (size_t i = 0; i < raw.size(); ++i) {
    block[kZeroPrefixBytes + i] = std::byte{raw[i]};
  }
  int16_t predictor = 0;  
  return audio_test::decompress_without_runway(block, predictor);
}

struct StereoSamples {
  std::vector<int16_t> even;
  std::vector<int16_t> odd;
};

static StereoSamples read_wav_samples(const fs::path &path) {
  std::ifstream wav(path, std::ios::binary);
  REQUIRE(wav);
  auto layout = read_wav_layout(wav);
  REQUIRE(layout.dataSize % (layout.numChannels * sizeof(int16_t)) == 0);
  std::vector<int16_t> interleaved(layout.dataSize / sizeof(int16_t));
  if (!interleaved.empty()) {
    wav.read(reinterpret_cast<char *>(interleaved.data()),
             static_cast<std::streamsize>(layout.dataSize));
    REQUIRE(wav.gcount() == static_cast<std::streamsize>(layout.dataSize));
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

static std::vector<uint8_t> build_header() {
  std::vector<uint8_t> h;
  push16(h, 0x16); // signature
  h.insert(h.end(), {'S', 'O', 'L', '\0'});
  push16(h, 5);   // version
  push16(h, 24);  // audio block size
  push16(h, 0);   // primerZeroCompressFlag
  push16(h, 0);   // skip
  push16(h, kNumFrames); // numFrames
  push16(h, 0);   // paletteSize
  push16(h, static_cast<uint16_t>(kPrimerHeaderSize));
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
  push32(p, kPrimerHeaderSize);
  push16(p, 0); // compType
  push32(p, 0); // even size
  push32(p, 0); // odd size
  return p;
}

TEST_CASE("Audio blocks are routed according to parity") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "odd_channel_route.rbt";
  fs::path outDir = tmpDir / "odd_channel_route_out";
  fs::create_directories(outDir);

  auto data = build_header();
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());

  // Frame sizes and packet sizes including audio blocks
  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 2);
  }
  for (uint16_t i = 0; i < kNumFrames; ++i) {
    push16(data, 26);
  }
  
  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  // Pad to 2048-byte boundary
  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  // Quatre frames audio dont deux avec position paire (canal pair) et deux
  // avec position impaire (canal impair).
  struct BlockInfo {
    uint32_t position = 0;
    bool even = false;
    std::vector<int16_t> samples;
  };

  std::array<uint8_t, kNumFrames> payloadBases = {0x00, 0x10, 0x20, 0x30};
  uint32_t nextEvenPos = 2;
  uint32_t nextOddPos = 3;
  std::vector<BlockInfo> blocks;
  blocks.reserve(kNumFrames);

  int16_t evenPredictor = 0;
  int16_t oddPredictor = 0;  
  for (size_t idx = 0; idx < payloadBases.size(); ++idx) {
    const bool isEven = (idx % 2) == 0;
    std::vector<uint8_t> raw(10);
    for (int i = 0; i < 10; ++i) {
      raw[static_cast<size_t>(i)] = static_cast<uint8_t>(payloadBases[idx] + i);
    }
    auto &predictor = isEven ? evenPredictor : oddPredictor;
    auto samples = decompress_truncated_block(raw, predictor);
    const uint32_t pos = isEven ? nextEvenPos : nextOddPos;

    data.push_back(0);
    data.push_back(0);
    push32(data, pos);
    push32(data, static_cast<uint32_t>(raw.size()));
    data.insert(data.end(), raw.begin(), raw.end());
    data.insert(data.end(), 6, 0); // padding to expected block length

    BlockInfo info;
    info.position = pos;
    info.even = isEven;
    info.samples = std::move(samples);
    const size_t sampleCount = info.samples.size();
    blocks.push_back(std::move(info));

    const uint32_t advance = static_cast<uint32_t>(sampleCount * 2 + 2);
    if (isEven) {
      nextEvenPos += advance;
    } else {
      nextOddPos += advance;
    }
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

  audio_test::ChannelExpectation evenExpected;
  audio_test::ChannelExpectation oddExpected;
  for (const auto &block : blocks) {
    auto &target = block.even ? evenExpected : oddExpected;
    audio_test::append_expected(target,
                                static_cast<int32_t>(block.position),
                                block.samples);
  }

  REQUIRE(evenStream == evenExpected.samples);
  REQUIRE(oddStream == oddExpected.samples);

  const auto stereoPath = outDir / "frame_00000.wav";
  REQUIRE(fs::exists(stereoPath));
  REQUIRE_FALSE(fs::exists(outDir / "frame_00001.wav"));

  auto stereoSamples = read_wav_samples(stereoPath);
  REQUIRE(stereoSamples.even == evenStream);
  REQUIRE(stereoSamples.odd == oddStream);
}
