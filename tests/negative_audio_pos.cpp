#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

#include "robot_extractor.hpp"
#include "utilities.hpp"

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr uint32_t kRunwayBytes = static_cast<uint32_t>(robot::kRobotRunwayBytes);

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
  push16(h, 24);  // audio block size
  push16(h, 0);   // primerZeroCompressFlag
  push16(h, 0);   // skip
  push16(h, 1);   // numFrames
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
  push32(p, kPrimerHeaderSize + kRunwayBytes);
  push16(p, 0); // compType
  push32(p, kRunwayBytes); // even size
  push32(p, 0); // odd size
  return p;
}

static std::vector<std::byte> to_bytes(const std::vector<uint8_t> &src) {
  std::vector<std::byte> out;
  out.reserve(src.size());
  for (uint8_t b : src) {
    out.push_back(static_cast<std::byte>(b));
  }
  return out;
}

static std::vector<int16_t>
decode_block_without_runway(const std::vector<uint8_t> &blockData) {
  auto bytes = to_bytes(blockData);
  int16_t predictor = 0;
  auto decoded = robot::dpcm16_decompress(std::span(bytes), predictor);
  std::vector<int16_t> samples;
  const size_t runwaySamples = robot::kRobotRunwayBytes;
  if (decoded.size() > runwaySamples) {
    samples.assign(decoded.begin() +
                       static_cast<std::ptrdiff_t>(runwaySamples),
                   decoded.end());
  }
  return samples;
}

static std::vector<int16_t> expected_stream_for_block(
    const std::vector<uint8_t> &blockData, int32_t pos) {
  auto samples = decode_block_without_runway(blockData);
  if (samples.empty()) {
    return samples;
  }
  int64_t startHalf = static_cast<int64_t>(pos);
  int64_t startSampleSigned = 0;
  if (startHalf >= 0) {
    startSampleSigned = startHalf / 2;
  } else {
    startSampleSigned = (startHalf - 1) / 2;
  }
  if (startSampleSigned < 0) {
    size_t skip = static_cast<size_t>(-startSampleSigned);
    if (skip >= samples.size()) {
      samples.clear();
    } else {
      samples.erase(samples.begin(),
                    samples.begin() + static_cast<std::ptrdiff_t>(skip));
    }
  }
  return samples;
}

TEST_CASE("Negative audio position is ignored") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "neg_audio_pos.rbt";
  fs::path outDir = tmpDir / "neg_audio_pos_out";
  fs::create_directories(outDir);

  auto data = build_header();
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());
  for (uint32_t i = 0; i < kRunwayBytes; ++i)
    data.push_back(0x88); // even primer data

  push16(data, 2);  // frame size
  push16(data, 26); // packet size

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  data.resize(((data.size() + 2047) / 2048) * 2048,
              0); // pad to 2048-byte boundary

  // Frame data (numCels = 0)
  data.push_back(0); // numCels low byte
  data.push_back(0); // numCels high byte

  push32(data, static_cast<uint32_t>(-2)); // negative pos
  push32(data, 10);                        // size (unused, but valid)
  for (uint32_t i = 0; i < kRunwayBytes; ++i)
    data.push_back(0); // runway
  for (int i = 0; i < 2; ++i)
    data.push_back(0); // audio data
  for (int i = 0; i < 6; ++i)
    data.push_back(0); // padding

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());
}

TEST_CASE("Audio block with position -1 is adjusted without corruption") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "neg_audio_pos_minus1.rbt";
  fs::path outDir = tmpDir / "neg_audio_pos_minus1_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }
  fs::create_directories(outDir);

  auto header = build_header();
  auto primer = build_primer_header();
  header.insert(header.end(), primer.begin(), primer.end());
  for (uint32_t i = 0; i < kRunwayBytes; ++i) {
    header.push_back(static_cast<uint8_t>(0x20 + (i * 3))); // even primer
  }

  push16(header, 2);        // frame size
  push16(header, 26);       // packet size (frame + audio block 24)

  for (int i = 0; i < 256; ++i)
    push32(header, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(header, 0); // cue values

  header.resize(((header.size() + 2047) / 2048) * 2048, 0);

  header.push_back(0); // numCels low
  header.push_back(0); // numCels high

  std::vector<uint8_t> blockData = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
                                    0xDE, 0xF0};
  std::vector<uint8_t> runway;
  runway.reserve(kRunwayBytes);
  for (uint32_t i = 0; i < kRunwayBytes; ++i) {
    runway.push_back(static_cast<uint8_t>(0x40 + i));
  }
  std::vector<uint8_t> fullBlock(runway);
  fullBlock.insert(fullBlock.end(), blockData.begin(), blockData.end());

  push32(header, static_cast<uint32_t>(-1));
  push32(header, static_cast<uint32_t>(fullBlock.size()));
  header.insert(header.end(), fullBlock.begin(), fullBlock.end());

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(header.data()),
            static_cast<std::streamsize>(header.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  auto expectedOdd = expected_stream_for_block(fullBlock, -1);
  auto evenStream = robot::RobotExtractorTester::buildChannelStream(extractor, true);
  auto oddStream = robot::RobotExtractorTester::buildChannelStream(extractor, false);

  REQUIRE(evenStream.empty());
  REQUIRE(oddStream == expectedOdd);
}
