#include <algorithm>
#include <array>
#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>

#include "audio_decompression_helpers.hpp"
#include "robot_extractor.hpp"
#include "utilities.hpp"
#include "wav_helpers.hpp"

namespace fs = std::filesystem;

constexpr uint32_t kPrimerHeaderSize = sizeof(uint32_t) + sizeof(int16_t) +
                                       2 * sizeof(uint32_t);
constexpr size_t kZeroPrefixBytes = robot::kRobotZeroCompressSize;
constexpr size_t kRunwayBytes = robot::kRobotRunwayBytes;
constexpr size_t kRunwaySamples = robot::kRobotRunwayBytes;
constexpr size_t kTruncatedPayloadBytes = 2;
constexpr uint32_t kBlockPosHalfSamples = 4;

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
decompress_without_runway(const std::vector<std::byte> &bytes,
                          int16_t &predictor) {
  return audio_test::decompress_without_runway(bytes, predictor);
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
  push32(p, kPrimerHeaderSize +
                     static_cast<uint32_t>(kRunwayBytes));
  push16(p, 0); // compType
  push32(p, static_cast<uint32_t>(kRunwayBytes)); // even size
  push32(p, 0); // odd size
  return p;
}

TEST_CASE("Truncated audio block triggers error") {
  fs::path tmpDir = fs::temp_directory_path();
  fs::path input = tmpDir / "trunc_audio.rbt";
  fs::path outDir = tmpDir / "trunc_audio_out";
  if (fs::exists(outDir)) {
    fs::remove_all(outDir);
  }  
  fs::create_directories(outDir);

  auto data = build_header();
  auto primer = build_primer_header();
  data.insert(data.end(), primer.begin(), primer.end());
  for (size_t i = 0; i < kRunwayBytes; ++i)
    data.push_back(0x88); // even primer data

  push16(data, 2);  // frame size
  push16(data, 22); // packet size (frame + audio block 20)

  for (int i = 0; i < 256; ++i)
    push32(data, 0); // cue times
  for (int i = 0; i < 256; ++i)
    push16(data, 0); // cue values

  // Pad to 2048-byte boundary
  data.resize(((data.size() + 2047) / 2048) * 2048, 0);

  // Frame data (numCels = 0)
  data.push_back(0); // numCels low byte
  data.push_back(0); // numCels high byte

  // Audio block truncated to 20 bytes instead of 24
  push32(data, kBlockPosHalfSamples); // pos (even)
  push32(data, 2); // size (payload bytes only)
  data.push_back(0x88);
  data.push_back(0x77);
  for (int i = 0; i < 10; ++i)
    data.push_back(0); // padding to reach 20 bytes

  std::ofstream out(input, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();

  robot::RobotExtractor extractor(input, outDir, true);
  REQUIRE_NOTHROW(extractor.extract());

  const auto evenStream =
      robot::RobotExtractorTester::buildChannelStream(extractor, true);

  std::vector<std::byte> primerBytes(kRunwayBytes,
                                     std::byte{static_cast<unsigned char>(0x88)});
  int16_t evenPredictor = 0;
  const auto primerSamples =
      audio_test::decompress_primer(primerBytes, evenPredictor);

  std::vector<std::byte> block(kZeroPrefixBytes + kTruncatedPayloadBytes,
                               std::byte{0});
  block[kZeroPrefixBytes + 0] = std::byte{static_cast<unsigned char>(0x88)};
  block[kZeroPrefixBytes + 1] = std::byte{static_cast<unsigned char>(0x77)};
  const auto blockSamples = decompress_without_runway(block, evenPredictor);

  audio_test::ChannelExpectation expectedEven;
  audio_test::append_expected(expectedEven, 0, primerSamples);
  audio_test::append_expected(expectedEven,
                              static_cast<int32_t>(kBlockPosHalfSamples),
                              blockSamples);

  REQUIRE(evenStream == expectedEven.samples);

  fs::path wavPath = outDir / "frame_00000.wav";
  REQUIRE(fs::exists(wavPath));

  std::ifstream wav(wavPath, std::ios::binary);
  REQUIRE(wav);
  auto layout = read_wav_layout(wav);
  REQUIRE(layout.dataSize % (layout.numChannels * sizeof(int16_t)) == 0);
  std::vector<int16_t> interleaved(layout.dataSize / sizeof(int16_t));
  if (!interleaved.empty()) {
    wav.read(reinterpret_cast<char *>(interleaved.data()),
             static_cast<std::streamsize>(layout.dataSize));
    REQUIRE(wav.gcount() == static_cast<std::streamsize>(layout.dataSize));
  }

  std::vector<int16_t> evenSamples;
  evenSamples.reserve(interleaved.size() / 2);
  for (size_t i = 0; i < interleaved.size(); i += 2) {
    evenSamples.push_back(interleaved[i]);
  }

  REQUIRE(evenSamples == evenStream);
}
